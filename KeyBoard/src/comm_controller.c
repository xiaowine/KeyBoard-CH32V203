#include "comm_controller.h"

#include <stdlib.h>
#include <string.h>

#include "usb_desc.h"
#include "usb_endp.h"



static void comm_recv_process(void);
static void comm_send_process(void);
static void reset_send_state(void);
static void clear_reply_session(void);
static void abort_reply_session(void);
static void queue_control_frame(FRAME_TYPE type);
static void handle_send_ack(void);
static void handle_send_retry_overflow(void);
static uint8_t prepare_next_frame(FrameData *out_frame, TX_SOURCE *out_source);
static void dispatch_received_payload(uint8_t payload_type, const uint8_t *payload, uint16_t payload_len);

static ReceiveHandle receive_handle = {0};
static SendHandle send_handle = {0};
static ControlQueue control_queue = {0};
static ReplySession reply_session = {0};
static comm_rx_callback_t rx_callback = NULL;

static uint16_t parse_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void write_u16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFu);
    buf[1] = (uint8_t)(value >> 8);
}

static void release_receive_payload_buffer(void)
{
    if (receive_handle.payload_buf != NULL)
    {
        free(receive_handle.payload_buf);
    }
    memset(&receive_handle, 0, sizeof(receive_handle));
}

static void release_reply_payload_buffer(void)
{
    if (reply_session.payload_buf != NULL)
    {
        free(reply_session.payload_buf);
    }
}

static void clear_reply_session(void)
{
    release_reply_payload_buffer();
    memset(&reply_session, 0, sizeof(reply_session));
}

static void reset_send_state(void)
{
    memset(&send_handle.frame_data, 0, sizeof(send_handle.frame_data));
    send_handle.status = SEND_STATUS_IDLE;
    send_handle.last_status = SEND_STATUS_IDLE;
    send_handle.retry_count = 0;
    send_handle.receive_ack = 0;
    send_handle.receive_nack = 0;
    send_handle.source = TX_SOURCE_NONE;
}

static void abort_reply_session(void)
{
    if (send_handle.source == TX_SOURCE_REPLY_START || send_handle.source == TX_SOURCE_REPLY_DATA)
    {
        reset_send_state();
    }
    clear_reply_session();
}

static void print_received_payload_content(const uint8_t *buf, uint16_t len)
{
    const uint16_t show_len = (len > 128u) ? 128u : len;
    uint16_t i = 0;

    PRINT("Received complete payload, len=%u bytes.\r\n", len);
    PRINT("Payload HEX (%u bytes): ", show_len);
    for (i = 0; i < show_len; i++)
    {
        PRINT("%02X", buf[i]);
        if (i + 1u < show_len)
        {
            PRINT(" ");
        }
    }
    if (show_len < len)
    {
        PRINT(" ...");
    }
    PRINT("\r\n");

    PRINT("Payload ASCII (%u bytes): ", show_len);
    for (i = 0; i < show_len; i++)
    {
        const uint8_t c = buf[i];
        if (c >= 32u && c <= 126u)
        {
            PRINT("%c", (char)c);
        }
        else
        {
            PRINT(".");
        }
    }
    if (show_len < len)
    {
        PRINT("...");
    }
    PRINT("\r\n");
}

static void queue_control_frame(const FRAME_TYPE type)
{
    /*
     * Single-session policy:
     * Any ACK/NACK/ERROR response to peer traffic preempts current outgoing
     * transaction, then gets sent first on next scheduler run.
     */
    reset_send_state();
    control_queue.type = type;
    control_queue.pending = 1;
}

static void comm_send(FrameData frame_data)
{
    uint32_t words[CRC_WORD_SIZE] = {0};
    memcpy(words, &frame_data, CRC_BYTES_SIZE);
    CRC_ResetDR();
    frame_data.crc = CRC_CalcBlockCRC(words, CRC_WORD_SIZE);
    USBD_SendCustomData((uint8_t *)&frame_data, sizeof(FrameData));
}

void comm_register_rx_callback(comm_rx_callback_t callback)
{
    rx_callback = callback;
}

void comm_queue_reply(uint8_t payload_type, const uint8_t *data, uint16_t len)
{
    uint8_t *new_payload_buf = NULL;

    if (len > FRAME_SEND_MAX_BYTES)
    {
        return;
    }

    if (len > 0u && data == NULL)
    {
        return;
    }

    if (len > 0u)
    {
        new_payload_buf = (uint8_t *)malloc(len);
        if (new_payload_buf == NULL)
        {
            return;
        }
        memcpy(new_payload_buf, data, len);
    }

    /*
     * Queue-full policy: overwrite existing pending/in-flight business reply.
     * Control frames are independent and remain higher priority.
     */
    abort_reply_session();

    reply_session.active = 1;
    reply_session.payload_type = payload_type;
    reply_session.next_seq_num = 1;
    reply_session.phase = REPLY_PHASE_SEND_START;
    reply_session.payload_len = len;
    reply_session.acked_payload_len = 0;
    reply_session.payload_buf = new_payload_buf;
}

