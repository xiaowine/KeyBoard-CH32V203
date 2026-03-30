#include "comm_controller.h"

#include <stdlib.h>
#include <string.h>

#include "usb_desc.h"
#include "usb_endp.h"

/**
 * @file comm_controller.c
 * @brief 自定义 HID 通信控制器。
 *
 * 设计要点：
 * 1. 协议以固定 64 字节帧传输，帧尾使用 CRC 校验。
 * 2. 接收采用 START + DATA 分片重组，发送采用 ACK/NACK 驱动重试。
 * 3. 控制帧（ACK/NACK/ERROR）优先级高于业务回复帧。
 * 4. 单会话策略：新会话或控制响应可抢占旧的业务回复会话。
 */

/** @brief 接收状态机主流程。 */
static void comm_recv_process(void);
/** @brief 发送状态机主流程。 */
static void comm_send_process(void);
/** @brief 重置发送状态机到空闲状态。 */
static void reset_send_state(void);
/** @brief 清空业务回复会话并释放缓存。 */
static void clear_reply_session(void);
/** @brief 中止业务回复会话（必要时也会重置发送状态）。 */
static void abort_reply_session(void);
/** @brief 入队一个控制帧（ACK/NACK/ERROR）。 */
static void queue_control_frame(FRAME_TYPE type);
/** @brief 处理发送侧收到 ACK 的推进逻辑。 */
static void handle_send_ack(void);
/** @brief 处理发送侧重试溢出。 */
static void handle_send_retry_overflow(void);
/** @brief 依据优先级准备下一帧待发送数据。 */
static uint8_t prepare_next_frame(FrameData *out_frame, TX_SOURCE *out_source);
/** @brief 将完整业务载荷分发给上层回调。 */
static void dispatch_received_payload(uint8_t payload_type, const uint8_t *payload, uint16_t payload_len);

/** @brief 接收会话上下文。 */
static ReceiveHandle receive_handle = {0};
/** @brief 发送会话上下文。 */
static SendHandle send_handle = {0};
/** @brief 业务回复会话上下文。 */
static ReplySession reply_session = {0};
/** @brief 上层接收回调。 */
static comm_rx_callback_t rx_callback = NULL;

/** @brief 以小端格式解析 16 位无符号整数。 */
static uint16_t parse_u16_le(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/** @brief 以小端格式写入 16 位无符号整数。 */
static void write_u16_le(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFu);
    buf[1] = (uint8_t)(value >> 8);
}

/** @brief 释放接收缓存并重置接收上下文。 */
static void release_receive_payload_buffer(void)
{
    if (receive_handle.payload_buf != NULL)
    {
        free(receive_handle.payload_buf);
    }
    memset(&receive_handle, 0, sizeof(receive_handle));
}

/** @brief 仅释放回复缓存，不改变其他回复上下文字段。 */
static void release_reply_payload_buffer(void)
{
    if (reply_session.payload_buf != NULL)
    {
        free(reply_session.payload_buf);
    }
}

/** @brief 清空回复会话：释放缓存并将会话字段清零。 */
static void clear_reply_session(void)
{
    release_reply_payload_buffer();
    memset(&reply_session, 0, sizeof(reply_session));
}

/** @brief 复位发送状态机到空闲起点。 */
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

/**
 * @brief 中止当前业务回复会话。
 *
 * 如果当前正在发送业务回复帧，则先重置发送状态，确保后续调度不再继续旧会话。
 */
static void abort_reply_session(void)
{
    if (send_handle.source == TX_SOURCE_REPLY_START || send_handle.source == TX_SOURCE_REPLY_DATA)
    {
        reset_send_state();
    }
    clear_reply_session();
}

/** @brief 打印接收完成后的载荷内容（HEX + ASCII，最多展示 128 字节）。 */
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

/** @brief 入队控制帧并触发发送抢占。 */
static void queue_control_frame(const FRAME_TYPE type)
{
    FrameData control_frame = {0};

    /*
     * 单会话策略：
     * 对端触发的 ACK/NACK/ERROR 属于控制帧，必须抢占当前业务发送状态，
     * 并在下一次调度中优先发送。
     */
    reset_send_state();
    control_frame.type = (uint8_t)type;
    control_frame.payload_length = 0u;
    send_handle.frame_data = control_frame;
    send_handle.source = TX_SOURCE_CONTROL;
    send_handle.last_status = SEND_STATUS_FRAME;
    send_handle.status = SEND_STATUS_FRAME;
}

