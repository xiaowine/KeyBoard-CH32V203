/********************* HID Communication Implementation *******************
 * File Name          : hid_comm.c
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : HID communication implementation
 *************************************************************************/
#include "hid_comm.h"
#include <string.h>
#include "usb_endp.h"
#include "debug.h"
#include <stdlib.h>

SendHandle_t hSendHid = {0};       // 全局发送句柄
ReceiveHandle_t hReceiveHid = {0}; // 全局接收句柄

/* 接收重组缓冲区：由库在收到 START/SINGLE 时按需分配（malloc），在完成或复位时由库释放，以节省 RAM */

/* ----------------------- 辅助静态函数（工具型） ----------------------- */
/* 校验一帧的 CRC32：协议定义 CRC 覆盖 Offset 0~27（共 28 字节）。这里按 7 个 32-bit word 喂给硬件 CRC 外设。 */
static uint8_t hid_comm_check_crc(const HidFrame_t *frame)
{
    volatile uint32_t crc_words[7];
    memcpy((void *)crc_words, frame, 28U);
    CRC_ResetDR();
    uint32_t calc = CRC_CalcBlockCRC((uint32_t *)crc_words, 7U);
    return (calc == frame->crc32) ? 1U : 0U;
}

/* 发送控制帧（ACK/NACK/BUSY/OF）：len 固定为 0，payload 全 0，自动补算 CRC32 */
static uint8_t USBD_SendCustomData_ctrl_frame(uint8_t frame_type, uint8_t data_type, uint8_t seq)
{
    HidFrame_t tx_frame;
    uint32_t crc_words[7] = {0};

    memset(&tx_frame, 0, sizeof(tx_frame));
    tx_frame.ctrl.type = frame_type;
    tx_frame.ctrl.len = 0;
    tx_frame.data_type = data_type;
    tx_frame.seq = seq;

    memcpy(crc_words, &tx_frame, 28U);
    CRC_ResetDR();
    tx_frame.crc32 = CRC_CalcBlockCRC(crc_words, 7U);

    return USBD_SendCustomData((const uint8_t *)&tx_frame, sizeof(HidFrame_t));
}

/* 发送单帧（含计算 CRC） */
static uint8_t USBD_SendCustomData_frame(HidFrame_t *tx_frame)
{
    uint32_t crc_words[7] = {0};
    memcpy(crc_words, tx_frame, 28U);
    CRC_ResetDR();
    tx_frame->crc32 = CRC_CalcBlockCRC(crc_words, 7U);
    return USBD_SendCustomData((const uint8_t *)tx_frame, sizeof(HidFrame_t));
}

/* ----------------------- 状态/重置相关工具函数 ----------------------- */
/* 丢弃当前 send 任务（收到 BUSY/OF 或超时耗尽时使用） */
static void hid_comm_abort_send(void)
{
    hSendHid.p_buf = 0;
    hSendHid.total_len = 0;
    hSendHid.sent_len = 0;
    hSendHid.data_type = 0;
    hSendHid.curr_seq = 0;
    hSendHid.status.ack_flag = 0U;
    hSendHid.status.retry_cnt = 0U;
    hSendHid.state = SEND_IDLE;
}

/* 复位 receive 状态机到空闲态。说明：不清空 last_seq，便于在必要时保留去重上下文。 */
static void hid_comm_reset_receive(void)
{
    if (hReceiveHid.p_buf != NULL)
    {
        free(hReceiveHid.p_buf);
        hReceiveHid.p_buf = NULL;
        hReceiveHid.capacity = 0;
    }
    hReceiveHid.state = RECEIVE_IDLE;
    hReceiveHid.total_len = 0;
    hReceiveHid.recved_len = 0;
    hReceiveHid.data_type = 0;
    hReceiveHid.retry_cnt = 0U;
}