static void dispatch_received_payload(uint8_t payload_type, const uint8_t *payload, uint16_t payload_len)
{
    if (rx_callback != NULL)
    {
        rx_callback(payload_type, payload, payload_len);
    }
}

void comm_controller_process(void)
{
    comm_recv_process();
    comm_send_process();
}

static void comm_recv_process(void)
{
    const uint8_t data_cnt = USBD_CustomDataCnt();
    if (data_cnt == DEF_ENDP_SIZE_CUSTOM)
    {
        FrameData frame_data;
        const uint8_t *p = USBD_GetCustomData();
        memcpy(&frame_data, p, sizeof(frame_data));

        if (frame_data.seq_num > SEQ_MAX_NUM)
        {
            queue_control_frame(FRAME_TYPE_ERROR);
            return;
        }

        {
            uint32_t words[CRC_WORD_SIZE] = {0};
            const uint32_t frame_crc = frame_data.crc;
            frame_data.crc = 0;
            memcpy(words, &frame_data, CRC_BYTES_SIZE);
            CRC_ResetDR();
            if (frame_crc != CRC_CalcBlockCRC(words, CRC_WORD_SIZE))
            {
                queue_control_frame(FRAME_TYPE_NACK);
                receive_handle.retry_count++;
            }
            else
            {
                receive_handle.retry_count = 0;

                switch (frame_data.type)
                {
                case FRAME_TYPE_START:
                {
                    if (frame_data.seq_num == 0u)
                    {
                        const uint16_t payload_all_len = parse_u16_le(frame_data.payload.data);
                        if (payload_all_len > FRAME_RECV_MAX_BYTES)
                        {
                            queue_control_frame(FRAME_TYPE_ERROR);
                            break;
                        }

                        /* New incoming session preempts any old business reply session. */
                        abort_reply_session();
                        release_receive_payload_buffer();

                        if (payload_all_len > 0u)
                        {
                            receive_handle.payload_buf = (uint8_t *)malloc(payload_all_len);
                            if (receive_handle.payload_buf == NULL)
                            {
                                queue_control_frame(FRAME_TYPE_ERROR);
                                break;
                            }
                        }

                        receive_handle.payload_type = frame_data.payload.type;
                        receive_handle.expected_payload_len = payload_all_len;
                        receive_handle.received_payload_len = 0;
                        receive_handle.last_seq_num = 0;
                        receive_handle.need_ack = payload_all_len > 0u ? 1u : 0u;

                        /* Queue ACK first, then callback can enqueue business reply. */
                        queue_control_frame(FRAME_TYPE_ACK);

                        if (payload_all_len == 0u)
                        {
                            dispatch_received_payload(receive_handle.payload_type, NULL, 0);
                            release_receive_payload_buffer();
                        }
                    }
                    else
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                    }
                    break;
                }
                case FRAME_TYPE_DATA:
                {
                    const uint16_t frame_payload_len = frame_data.payload_length;

                    if (frame_data.seq_num != (uint8_t)(receive_handle.last_seq_num + 1u) ||
                        receive_handle.expected_payload_len == 0u)
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    if (frame_payload_len == 0u || frame_payload_len > FRAME_PAYLOAD_DATA_SIZE)
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    if (frame_data.payload.type != receive_handle.payload_type)
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    if (frame_payload_len > (uint16_t)(receive_handle.expected_payload_len - receive_handle.received_payload_len))
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    if (receive_handle.payload_buf == NULL)
                    {
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    memcpy(receive_handle.payload_buf + receive_handle.received_payload_len,
                           frame_data.payload.data, frame_payload_len);
                    receive_handle.received_payload_len += frame_payload_len;
                    receive_handle.last_seq_num++;
                    receive_handle.need_ack =
                        receive_handle.received_payload_len < receive_handle.expected_payload_len ? 1u : 0u;

                    /* Queue ACK first, callback later. */
                    queue_control_frame(FRAME_TYPE_ACK);

                    if (receive_handle.received_payload_len == receive_handle.expected_payload_len)
                    {
                        print_received_payload_content(receive_handle.payload_buf, receive_handle.received_payload_len);
                        dispatch_received_payload(receive_handle.payload_type, receive_handle.payload_buf,
                                                  receive_handle.received_payload_len);
                        release_receive_payload_buffer();
                    }
                    break;
                }
                case FRAME_TYPE_ACK:
                    send_handle.receive_ack = 1;
                    break;
                case FRAME_TYPE_NACK:
                    send_handle.receive_nack = 1;
                    break;
                case FRAME_TYPE_ERROR:
                    release_receive_payload_buffer();
                    break;
                default:
                    break;
                }
            }
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
        PRINT("Receive data retry count exceeded, resetting state.\r\n");
    }
}

static uint8_t prepare_next_frame(FrameData *out_frame, TX_SOURCE *out_source)
{
    memset(out_frame, 0, sizeof(*out_frame));
    *out_source = TX_SOURCE_NONE;

    if (control_queue.pending)
    {
        out_frame->type = (uint8_t)control_queue.type;
        out_frame->payload_length = 0;
        *out_source = TX_SOURCE_CONTROL;
        return 1;
    }

    if (!reply_session.active)
    {
        return 0;
    }

    if (reply_session.phase == REPLY_PHASE_SEND_START)
    {
        out_frame->seq_num = 0;
        out_frame->type = FRAME_TYPE_START;
        out_frame->payload_length = 2;
        out_frame->payload.type = reply_session.payload_type;
        write_u16_le(out_frame->payload.data, reply_session.payload_len);
        *out_source = TX_SOURCE_REPLY_START;
        return 1;
    }

    if (reply_session.phase == REPLY_PHASE_SEND_DATA)
    {
        const uint16_t remaining_len = (uint16_t)(reply_session.payload_len - reply_session.acked_payload_len);
        if (remaining_len == 0u)
        {
            clear_reply_session();
            return 0;
        }

        {
            const uint16_t chunk_len =
                (remaining_len > FRAME_PAYLOAD_DATA_SIZE) ? FRAME_PAYLOAD_DATA_SIZE : remaining_len;

            out_frame->seq_num = reply_session.next_seq_num;
            out_frame->type = FRAME_TYPE_DATA;
            out_frame->payload_length = (uint8_t)chunk_len;
            out_frame->payload.type = reply_session.payload_type;
            memcpy(out_frame->payload.data, reply_session.payload_buf + reply_session.acked_payload_len, chunk_len);
            *out_source = TX_SOURCE_REPLY_DATA;
            return 1;
        }
    }

    return 0;
}

static void handle_send_ack(void)
{
    switch (send_handle.source)
    {
    case TX_SOURCE_CONTROL:
        control_queue.pending = 0;
        break;
    case TX_SOURCE_REPLY_START:
        if (reply_session.payload_len == 0u)
        {
            clear_reply_session();
        }
        else
        {
            reply_session.phase = REPLY_PHASE_SEND_DATA;
        }
        break;
    case TX_SOURCE_REPLY_DATA:
        reply_session.acked_payload_len =
            (uint16_t)(reply_session.acked_payload_len + send_handle.frame_data.payload_length);
        reply_session.next_seq_num++;
        if (reply_session.acked_payload_len >= reply_session.payload_len)
        {
            clear_reply_session();
        }
        break;
    default:
        break;
    }

    /* Strict pacing: after ACK, next frame is scheduled in next process tick. */
    reset_send_state();
}

static void handle_send_retry_overflow(void)
{
    if (send_handle.source == TX_SOURCE_REPLY_START || send_handle.source == TX_SOURCE_REPLY_DATA)
    {
        clear_reply_session();
    }

    PRINT("Send data retry count exceeded, resetting state.\r\n");
    queue_control_frame(FRAME_TYPE_ERROR);
}

static void comm_send_process(void)
{
    if (send_handle.status == SEND_STATUS_WAIT_RESPONSE)
    {
        if (send_handle.receive_ack)
        {
            send_handle.receive_ack = 0;
            send_handle.receive_nack = 0;
            handle_send_ack();
            return;
        }

        if (send_handle.receive_nack)
        {
            send_handle.receive_nack = 0;
            send_handle.status = SEND_STATUS_RETRY;
        }
        else
        {
            send_handle.status = SEND_STATUS_RETRY;
        }
    }

    if (send_handle.status == SEND_STATUS_RETRY)
    {
        send_handle.last_status = SEND_STATUS_RETRY;
        send_handle.retry_count++;
        if (send_handle.retry_count > RETRY_MAX_CNT)
        {
            handle_send_retry_overflow();
            return;
        }

        comm_send(send_handle.frame_data);
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
        return;
    }

    if (send_handle.status != SEND_STATUS_IDLE)
    {
        return;
    }

    {
        FrameData next_frame;
        TX_SOURCE next_source = TX_SOURCE_NONE;
        if (!prepare_next_frame(&next_frame, &next_source))
        {
            return;
        }

        send_handle.frame_data = next_frame;
        send_handle.source = next_source;
        send_handle.last_status = SEND_STATUS_FRAME;
        send_handle.retry_count = 0;
        send_handle.receive_ack = 0;
        send_handle.receive_nack = 0;

        comm_send(send_handle.frame_data);
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
    }
}