/** @brief 发送一帧数据：补齐 CRC 后通过 USB 端点发出。 */
static void comm_send(FrameData frame_data)
{
    uint32_t words[CRC_WORD_SIZE] = {0};
    memcpy(words, &frame_data, CRC_BYTES_SIZE);
    CRC_ResetDR();
    frame_data.crc = CRC_CalcBlockCRC(words, CRC_WORD_SIZE);
    USBD_SendCustomData((uint8_t *)&frame_data, sizeof(FrameData));
}

/** @brief 注册上层接收回调。 */
void comm_register_rx_callback(comm_rx_callback_t callback)
{
    rx_callback = callback;
}

/**
 * @brief 入队业务回复数据。
 *
 * 规则：
 * 1. 长度超限或空指针参数非法时直接忽略。
 * 2. 若已有待发/在发业务回复，会被新回复覆盖（控制帧优先级独立更高）。
 */
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
     * 队列满策略：覆盖既有业务回复。
     * 控制帧独立于业务回复且优先级更高。
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

/** @brief 将完整业务载荷回调给上层。 */
static void dispatch_received_payload(uint8_t payload_type, const uint8_t *payload, uint16_t payload_len)
{
    if (rx_callback != NULL)
    {
        rx_callback(payload_type, payload, payload_len);
    }
}

/** @brief 通信控制器周期任务入口：先收后发。 */
void comm_controller_process(void)
{
    comm_recv_process();
    comm_send_process();
}