/* 接收失败计数：在可重试场景中累计，超限则丢弃任务以避免卡住 */
static void hid_comm_on_receive_retry_fail(const HidFrame_t *frame)
{
    if (hReceiveHid.retry_cnt < 0xFFU)
    {
        hReceiveHid.retry_cnt++;
    }

    if (hReceiveHid.retry_cnt >= HID_RETRY_LIMIT)
    {
        PRINT("HID receive retry exhausted, drop task type=%d seq=%d\r\n", frame->data_type, frame->seq);
        hReceiveHid.state = RECEIVE_ERROR;
        hid_comm_reset_receive();
    }
}

/* 非阻塞启动一次发送任务：0 成功，1 表示当前发送器忙或参数错误 */
uint8_t hid_comm_start_send(const uint8_t *data, uint16_t len, uint8_t data_type)
{
    if (data == NULL || len == 0)
        return 1U;

    if (hSendHid.state != SEND_IDLE)
    {
        return 1U; // busy
    }

    hSendHid.p_buf = data;
    hSendHid.total_len = len;
    hSendHid.sent_len = 0U;
    hSendHid.data_type = data_type;
    hSendHid.curr_seq = 0U;
    hSendHid.status.ack_flag = 0U;
    hSendHid.status.retry_cnt = 0U;
    hSendHid.timeout_ticks = 0U;

    if (len <= sizeof(((HidFrame_t *)0)->payload))
    {
        /* 单包发送：构造 SINGLE 并发送 */
        HidFrame_t tx = {0};
        tx.ctrl.type = FRAME_TYPE_SINGLE;
        tx.ctrl.len = (uint8_t)len;
        tx.data_type = data_type;
        tx.seq = 0U;
        if (len > 0U)
        {
            memcpy(tx.payload, data, len);
        }
        hSendHid.curr_seq = 0U;
        hSendHid.state = SEND_WAIT_ACK;
        hSendHid.status.retry_cnt = 0U;
        hSendHid.timeout_ticks = 0U;
        USBD_SendCustomData_frame(&tx);
        return 0U;
    }

    /* 多包：先发送 START（payload 前 4 字节为 total_size） */
    {
        HidFrame_t tx = {0};
        tx.ctrl.type = FRAME_TYPE_START;
        uint32_t total_size = (uint32_t)len;
        const uint8_t first_data_bytes = (uint8_t)(sizeof(tx.payload) - 4U); /* 20 bytes */
        uint8_t first_payload_len = (uint8_t)((len > first_data_bytes) ? first_data_bytes : len);

        tx.ctrl.len = (uint8_t)(4U + first_payload_len);
        tx.data_type = data_type;
        tx.seq = 0U;
        memcpy(tx.payload, &total_size, 4U);
        if (first_payload_len > 0U)
        {
            memcpy(&tx.payload[4], data, first_payload_len);
        }

        hSendHid.sent_len = first_payload_len;
        hSendHid.curr_seq = 0U;
        hSendHid.state = SEND_WAIT_ACK;
        hSendHid.status.retry_cnt = 0U;
        hSendHid.timeout_ticks = 0U;
        USBD_SendCustomData_frame(&tx);
    }

    return 0U;
}

