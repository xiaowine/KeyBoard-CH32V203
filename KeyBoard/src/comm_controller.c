#include "comm_controller.h"

#include <stdlib.h>
#include <string.h>

#include "usb_desc.h"
#include "usb_endp.h"

void comm_recv_process();
void comm_send_process();

ReceiveHandle receive_handle = {0};
SendHandle send_handle = {0};

static uint16_t parse_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void release_receive_payload_buffer(void)
{
    if (receive_handle.payload_buf != NULL)
    {
        free(receive_handle.payload_buf);
        receive_handle.payload_buf = NULL;
    }
    receive_handle.expected_payload_len = 0;
    receive_handle.received_payload_len = 0;
}

void load_no_payload_flag(const FRAME_TYPE type)
{
    memset(&send_handle.frame_data, 0, sizeof(send_handle.frame_data));
    send_handle.frame_data = (FrameData){
        .type = type,
        .payload_length = 0,
    };
    send_handle.send_pending = 1;
}

void comm_send(FrameData frame_data)
{
    uint32_t words[CRC_WORD_SIZE] = {0};
    memcpy(words, &frame_data, CRC_BYTES_SIZE);
    CRC_ResetDR();
    frame_data.crc = CRC_CalcBlockCRC(words, CRC_WORD_SIZE);
    USBD_SendCustomData((uint8_t *)&frame_data, sizeof(FrameData));
}

void comm_controller_process()
{
    comm_recv_process();
    comm_send_process();
}

void comm_recv_process()
{
    const uint8_t data_cnt = USBD_CustomDataCnt();
    if (data_cnt == DEF_ENDP_SIZE_CUSTOM)
    {
        FrameData frame_data;
        const uint8_t *p = USBD_GetCustomData();
        memcpy(&frame_data, p, sizeof(frame_data));
        if (frame_data.seq_num > SEQ_MAX_NUM)
        {
            load_no_payload_flag(FRAME_TYPE_ERROR);
            return;
        }

        uint32_t words[CRC_WORD_SIZE] = {0};
        const uint32_t frame_crc = frame_data.crc;
        frame_data.crc = 0;
        memcpy(words, &frame_data, CRC_BYTES_SIZE);
        CRC_ResetDR();
        if (frame_crc != CRC_CalcBlockCRC(words, CRC_WORD_SIZE))
        {
            load_no_payload_flag(FRAME_TYPE_NACK);
            receive_handle.retry_count++;
            return;
        }

        receive_handle.retry_count = 0;
        switch (frame_data.type)
        {
        case FRAME_TYPE_START:
            if (frame_data.seq_num == 0)
            {
                uint16_t payload_all_len = 0;
                payload_all_len = parse_u16_le(frame_data.payload.data);
                if (payload_all_len > FRAME_RECV_MAX_BYTES)
                {
                    load_no_payload_flag(FRAME_TYPE_ERROR);
                    break;
                }

                release_receive_payload_buffer();
                if (payload_all_len > 0)
                {
                    receive_handle.payload_buf = (uint8_t *)malloc(payload_all_len);
                    if (receive_handle.payload_buf == NULL)
                    {
                        load_no_payload_flag(FRAME_TYPE_ERROR);
                        break;
                    }
                }

                receive_handle.expected_payload_len = payload_all_len;
                receive_handle.received_payload_len = 0;
                receive_handle.last_sqe_num = 0;
                load_no_payload_flag(FRAME_TYPE_ACK);
            }
            else
            {
                release_receive_payload_buffer();
                load_no_payload_flag(FRAME_TYPE_ERROR);
            }
            break;
        case FRAME_TYPE_DATA:
            if (frame_data.seq_num == receive_handle.last_sqe_num + 1)
            {
                const uint16_t frame_payload_len = frame_data.payload_length;

                if (frame_payload_len > FRAME_PAYLOAD_DATA_SIZE || frame_payload_len == 0)
                {
                    release_receive_payload_buffer();
                    load_no_payload_flag(FRAME_TYPE_ERROR);
                    break;
                }

                if (frame_payload_len > receive_handle.expected_payload_len - receive_handle.received_payload_len)
                {
                    release_receive_payload_buffer();
                    load_no_payload_flag(FRAME_TYPE_ERROR);
                    break;
                }

                if (receive_handle.payload_buf == NULL)
                {
                    load_no_payload_flag(FRAME_TYPE_ERROR);
                    break;
                }
                memcpy(receive_handle.payload_buf + receive_handle.received_payload_len,
                       frame_data.payload.data, frame_payload_len);

                receive_handle.received_payload_len += frame_payload_len;
                receive_handle.last_sqe_num++;
                load_no_payload_flag(FRAME_TYPE_ACK);
                if (receive_handle.received_payload_len == receive_handle.expected_payload_len)
                {
                    // TODO
                }
            }
            else
            {
                release_receive_payload_buffer();
                load_no_payload_flag(FRAME_TYPE_ERROR);
            }

            break;
        case FRAME_TYPE_ACK:
            send_handle.receive_ack = 1;
            break;
        case FRAME_TYPE_NACK:
            send_handle.status = SEND_STATUS_RETRY;
            send_handle.retry_count++;
            break;
        case FRAME_TYPE_ERROR:
            release_receive_payload_buffer();
            memset(&receive_handle, 0, sizeof(receive_handle));
            break;
        default:
            break;
        }
    }
    else
    {
        if (receive_handle.need_ack)
        {
            receive_handle.retry_count++;
        }
    }
    if (receive_handle.retry_count > RETRY_MAX_CNT)
    {
        release_receive_payload_buffer();
        memset(&receive_handle, 0, sizeof(receive_handle));
        PRINT("Receive data retry count exceeded, resetting state.\r\n");
    }
}

