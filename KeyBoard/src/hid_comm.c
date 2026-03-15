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

/* External functions from usb_endp.c */
/* Static variables */
static uint8_t rx_buffer[HID_COMM_DATA_SIZE] = {0};
/* 接收重组缓冲区：最大 256 帧 * 24 字节 Payload = 6144 字节 */
static uint8_t rx_task_buffer[24U * 256U] = {0};

/* 丢弃当前 TX 任务（收到 BUSY/OF 等不可继续信号时使用） */
static void hid_comm_abort_tx(void)
{
    hTxHid.p_buf = 0;
    hTxHid.total_len = 0;
    hTxHid.sent_len = 0;
    hTxHid.data_type = 0;
    hTxHid.curr_seq = 0;
    hTxHid.status.ack_flag = 0U;
    hTxHid.status.retry_cnt = 0U;
    hTxHid.state = TX_IDLE;
}

/*
 * 复位 RX 状态机到空闲态。
 * 说明：不清空 last_seq，便于在必要时保留去重上下文；
 * 当前实现中，进入新 START 会覆盖 last_seq。
 */
static void hid_comm_reset_rx(void)
{
    hRxHid.state = RX_IDLE;
    hRxHid.total_len = 0;
    hRxHid.recved_len = 0;
    hRxHid.data_type = 0;
    hRxHid.retry_cnt = 0U;
}

/*
 * RX 失败计数：在可重试场景（如 seq/type/长度不匹配）中累计。
 * 达到上限后，丢弃当前接收任务，避免状态机长期卡住。
 */
static void hid_comm_on_rx_retry_fail(const HidFrame_t *frame)
{
    if (hRxHid.retry_cnt < 0xFFU)
    {
        hRxHid.retry_cnt++;
    }

    if (hRxHid.retry_cnt >= HID_RX_RETRY_LIMIT)
    {
        PRINT("HID RX retry exhausted, drop task type=%d seq=%d\r\n", frame->data_type, frame->seq);
        hRxHid.state = RX_ERROR;
        hid_comm_reset_rx();
    }
}

/*
 * 校验一帧的 CRC32：
 * - 协议定义 CRC 覆盖 Offset 0~27（共 28 字节）
 * - 这里按 7 个 32-bit word 喂给硬件 CRC 外设
 */
static uint8_t hid_comm_check_crc(const HidFrame_t *frame)
{
    volatile uint32_t crc_words[7];
    memcpy((void *)crc_words, frame, 28U);
    CRC_ResetDR();
    uint32_t calc = CRC_CalcBlockCRC((uint32_t *)crc_words, 7U);
    return (calc == frame->crc32) ? 1U : 0U;
}

/*
 * 发送控制帧（ACK/NACK 等）：
 * - len 固定为 0
 * - payload 全 0
 * - 自动补算 CRC32
 */
static uint8_t hid_comm_send_ctrl_frame(uint8_t frame_type, uint8_t data_type, uint8_t seq)
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

    return hid_comm_send((const uint8_t *)&tx_frame, sizeof(HidFrame_t));
}

/*********************************************************************
 * @fn      hid_comm_send
 *
 * @brief   Send data through HID communication (up to 32-byte payload)

 * @param   data - pointer to data buffer
 * @param len should not exceed HID_COMM_DATA_SIZE
 *
 * @return  Status (0=success, 1=error)
 */
uint8_t hid_comm_send(const uint8_t *data, const uint16_t len)
{
    return USBD_SendCustomData(data, len);
}

/*********************************************************************
 * @fn      hid_comm_process
 *
 * @brief   Process received HID communication data
 *
 * @return  None
 */