/* ----------------------- 发送/接收状态机（驱动） ----------------------- */
/* 发送状态机：由 hid_comm_process 周期性调用（非阻塞），负责分片与重传 */
static void hid_comm_process_send(void)
{
    if (hSendHid.state == SEND_IDLE)
        return;

    if (hSendHid.state == SEND_WAIT_ACK)
    {
        if (hSendHid.status.ack_flag)
        {
            hSendHid.status.ack_flag = 0U;
            hSendHid.status.retry_cnt = 0U;
            hSendHid.timeout_ticks = 0U;

            if (hSendHid.total_len <= sizeof(((HidFrame_t *)0)->payload))
            {
                hid_comm_abort_send();
                return;
            }

            if ((uint32_t)hSendHid.sent_len >= (uint32_t)hSendHid.total_len)
            {
                hid_comm_abort_send();
                return;
            }

            uint16_t remain = (uint16_t)(hSendHid.total_len - hSendHid.sent_len);
            HidFrame_t tx = {0};
            uint8_t to_copy = (uint8_t)(remain > sizeof(tx.payload) ? sizeof(tx.payload) : remain);
            if ((uint32_t)hSendHid.sent_len + (uint32_t)to_copy == (uint32_t)hSendHid.total_len)
            {
                tx.ctrl.type = FRAME_TYPE_END;
            }
            else
            {
                tx.ctrl.type = FRAME_TYPE_DATA;
            }
            tx.ctrl.len = to_copy;
            tx.data_type = hSendHid.data_type;
            hSendHid.curr_seq = (uint8_t)(hSendHid.curr_seq + 1U);
            tx.seq = hSendHid.curr_seq;
            if (to_copy > 0U)
            {
                memcpy(tx.payload, &hSendHid.p_buf[hSendHid.sent_len], to_copy);
            }

            hSendHid.sent_len = (uint16_t)(hSendHid.sent_len + to_copy);
            hSendHid.state = SEND_WAIT_ACK;
            hSendHid.timeout_ticks = 0U;
            USBD_SendCustomData_frame(&tx);
            return;
        }

        hSendHid.timeout_ticks++;
        if (hSendHid.timeout_ticks >= HID_RETRY_LIMIT)
        {
            hSendHid.timeout_ticks = 0U;
            if (hSendHid.status.retry_cnt >= HID_RETRY_LIMIT)
            {
                PRINT("HID TX retry exhausted, drop task\r\n");
                hid_comm_abort_send();
                return;
            }
            hSendHid.status.retry_cnt++;
            hSendHid.state = SEND_RETRY;
        }
    }

    if (hSendHid.state == SEND_RETRY)
    {
        HidFrame_t tx = {0};
        if (hSendHid.curr_seq == 0U && hSendHid.total_len > sizeof(tx.payload))
        {
            tx.ctrl.type = FRAME_TYPE_START;
            uint32_t total_size = (uint32_t)hSendHid.total_len;
            const uint8_t first_data_bytes = (uint8_t)(sizeof(tx.payload) - 4U);
            uint8_t first_payload_len = (uint8_t)((hSendHid.total_len > first_data_bytes) ? first_data_bytes : hSendHid.total_len);
            tx.ctrl.len = (uint8_t)(4U + first_payload_len);
            tx.data_type = hSendHid.data_type;
            tx.seq = 0U;
            memcpy(tx.payload, &total_size, 4U);
            if (first_payload_len > 0U)
            {
                memcpy(&tx.payload[4], hSendHid.p_buf, first_payload_len);
            }
            USBD_SendCustomData_frame(&tx);
            hSendHid.state = SEND_WAIT_ACK;
            return;
        }

        {
            uint8_t seq = hSendHid.curr_seq;
            uint16_t offset = 0U;
            if (seq == 0U)
            {
                offset = 0U;
            }
            else
            {
                const uint8_t first_data_bytes = (uint8_t)(sizeof(((HidFrame_t *)0)->payload) - 4U);
                offset = first_data_bytes + (uint16_t)((uint32_t)(seq - 1U) * (uint32_t)sizeof(((HidFrame_t *)0)->payload));
            }

            uint16_t remain = 0U;
            if (offset < hSendHid.total_len)
            {
                remain = (uint16_t)(hSendHid.total_len - offset);
            }
            else
            {
                remain = 0U;
            }

            uint8_t to_copy = (uint8_t)(remain > sizeof(tx.payload) ? sizeof(tx.payload) : remain);
            if (to_copy == 0U)
            {
                hid_comm_abort_send();
                return;
            }
            tx.ctrl.len = to_copy;
            tx.data_type = hSendHid.data_type;
            tx.seq = seq;
            if ((uint32_t)offset + (uint32_t)to_copy == (uint32_t)hSendHid.total_len)
            {
                tx.ctrl.type = FRAME_TYPE_END;
            }
            else
            {
                tx.ctrl.type = FRAME_TYPE_DATA;
            }
            memcpy(tx.payload, &hSendHid.p_buf[offset], to_copy);
            USBD_SendCustomData_frame(&tx);
            hSendHid.state = SEND_WAIT_ACK;
            return;
        }
    }
}

