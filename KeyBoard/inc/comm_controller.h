#ifndef COMM_CONTROLLER_H
#define COMM_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>
#include "utils.h"

/**
 * @brief 参与 CRC 计算的 32 位字数量。
 *
 * 计算范围为 FrameData 结构体中 crc 字段之前的所有字节，并向上按 4 字节对齐。
 */
#define CRC_WORD_SIZE ((offsetof(FrameData, crc) + 3u) / 4u)

/** @brief 参与 CRC 计算的原始字节数（即 crc 字段之前的长度）。 */
#define CRC_BYTES_SIZE offsetof(FrameData, crc)

/** @brief 单帧可携带的有效载荷字节数（不含 payload.type）。 */
#define FRAME_PAYLOAD_DATA_SIZE (sizeof(((FrameData *)0)->payload.data))

/** @brief 收发流程允许的最大重试次数。 */
#define RETRY_MAX_CNT 10u

/** @brief 接收方向单次会话允许的最大有效载荷长度。 */
#define FRAME_RECV_MAX_BYTES 4096u

/** @brief 发送方向单次会话允许的最大有效载荷长度。 */
#define FRAME_SEND_MAX_BYTES 4096u

/** @brief 协议允许的最大序号值（seq_num）。 */
#define SEQ_MAX_NUM 100u

/** @brief 判断 DATA_TYPE 是否为查询类。 */
#define DATA_TYPE_IS_QUERY(type) ((((uint8_t)(type)) & 0b0001) == 0u)

/** @brief 帧类型定义。 */
typedef enum
{
    /** 协议或状态错误帧。 */
    FRAME_TYPE_ERROR = 0u,
    /** 会话起始帧，携带总长度信息。 */
    FRAME_TYPE_START = 1u,
    /** 数据帧，携带分片数据。 */
    FRAME_TYPE_DATA = 2u,
    /** 确认帧。 */
    FRAME_TYPE_ACK = 3u,
    /** 否认帧，请求对端重发。 */
    FRAME_TYPE_NACK = 4u
} FRAME_TYPE;

/** @brief 业务数据类型定义，由上层回调解释。 */
typedef enum
{
    /** 读取按键快照。 */
    DATA_TYPE_GET_KEY = 0b0000,
    /** 读取当前层按键映射。 */
    DATA_TYPE_GET_LAYER_KEYMAP = 0b0010,
    /** 读取全部层按键映射。 */
    DATA_TYPE_GET_ALL_LAYER_KEYMAP = 0b0100,
    /** 设置当前层（预留）。 */
    DATA_TYPE_SET_LAYER = 0b0001,
    /** 设置层映射（预留）。 */
    DATA_TYPE_SET_LAYER_KEYMAP = 0b0011,
} DATA_TYPE;

/** @brief 发送状态机状态定义。 */
typedef enum
{
    /** 空闲，可调度下一帧。 */
    SEND_STATUS_IDLE = 0u,
    /** 已发送，等待对端响应。 */
    SEND_STATUS_WAIT_RESPONSE = 1u,
    /** 预留状态：刚装配帧。 */
    SEND_STATUS_FRAME = 2u,
    /** 触发重试。 */
    SEND_STATUS_RETRY = 3u,
} SEND_STATUS;

/**
 * @brief 协议帧结构（固定 64 字节）。
 *
 * 字段布局：
 * - 头部：seq_num/type/payload_length
 * - payload：业务类型 + 数据
 * - crc：覆盖 crc 前所有字节
 */
typedef struct PACKED
{
    /** 帧序号。START 固定为 0，DATA 从 1 递增。 */
    uint8_t seq_num;
    /** 帧类型，取值见 FRAME_TYPE。 */
    uint8_t type;
    /** payload.data 的有效长度。 */
    uint8_t payload_length;

    struct
    {
        /** 业务类型，取值见 DATA_TYPE。 */
        uint8_t type;
        /** 实际数据区。 */
        uint8_t data[56];
    } payload;

    /** CRC 校验值。 */
    uint32_t crc;
} FrameData;