void hid_comm_process(void)
{
    /* 懒初始化：首次进入时绑定默认接收缓冲区 */
    if (hRxHid.p_buf == 0)
    {
        hRxHid.p_buf = rx_task_buffer;
        hRxHid.capacity = (uint16_t)sizeof(rx_task_buffer);
        hRxHid.last_seq = 0xFFU;
        hRxHid.retry_cnt = 0U;
        hRxHid.state = RX_IDLE;
    }

    const uint16_t received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));

    if (received_len == 0 || received_len != sizeof(rx_buffer))
    {
        /* 无新包或包长异常：直接返回，不阻塞主循环 */
        return;
    }
    const HidFrame_t *frame = (const HidFrame_t *)rx_buffer;
    const uint8_t frame_type = frame->ctrl.type;
    const uint8_t frame_len = frame->ctrl.len;

    if (frame_len > sizeof(frame->payload))
    {
        /* 帧内长度字段非法（协议最大 24） */
        PRINT("HID RX error: invalid payload len=%d\r\n", frame_len);
        hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        return;
    }

    if (hid_comm_check_crc(frame) == 0U)
    {
        /* CRC 错误：回 NACK 通知对端重发 */
        PRINT("HID RX error: CRC mismatch type=%d seq=%d\r\n", frame_type, frame->seq);
        hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        return;
    }

    /* 对端 ACK/NACK：驱动 TX 状态机推进或进入重发 */
    if (frame_type == FRAME_TYPE_ACK || frame_type == FRAME_TYPE_NACK)
    {
        if (hTxHid.state != TX_IDLE && frame->seq == hTxHid.curr_seq)
        {
            if (frame_type == FRAME_TYPE_ACK)
            {
                hTxHid.status.ack_flag = 1U;
            }
            else
            {
                hTxHid.status.ack_flag = 0U;
                if (hTxHid.status.retry_cnt < 0x7FU)
                {
                    hTxHid.status.retry_cnt++;
                }
                hTxHid.state = TX_RETRY;
            }
        }
        return;
    }

    /* 对端 BUSY/OF：抛弃当前整个发送消息 */
    if (frame_type == FRAME_TYPE_BUSY || frame_type == FRAME_TYPE_OF)
    {
        if (hTxHid.state != TX_IDLE && frame->seq == hTxHid.curr_seq)
        {
            PRINT("HID TX aborted by remote ctrl type=%d seq=%d\r\n", frame_type, frame->seq);
            hid_comm_abort_tx();
        }
        return;
    }

    /*
     * 去重逻辑（ACK 丢失场景）：
     * 若处于 RX_WAIT 且收到与上一帧同 seq 的 DATA/END，
     * 说明对端重发了上一帧，此时只重发 ACK，不重复写入 payload。
     */
    if (hRxHid.state == RX_WAIT && frame->seq == hRxHid.last_seq &&
        (frame_type == FRAME_TYPE_DATA || frame_type == FRAME_TYPE_END))
    {
        hid_comm_send_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        return;
    }

    switch (frame_type)
    {
    case FRAME_TYPE_SINGLE:
        /* 单包：payload 直接作为完整任务数据 */
        if (frame_len > hRxHid.capacity)
        {
            hRxHid.state = RX_ERROR;
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }
        if (frame_len > 0U)
        {
            memcpy(hRxHid.p_buf, frame->payload, frame_len);
        }
        hRxHid.total_len = frame_len;
        hRxHid.recved_len = frame_len;
        hRxHid.data_type = frame->data_type;
        hRxHid.last_seq = frame->seq;
        hRxHid.state = RX_COMPLETE;
        hRxHid.retry_cnt = 0U;
        hid_comm_send_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        break;

    case FRAME_TYPE_START:
    {
        /* START 约定：payload 前 4 字节为 total_size（小端） */
        uint32_t total_size = 0U;
        if (frame_len < 4U)
        {
            hRxHid.state = RX_ERROR;
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        memcpy(&total_size, frame->payload, sizeof(total_size));
        if (total_size == 0U || total_size > hRxHid.capacity)
        {
            hRxHid.state = RX_ERROR;
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        hRxHid.total_len = (uint16_t)total_size;
        hRxHid.recved_len = 0U;
        hRxHid.data_type = frame->data_type;
        hRxHid.last_seq = frame->seq;
        hRxHid.state = RX_WAIT;
        hRxHid.retry_cnt = 0U;
        hid_comm_send_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        break;
    }

    case FRAME_TYPE_DATA:
    case FRAME_TYPE_END:
    {
        /* DATA/END 必须严格按顺序到达：seq = last_seq + 1 */
        const uint8_t expect_seq = (uint8_t)(hRxHid.last_seq + 1U);
        if (hRxHid.state != RX_WAIT)
        {
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            return;
        }

        if (frame->seq != expect_seq || frame->data_type != hRxHid.data_type)
        {
            /* 可重试错误：保持 RX_WAIT，不丢上下文，等待对端按 NACK 重发 */
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_rx_retry_fail(frame);
            return;
        }

        /* 长度保护：累计接收长度不能超过 START 声明长度 */
        if ((uint32_t)hRxHid.recved_len + (uint32_t)frame_len > (uint32_t)hRxHid.total_len)
        {
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_rx_retry_fail(frame);
            return;
        }

        /* END 包要求正好收满 total_len；不满足则请求该帧重发 */
        if (frame_type == FRAME_TYPE_END &&
            ((uint32_t)hRxHid.recved_len + (uint32_t)frame_len != (uint32_t)hRxHid.total_len))
        {
            hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
            hid_comm_on_rx_retry_fail(frame);
            return;
        }

        if (frame_len > 0U)
        {
            /* 复制本帧有效 payload 到重组缓冲区 */
            memcpy(&hRxHid.p_buf[hRxHid.recved_len], frame->payload, frame_len);
        }
        hRxHid.recved_len = (uint16_t)(hRxHid.recved_len + frame_len);
        hRxHid.last_seq = frame->seq;
        hRxHid.retry_cnt = 0U;

        if (frame_type == FRAME_TYPE_END)
        {
            hRxHid.state = RX_COMPLETE;
            hid_comm_send_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        }
        else
        {
            hid_comm_send_ctrl_frame(FRAME_TYPE_ACK, frame->data_type, frame->seq);
        }
        break;
    }

    default:
        /* 未定义帧类型 */
        hid_comm_send_ctrl_frame(FRAME_TYPE_NACK, frame->data_type, frame->seq);
        break;
    }

    /* 完成态仅打印日志，数据仍保留在 hRxHid.p_buf 中等待上层处理 */
    if (hRxHid.state == RX_COMPLETE)
    {
        PRINT("HID RX complete: type=%d len=%d seq=%d\r\n", hRxHid.data_type, hRxHid.total_len, hRxHid.last_seq);
    }
}