/**
 * @brief 接收状态机。
 *
 * 处理步骤：
 * 1. 从 USB 端点读取一帧并做基础合法性检查。
 * 2. 校验 CRC，失败则回 NACK 并计入重试。
 * 3. 根据帧类型推进接收会话或更新发送响应标志。
 */
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
            /* 序号超出协议范围，直接回 ERROR。 */
            PRINT("comm_recv: frame seq_num %u > SEQ_MAX_NUM\r\n", (unsigned)frame_data.seq_num);
            queue_control_frame(FRAME_TYPE_ERROR);
            return;
        }

        {
            uint32_t words[CRC_WORD_SIZE] = {0};
            const uint32_t frame_crc = frame_data.crc;
            frame_data.crc = 0;
            memcpy(words, &frame_data, CRC_BYTES_SIZE);
            CRC_ResetDR();
            uint32_t calc_crc = CRC_CalcBlockCRC(words, CRC_WORD_SIZE);
            if (frame_crc != calc_crc)
            {
                /* CRC 不匹配：请求对端重发。 */
                PRINT("comm_recv: CRC mismatch, frame_crc=0x%08X calc_crc=0x%08X\r\n", frame_crc, calc_crc);
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
                    /* START 必须从 seq=0 开始。 */
                    if (frame_data.seq_num == 0u)
                    {
                        const uint16_t payload_all_len = parse_u16_le(frame_data.payload.data);
                        if (payload_all_len > FRAME_RECV_MAX_BYTES)
                        {
                            /* 总长度超限，拒绝会话。 */
                            PRINT("comm_recv: START payload_all_len %u > FRAME_RECV_MAX_BYTES\r\n", (unsigned)payload_all_len);
                            queue_control_frame(FRAME_TYPE_ERROR);
                            break;
                        }

                        /* 新接收会话会抢占旧业务回复会话。 */
                        abort_reply_session();
                        /* 清理旧接收缓存，开始新会话。 */
                        release_receive_payload_buffer();

                        if (payload_all_len > 0u)
                        {
                            receive_handle.payload_buf = (uint8_t *)malloc(payload_all_len);
                            if (receive_handle.payload_buf == NULL)
                            {
                                /* 内存不足，通知对端错误。 */
                                PRINT("comm_recv: malloc failed for payload_len=%u\r\n", (unsigned)payload_all_len);
                                queue_control_frame(FRAME_TYPE_ERROR);
                                break;
                            }
                        }

                        receive_handle.payload_type = frame_data.payload.type;
                        receive_handle.expected_payload_len = payload_all_len;
                        receive_handle.received_payload_len = 0;
                        receive_handle.last_seq_num = 0;
                        receive_handle.need_ack = payload_all_len > 0u ? 1u : 0u;

                        /* 先入队 ACK，确保协议确认优先于业务回复。 */
                        queue_control_frame(FRAME_TYPE_ACK);

                        if (payload_all_len == 0u)
                        {
                            /* 零长度请求：立即回调并释放会话。 */
                            dispatch_received_payload(receive_handle.payload_type, NULL, 0);
                            release_receive_payload_buffer();
                        }
                    }
                    else
                    {
                        /* 非法 START 序号，重置接收并回 ERROR。 */
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                    }
                    break;
                }
                case FRAME_TYPE_DATA:
                {
                    const uint16_t frame_payload_len = frame_data.payload_length;
                    uint8_t is_complete = 0u;

                    /* DATA 必须严格按顺序到达且需要存在活动接收会话。 */
                    if (frame_data.seq_num != (uint8_t)(receive_handle.last_seq_num + 1u) ||
                        receive_handle.expected_payload_len == 0u)
                    {
                        PRINT("comm_recv: DATA seq mismatch or no active session; seq=%u last=%u expected_len=%u\r\n",
                              (unsigned)frame_data.seq_num, (unsigned)receive_handle.last_seq_num, (unsigned)receive_handle.expected_payload_len);
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    /* 单帧数据长度必须在 (0, FRAME_PAYLOAD_DATA_SIZE] 范围内。 */
                    if (frame_payload_len == 0u || frame_payload_len > FRAME_PAYLOAD_DATA_SIZE)
                    {
                        PRINT("comm_recv: DATA invalid payload length=%u\r\n", (unsigned)frame_payload_len);
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    /* 业务类型在整个会话内必须一致。 */
                    if (frame_data.payload.type != receive_handle.payload_type)
                    {
                        PRINT("comm_recv: DATA payload type mismatch: got=%u expected=%u\r\n",
                              (unsigned)frame_data.payload.type, (unsigned)receive_handle.payload_type);
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    /* 不能越界写入接收缓存。 */
                    if (frame_payload_len > (uint16_t)(receive_handle.expected_payload_len - receive_handle.received_payload_len))
                    {
                        PRINT("comm_recv: DATA would overflow buffer: frame_len=%u remaining=%u\r\n",
                              (unsigned)frame_payload_len,
                              (unsigned)(receive_handle.expected_payload_len - receive_handle.received_payload_len));
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    /* 有效会话必须已分配缓存。 */
                    if (receive_handle.payload_buf == NULL)
                    {
                        PRINT("comm_recv: receive buffer NULL before DATA copy\r\n");
                        release_receive_payload_buffer();
                        queue_control_frame(FRAME_TYPE_ERROR);
                        break;
                    }

                    /* 复制分片并推进接收进度。 */
                    memcpy(receive_handle.payload_buf + receive_handle.received_payload_len,
                           frame_data.payload.data, frame_payload_len);
                    receive_handle.received_payload_len += frame_payload_len;
                    receive_handle.last_seq_num++;
                    receive_handle.need_ack =
                        receive_handle.received_payload_len < receive_handle.expected_payload_len ? 1u : 0u;
                    is_complete = receive_handle.received_payload_len == receive_handle.expected_payload_len ? 1u : 0u;

                    /* 末片查询请求不回 ACK，直接进入回复 START。 */
                    if (!(is_complete && DATA_TYPE_IS_QUERY(receive_handle.payload_type)))
                    {
                        queue_control_frame(FRAME_TYPE_ACK);
                    }

                    if (is_complete)
                    {
                        /* 会话完成：打印内容、回调上层、释放缓存。 */
                        print_received_payload_content(receive_handle.payload_buf, receive_handle.received_payload_len);
                        dispatch_received_payload(receive_handle.payload_type, receive_handle.payload_buf,
                                                  receive_handle.received_payload_len);
                        release_receive_payload_buffer();
                    }
                    break;
                }
                case FRAME_TYPE_ACK:
                    /* 仅在 WAIT_RESPONSE 且 seq 匹配当前待确认帧时接受 ACK。 */
                    if (send_handle.status == SEND_STATUS_WAIT_RESPONSE &&
                        frame_data.seq_num == send_handle.frame_data.seq_num)
                    {
                        send_handle.receive_ack = 1;
                    }
                    break;
                case FRAME_TYPE_NACK:
                    /* 仅在 WAIT_RESPONSE 且 seq 匹配当前待确认帧时接受 NACK。 */
                    if (send_handle.status == SEND_STATUS_WAIT_RESPONSE &&
                        frame_data.seq_num == send_handle.frame_data.seq_num)
                    {
                        send_handle.receive_nack = 1;
                    }
                    break;
                case FRAME_TYPE_ERROR:
                    /* 对端报告错误时，重置本地接收会话。 */
                    release_receive_payload_buffer();
                    break;
                default:
                    /* 未知类型忽略。 */
                    break;
                }
            }
        }
    }
    else
    {
        /* 本周期未收到完整帧；若仍待 ACK，累计超时重试计数。 */
        if (receive_handle.need_ack)
        {
            receive_handle.retry_count++;
        }
    }

    if (receive_handle.retry_count > RETRY_MAX_CNT)
    {
        /* 接收方向超时重试过多，复位接收状态。 */
        release_receive_payload_buffer();
        PRINT("Receive data retry count exceeded, resetting state.\r\n");
    }
}

