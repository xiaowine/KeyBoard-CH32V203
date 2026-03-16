#ifndef KEYBOARD_HID_COMM_H
#define KEYBOARD_HID_COMM_H

#include "usb_desc.h"

#define HID_COMM_DATA_SIZE DEF_ENDP_SIZE_CUSTOM // 32 bytes

// 包类型宏定义
#define FRAME_TYPE_SINGLE 0b000 // 单包，适用于长度不超过24字节的情况
#define FRAME_TYPE_START 0b001  // 起始包，表示多包传输的开始
#define FRAME_TYPE_DATA 0b010   // 数据包，表示多包传输中的中间包
#define FRAME_TYPE_END 0b011    // 结束包，表示多包传输的结束
#define FRAME_TYPE_ACK 0b100    // ACK包，表示接收方对某个包的确认
#define FRAME_TYPE_NACK 0b101   // NACK包，表示接收方对某个包的否认
#define FRAME_TYPE_BUSY 0b110   // BUSY包，表示接收方忙碌，无法处理当前包
#define FRAME_TYPE_OF 0b111     // 溢出包，表示发送方的缓冲区溢出或其他错误

// 数据类型宏定义
#define DATA_TYPE_SET_LAYER_KEYMAP 0     // 键位映射数据
#define DATA_TYPE_GET_KEY 1              // 获取键位状态数据
#define DATA_TYPE_GET_LAYER_KEYMAP 2     // 获取层键位映射数据
#define DATA_TYPE_GET_ALL_LAYER_KEYMAP 3 // 获取全部层键位映射数据

/* 重试上限：连续 NACK 超过该值则丢弃当前接收任务 */
#define HID_RETRY_LIMIT 5U
/* 最大动态接收缓冲（24 * 256 = 6144 字节） */
#define HID_RECEIVE_MAX_CAPACITY (sizeof(((HidFrame_t *)0)->payload) * 256U)

// ---------------- Reason Codes ----------------
// 用于 HidComm_EventParam_t.reason，便于统一管理和可读性
#define HID_COMM_REASON_INVALID_LEN 1U           // 数据包长度无效
#define HID_COMM_REASON_CRC_ERROR 2U             // CRC 校验失败
#define HID_COMM_REASON_CAPACITY_EXCEEDED 3U     // 接收缓冲区容量超限
#define HID_COMM_REASON_MALLOC_FAIL 4U           // 动态内存分配失败
#define HID_COMM_REASON_START_HEADER_SHORT 5U    // START 包头长度不足
#define HID_COMM_REASON_INVALID_TOTAL_SIZE 6U    // 总长度字段无效
#define HID_COMM_REASON_INSUFFICIENT_CAPACITY 7U // 应用层缓冲区不足
#define HID_COMM_REASON_RETRY_EXHAUSTED 8U       // 接收侧重试耗尽
#define HID_COMM_REASON_TX_RETRY_EXHAUSTED 10U   // 发送侧重试耗尽

typedef struct PACKED
{
    struct
    {
        uint8_t len : 5;  // Bit 0-4: 有效长度 (0-24)
        uint8_t type : 3; // Bit 5-7: 包类型
    } ctrl;

    uint8_t data_type;   // 业务类型
    uint8_t seq;         // 序列号
    uint8_t reserved;    // 保留字节 (确保后面 4 字节对齐)
    uint8_t payload[24]; // 负载
    uint32_t crc32;      // 硬件 CRC32
} HidFrame_t;

typedef enum
{
    SEND_IDLE = 0, // 空闲：等待新任务
    SEND_START,    // 起始：已发出 START 包，等待 ACK
    SEND_WAIT_ACK, // 等待：已发出，等待接收方回复 ACK
    SEND_RETRY,    // 重发：已发出，等待 ACK 超时，正在重试
    SEND_ERROR     // 错误：重试耗尽，任务失败
} SendState_t;

typedef enum
{
    RECEIVE_IDLE = 0, // 空闲：等待 START 或 SINGLE 包
    RECEIVE_WAIT,     // 接收中：已收到 START，正在拼装 DATA 包
    RECEIVE_COMPLETE, // 完成：已收到 END 包，数据完整
    RECEIVE_ERROR,    // 错误：超时或重试耗尽，丢弃当前数据
} ReceiveState_t;

typedef struct
{
    const uint8_t *p_buf; // 待发送数据指针 (4字节对齐)
    uint16_t total_len;   // 任务总长度 (0~6144 字节)
    uint16_t sent_len;    // 已发送累计长度
    SendState_t state;    // 当前发送状态机状态
    uint8_t data_type;    // 业务负载类型
    uint8_t curr_seq;     // 当前帧序列号 (0-255)
    volatile struct
    {
        uint8_t ack_flag : 1;  // Bit 0: 接收标志
        uint8_t retry_cnt : 7; // Bit 1-7: 重试计数 (0-127)
    } status;
    uint8_t timeout_ticks; // 发送等待 ACK 的超时计数（以 hid_comm_process 调用次数为单位）
} SendHandle_t;

typedef struct
{
    uint8_t *p_buf;       // 接收缓冲区指针 (通常由应用层传入)
    uint16_t capacity;    // 缓冲区总容量
    uint16_t total_len;   // 预期总长度 (从 START 包的 len 字段获取)
    uint16_t recved_len;  // 当前已累计接收的长度
    ReceiveState_t state; // 当前接收状态机
    uint8_t data_type;    // 当前传输的任务类型
    uint8_t last_seq;     // 上一次成功接收的序列号 (用于去重)
    uint8_t retry_cnt;    // 接收侧重试计数（连续 NACK 次数）
} ReceiveHandle_t;

// ---------------- Callback API ----------------
typedef enum
{
    HID_COMM_EVT_RX_COMPLETE = 0, // 接收完成：参数包含 data pointer + len + data_type
    HID_COMM_EVT_RX_ERROR,        // 接收出错：参数包含可能的 data_type/seq/reason
    HID_COMM_EVT_TX_COMPLETE,     // 发送完成：参数包含 data_type/len
    HID_COMM_EVT_TX_ERROR,        // 发送错误：参数包含 reason
    HID_COMM_EVT_TX_ABORT         // 发送被远端中止（BUSY/OF）
} HidComm_Event_t;

typedef struct
{
    uint8_t data_type; // 业务类型
    uint8_t *p;        // 指向接收数据（仅对 RX_COMPLETE 有效）
    uint16_t len;      // 长度（RX_COMPLETE/TX_COMPLETE）
    uint8_t seq;       // 相关序号（有时有效）
    uint8_t reason;    // 错误原因码（如果有）
} HidComm_EventParam_t;

typedef void (*hid_comm_callback_t)(HidComm_Event_t evt, const HidComm_EventParam_t *param);

// 注册回调，传入 NULL 将取消注册
void hid_comm_register_callback(hid_comm_callback_t cb);

void hid_comm_process(void);
// 非阻塞启动一次发送任务（0=成功，1=失败 e.g. busy）

uint8_t hid_comm_start_send(const uint8_t *data, uint16_t len, uint8_t data_type);
// 库会在接收到 START/SINGLE 时按需分配接收缓冲，应用无需注册缓冲区。

#endif