/* 接收处理单元：从 USB 读取一帧并处理（包含 CRC 校验和状态机） */
static void hid_comm_process_recv(void)
{
    static uint8_t rx_buffer[HID_COMM_DATA_SIZE] = {0};

    if (hReceiveHid.last_seq == 0U && hReceiveHid.state == RECEIVE_IDLE)
    {
        hReceiveHid.last_seq = 0xFFU;
        hReceiveHid.retry_cnt = 0U;
    }

    const uint16_t received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));

    if (received_len == 0 || received_len != sizeof(rx_buffer))
    {
        return;
    }
    const HidFrame_t *frame = (const HidFrame_t *)rx_buffer;
    const uint8_t frame_type = frame->ctrl.type;
    const uint8_t frame_len = frame->ctrl.len;

    if (frame_len > sizeof(frame->payload))
    {
        PRINT("HID RX error: invalid payload len=%d\r\n", frame_len);
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        return;
    }

    if (hid_comm_check_crc(frame) == 0U)
    {
        PRINT("HID RX error: CRC mismatch type=%d seq=%d\r\n", frame_type, frame->seq);
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        return;
    }

    if (frame_type == FRAME_TYPE_ACK || frame_type == FRAME_TYPE_NACK)
    {
        if (hSendHid.state != SEND_IDLE && frame->seq == hSendHid.curr_seq)
        {
            if (frame_type == FRAME_TYPE_ACK)
            {
                hSendHid.status.ack_flag = 1U;
            }
            else
            {
                hSendHid.status.ack_flag = 0U;
                if (hSendHid.status.retry_cnt < 0x7FU)
                {
                    hSendHid.status.retry_cnt++;
                }
                hSendHid.state = SEND_RETRY;
            }
        }
        return;
    }

    if (frame_type == FRAME_TYPE_BUSY || frame_type == FRAME_TYPE_OF)
    {
        if (hSendHid.state != SEND_IDLE && frame->seq == hSendHid.curr_seq)
        {
            PRINT("HID TX aborted by remote ctrl type=%d seq=%d\r\n", frame_type, frame->seq);
            hid_comm_abort_send();
        }
        return;
    }

    if (hReceiveHid.state == RECEIVE_WAIT && frame->seq == hReceiveHid.last_seq &&
        (frame_type == FRAME_TYPE_DATA || frame_type == FRAME_TYPE_END))
    {
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        return;
    }

    switch (frame_type)
    {
    case FRAME_TYPE_SINGLE:
        if (frame->seq != 0U)
        {
            PRINT("HID RX error: SINGLE frame seq must be 0, got=%d\r\n", frame->seq);
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }
        if (hReceiveHid.p_buf == NULL)
        {
            if (frame_len > 0U)
            {
                if (frame_len > HID_RECEIVE_MAX_CAPACITY)
                {
                    hReceiveHid.state = RECEIVE_ERROR;
                    USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
                    return;
                }
                uint8_t *p = (uint8_t *)malloc((size_t)frame_len);
                if (p == NULL)
                {
                    hReceiveHid.state = RECEIVE_ERROR;
                    USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
                    return;
                }
                hReceiveHid.p_buf = p;
                hReceiveHid.capacity = frame_len;
            }
        }
        if (frame_len > 0U)
        {
            memcpy(hReceiveHid.p_buf, frame->payload, frame_len);
        }
        hReceiveHid.total_len = frame_len;
        hReceiveHid.recved_len = frame_len;
        hReceiveHid.data_type = frame->data_type;
        hReceiveHid.last_seq = frame->seq;
        hReceiveHid.state = RECEIVE_COMPLETE;
        hReceiveHid.retry_cnt = 0U;
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        break;

    case FRAME_TYPE_START:
    {
        if (frame->seq != 0U)
        {
            PRINT("HID RX error: START frame seq must be 0, got=%d\r\n", frame->seq);
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_receive_retry_fail(frame);
            return;
        }
        uint32_t total_size = 0U;
        if (frame_len < 4U)
        {
            hReceiveHid.state = RECEIVE_ERROR;
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        memcpy(&total_size, frame->payload, sizeof(total_size));
        if (total_size == 0U || total_size > HID_RECEIVE_MAX_CAPACITY)
        {
            hReceiveHid.state = RECEIVE_ERROR;
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        if (hReceiveHid.p_buf != NULL && hReceiveHid.state == RECEIVE_COMPLETE)
        {
            free(hReceiveHid.p_buf);
            hReceiveHid.p_buf = NULL;
            hReceiveHid.capacity = 0;
            hReceiveHid.state = RECEIVE_IDLE;
            hReceiveHid.total_len = 0;
            hReceiveHid.recved_len = 0;
            hReceiveHid.data_type = 0;
            hReceiveHid.last_seq = 0xFFU;
            hReceiveHid.retry_cnt = 0U;
        }

        if (hReceiveHid.p_buf == NULL)
        {
            uint8_t *p = (uint8_t *)malloc((size_t)total_size);
            if (p == NULL)
            {
                hReceiveHid.state = RECEIVE_ERROR;
                USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
                return;
            }
            hReceiveHid.p_buf = p;
            hReceiveHid.capacity = (uint16_t)total_size;
        }
        else if (hReceiveHid.capacity < total_size)
        {
            hReceiveHid.state = RECEIVE_ERROR;
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        hReceiveHid.total_len = (uint16_t)total_size;
        hReceiveHid.recved_len = 0U;
        hReceiveHid.data_type = frame->data_type;
        hReceiveHid.last_seq = frame->seq;
        hReceiveHid.state = RECEIVE_WAIT;
        hReceiveHid.retry_cnt = 0U;
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        break;
    }

    case FRAME_TYPE_DATA:
    case FRAME_TYPE_END:
    {
        const uint8_t expect_seq = (uint8_t)(hReceiveHid.last_seq + 1U);
        if (hReceiveHid.p_buf == NULL || hReceiveHid.state != RECEIVE_WAIT)
        {
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        if (frame->seq != expect_seq || frame->data_type != hReceiveHid.data_type)
        {
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_receive_retry_fail(frame);
            return;
        }

        if ((uint32_t)hReceiveHid.recved_len + (uint32_t)frame_len > (uint32_t)hReceiveHid.total_len)
        {
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_receive_retry_fail(frame);
            return;
        }

        if (frame_type == FRAME_TYPE_END &&
            ((uint32_t)hReceiveHid.recved_len + (uint32_t)frame_len != (uint32_t)hReceiveHid.total_len))
        {
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_receive_retry_fail(frame);
            return;
        }

        if (frame_len > 0U)
        {
            memcpy(&hReceiveHid.p_buf[hReceiveHid.recved_len], frame->payload, frame_len);
        }
        hReceiveHid.recved_len = (uint16_t)(hReceiveHid.recved_len + frame_len);
        hReceiveHid.last_seq = frame->seq;
        hReceiveHid.retry_cnt = 0U;

        if (frame_type == FRAME_TYPE_END)
        {
            hReceiveHid.state = RECEIVE_COMPLETE;
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        }
        else
        {
            USBD_SendCustomData_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        }
        break;
    }

    default:
        USBD_SendCustomData_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        break;
    }

    if (hReceiveHid.state == RECEIVE_COMPLETE)
    {
        PRINT("HID RX complete: type=%d len=%d seq=%d\r\n", hReceiveHid.data_type, hReceiveHid.total_len,
              hReceiveHid.last_seq);
    }
}

/*********************************************************************
 * @fn      hid_comm_process
 *
 * @brief   由主循环周期性调用以驱动发送/接收状态机（先驱动发送以保证推进）。
 *
 * @return  None
 */
void hid_comm_process(void)
{
    /* 先驱动发送状态机（非阻塞），以确保即便没有收到新的 RX 包也能推进发送 */
    hid_comm_process_send();
    /* 再驱动接收处理单元 */
    hid_comm_process_recv();
}
