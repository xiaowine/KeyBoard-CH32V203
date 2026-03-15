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
#define DATA_TYPE_KEY 0    // 键位状态数据
#define DATA_TYPE_KEYMAP 1 // 键位映射数据

/* RX 重试上限：连续 NACK 超过该值则丢弃当前接收任务 */
#define HID_RX_RETRY_LIMIT 5U

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
  TX_IDLE = 0, // 空闲：等待新任务、
  TX_START,    // 起始：已发出 START 包，等待 ACK
  TX_WAIT_ACK, // 等待：已发出，等待接收方回复 ACK
  TX_RETRY,    // 重发：已发出，等待 ACK 超时，正在重试
  TX_ERROR     // 错误：重试耗尽，任务失败
} TXState_t;

typedef enum
{
  RX_IDLE = 0, // 空闲：等待 START 或 SINGLE 包
  RX_WAIT,     // 接收中：已收到 START，正在拼装 DATA 包
  RX_COMPLETE, // 完成：已收到 END 包，数据完整
  RX_ERROR,    // 错误：超时或重试耗尽，丢弃当前数据
} RXState_t;

typedef struct
{
  const uint8_t *p_buf; // 待发送数据指针 (4字节对齐)
  uint16_t total_len;   // 任务总长度 (0~6144 字节)
  uint16_t sent_len;    // 已发送累计长度
  TXState_t state;      // 当前发送状态机状态
  uint8_t data_type;    // 业务负载类型
  uint8_t curr_seq;     // 当前帧序列号 (0-255)
  volatile struct
  {
    uint8_t ack_flag : 1;  // Bit 0: 接收标志
    uint8_t retry_cnt : 7; // Bit 1-7: 重试计数 (0-127)
  } status;
} TXHandle_t;

typedef struct
{
  uint8_t *p_buf;      // 接收缓冲区指针 (通常由应用层传入)
  uint16_t capacity;   // 缓冲区总容量
  uint16_t total_len;  // 预期总长度 (从 START 包的 len 字段获取)
  uint16_t recved_len; // 当前已累计接收的长度
  RXState_t state;     // 当前接收状态机
  uint8_t data_type;   // 当前传输的任务类型
  uint8_t last_seq;    // 上一次成功接收的序列号 (用于去重)
  uint8_t retry_cnt;   // 接收侧重试计数（连续 NACK 次数）
} RXHandle_t;

static TXHandle_t hTxHid; // 全局发送句柄
static RXHandle_t hRxHid; // 全局接收句柄

uint8_t hid_comm_send(const uint8_t *data, uint16_t len);
void hid_comm_process(void);

#endif