/**
 * @brief 装配下一帧待发送数据。
 *
 * 优先级：
 * 1. 控制帧（ACK/NACK/ERROR）
 * 2. 业务回复 START
 * 3. 业务回复 DATA
 */
static uint8_t prepare_next_frame(FrameData *out_frame, TX_SOURCE *out_source)
{
    memset(out_frame, 0, sizeof(*out_frame));
    *out_source = TX_SOURCE_NONE;

    if (!reply_session.active)
    {
        /* 无待发业务回复。 */
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
            /* 理论上不会进入此分支，保险清理。 */
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

    /* 未知阶段，保守不发送。 */
    return 0;
}

/** @brief 处理发送侧 ACK：根据帧来源推进队列/会话。 */
static void handle_send_ack(void)
{
    switch (send_handle.source)
    {
    case TX_SOURCE_CONTROL:
        break;
    case TX_SOURCE_REPLY_START:
        if (reply_session.payload_len == 0u)
        {
            /* 零长度回复到此结束。 */
            clear_reply_session();
        }
        else
        {
            /* START 已确认，进入 DATA 分片阶段。 */
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

    /* 严格节拍：ACK 后仅复位状态，下一帧由下一次 process 调度发送。 */
    reset_send_state();
}

/** @brief 发送方向重试溢出处理：终止回复并回 ERROR。 */
static void handle_send_retry_overflow(void)
{
    if (send_handle.source == TX_SOURCE_CONTROL && send_handle.frame_data.type == FRAME_TYPE_ERROR)
    {
        reset_send_state();
        return;
    }
    if (send_handle.source == TX_SOURCE_REPLY_START || send_handle.source == TX_SOURCE_REPLY_DATA)
    {
        /* 业务回复方向失败时清空会话，避免继续发送陈旧数据。 */
        clear_reply_session();
    }

    PRINT("Send data retry count exceeded, resetting state.\r\n");
    queue_control_frame(FRAME_TYPE_ERROR);
}

/**
 * @brief 发送状态机。
 *
 * 状态推进：
 * - WAIT_RESPONSE：消费 ACK/NACK 或按超时进入 RETRY
 * - RETRY：重发当前帧并计数
 * - IDLE：调度下一帧并发送
 */
static void comm_send_process(void)
{
    if (send_handle.status == SEND_STATUS_FRAME)
    {
        comm_send(send_handle.frame_data);
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
        return;
    }

    if (send_handle.status == SEND_STATUS_WAIT_RESPONSE)
    {
        if (send_handle.receive_ack)
        {
            /* ACK 优先：立即推进并结束本次调度。 */
            send_handle.receive_ack = 0;
            send_handle.receive_nack = 0;
            handle_send_ack();
            return;
        }

        if (send_handle.receive_nack)
        {
            /* 对端明确 NACK，进入重试。 */
            send_handle.receive_nack = 0;
            send_handle.status = SEND_STATUS_RETRY;
        }
        else
        {
            /* 未收到响应，按超时重试策略处理。 */
            send_handle.status = SEND_STATUS_RETRY;
        }
    }

    if (send_handle.status == SEND_STATUS_RETRY)
    {
        send_handle.last_status = SEND_STATUS_RETRY;
        send_handle.retry_count++;
        if (send_handle.retry_count > RETRY_MAX_CNT)
        {
            /* 重试超限。 */
            handle_send_retry_overflow();
            return;
        }

        /* 原帧重发。 */
        comm_send(send_handle.frame_data);
        send_handle.status = SEND_STATUS_WAIT_RESPONSE;
        return;
    }

    if (send_handle.status != SEND_STATUS_IDLE)
    {
        /* 非空闲但也非显式可推进状态时，不做任何动作。 */
        return;
    }

    {
        FrameData next_frame;
        TX_SOURCE next_source = TX_SOURCE_NONE;
        if (!prepare_next_frame(&next_frame, &next_source))
        {
            /* 当前无可发帧。 */
            return;
        }

        /* 锁存本次待发帧，供后续 ACK/NACK/重试处理复用。 */
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