void comm_send_process()
{
    if (send_handle.send_pending && send_handle.status == SEND_STATUS_IDLE)
    {
        send_handle.status = SEND_STATUS_FRAME;
    }

    switch (send_handle.status)
    {
    case SEND_STATUS_FRAME:
    {
        send_handle.last_status = SEND_STATUS_FRAME;
        comm_send(send_handle.frame_data);
        send_handle.send_pending = 0;
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
        break;
    }
    case SEND_STATUS_WAIT_RESPONSE:
    {
        if (send_handle.receive_ack)
        {
            send_handle.receive_ack = 0;
            switch (send_handle.last_status)
            {
            case SEND_STATUS_FRAME:
            {
                // TODO 是否发送完，没发送完继续发送下一帧然后等待响应
                // TODO 发送完了就直接置空闲状态
                send_handle.status = SEND_STATUS_IDLE;
                break;
            }
            case SEND_STATUS_RETRY:
            {
                send_handle.retry_count = 0;
                send_handle.status = SEND_STATUS_IDLE;
                // TODO 重发完成，是否发送完，没发送完跳回SEND_STATUS_FRAME
                // TODO 发送完了就直接置空闲状态
                break;
            }
            default:
                send_handle.status = SEND_STATUS_IDLE;
                break;
            }
        }
        else
        {
            send_handle.retry_count++;
            send_handle.status = SEND_STATUS_RETRY;
        }

        if (send_handle.retry_count > RETRY_MAX_CNT)
        {
            memset(&send_handle.frame_data, 0, sizeof(send_handle.frame_data));
            send_handle.frame_data = (FrameData){
                .type = FRAME_TYPE_ERROR,
                .payload_length = 0,
            };
            comm_send(send_handle.frame_data);
            PRINT("Send data retry count exceeded, resetting state.\r\n");
            send_handle.status = SEND_STATUS_IDLE;
        }
        break;
    }
    case SEND_STATUS_RETRY:
    {
        send_handle.last_status = SEND_STATUS_RETRY;
        comm_send(send_handle.frame_data);
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
        break;
    }
    default:
    {
        break;
    }
    }
}