/** @brief 当前发送帧的来源，用于 ACK 后推进不同流程。 */
typedef enum
{
    /** 无来源。 */
    TX_SOURCE_NONE = 0u,
    /** 控制帧队列（ACK/NACK/ERROR）。 */
    TX_SOURCE_CONTROL = 1u,
    /** 业务回复会话的 START 帧。 */
    TX_SOURCE_REPLY_START = 2u,
    /** 业务回复会话的 DATA 帧。 */
    TX_SOURCE_REPLY_DATA = 3u
} TX_SOURCE;

/** @brief 业务回复会话阶段。 */
typedef enum
{
    /** 空闲阶段。 */
    REPLY_PHASE_IDLE = 0u,
    /** 待发送 START。 */
    REPLY_PHASE_SEND_START = 1u,
    /** 待发送 DATA 分片。 */
    REPLY_PHASE_SEND_DATA = 2u
} REPLY_PHASE;

/** @brief 接收方向上下文。 */
typedef struct
{
    /** 当前会话业务类型。 */
    uint8_t payload_type;
    /** 最近一次成功接收的 seq_num。 */
    uint8_t last_seq_num;
    /** 接收方向重试计数。 */
    uint8_t retry_count;
    /** 标记是否需要回 ACK。 */
    uint8_t need_ack;
    /** 当前会话声明的总长度。 */
    uint16_t expected_payload_len;
    /** 已累计接收的字节数。 */
    uint16_t received_payload_len;
    /** 接收缓存指针，长度为 expected_payload_len。 */
    uint8_t *payload_buf;
} ReceiveHandle;

/** @brief 发送方向上下文。 */
typedef struct
{
    /** 最近一次发送/待重发的帧缓存。 */
    FrameData frame_data;
    /** 当前发送状态。 */
    SEND_STATUS status;
    /** 记录上一次状态，便于调试。 */
    SEND_STATUS last_status;
    /** 发送方向重试计数。 */
    uint8_t retry_count;
    /** 是否收到 ACK。 */
    uint8_t receive_ack;
    /** 是否收到 NACK。 */
    uint8_t receive_nack;
    /** 当前帧来源。 */
    TX_SOURCE source;
} SendHandle;

/** @brief 业务回复会话上下文。 */
typedef struct
{
    /** 1 表示会话有效。 */
    uint8_t active;
    /** 回复业务类型。 */
    uint8_t payload_type;
    /** 下一帧 DATA 序号。 */
    uint8_t next_seq_num;
    /** 当前发送阶段。 */
    REPLY_PHASE phase;
    /** 本次回复总长度。 */
    uint16_t payload_len;
    /** 已被 ACK 的字节数。 */
    uint16_t acked_payload_len;
    /** 回复缓存指针，长度为 payload_len。 */
    uint8_t *payload_buf;
} ReplySession;

/**
 * @brief 接收业务载荷后的回调函数类型。
 *
 * @param payload_type 业务类型。
 * @param payload      业务数据指针；当 payload_len 为 0 时可能为 NULL。
 * @param payload_len  业务数据长度。
 */
typedef void (*comm_rx_callback_t)(uint8_t payload_type, const uint8_t *payload, uint16_t payload_len);

/** @brief 驱动通信控制器一次收发调度（建议周期调用）。 */
void comm_controller_process(void);

/** @brief 注册业务接收回调，后注册会覆盖前注册。 */
void comm_register_rx_callback(comm_rx_callback_t callback);

/**
 * @brief 入队一条业务回复数据。
 *
 * 若已有待发/在发业务回复，本函数会按单会话策略覆盖旧回复。
 */
void comm_queue_reply(uint8_t payload_type, const uint8_t *data, uint16_t len);

#endif
