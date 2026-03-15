# HID 双向可靠传输协议规范 (V1.1)

## 1. 协议概述
本协议专为固定 32 字节报文长度的 HID 设备设计，旨在提供高可靠性的双向流传输。
- **核心目标**：分包传输、丢包重发、硬件校验、非阻塞执行。
- **硬件优化**：采用 **4 字节手动对齐** 布局，确保 32 位 MCU 访问 `Payload` 和 `CRC32` 时无需非对齐处理，支持 DMA 和硬件 CRC 加速。

---

## 2. 帧结构定义 (Frame Structure)
整帧固定为 **32 字节**。通过 `Reserved` 字节强制对齐。

| 偏移 (Byte) | 字段名 | 长度 | 类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| 0 | **Ctrl_Len** | 1 | uint8_t | 高 3 位：包类型；低 5 位：有效长度 (0-24) |
| 1 | **DataType** | 1 | uint8_t | 业务负载类型（由应用层定义） |
| 2 | **Seq_Num** | 1 | uint8_t | 序列号 (0-255 循环) |
| 3 | **Reserved** | 1 | uint8_t | **保留字节**：用于 4 字节对齐补位，默认 0x00 |
| 4-27 | **Payload** | 24 | uint8_t[24] | **4 字节对齐起始** 的实际载荷数据 |
| 28-31 | **CRC32** | 4 | uint32_t | 硬件 CRC32 校验值 (覆盖 Offset 0-27) |

---

## 3. 字段详解

### 3.1 Ctrl_Len (控制与长度)
- **Packet Type (Bit 7:5)**:
  - `000` (SINGLE): 单包任务 (数据量 $\le$ 24 字节)。
  - `001` (START): 长数据起始包。
  - `010` (DATA): 长数据中间包。
  - `011` (END): 长数据结束包。
  - `100` (ACK): 确认包 (用于状态机推进)。
  - `101` (NACK): 错误通知 (请求重发)。
- **Data Length (Bit 4:0)**: 
  - Payload 字段中有效字节数 ($0 \sim 24$)。

### 3.2 Seq_Num (序列号)
- 物理传输能力：$256 \text{ pkts} \times 24 \text{ bytes} = 6,144 \text{ Bytes}$ (6 KB)。
- 若超过 6KB，应用层需处理序列号回绕 (Rollover)。

---

## 4. 传输逻辑 (1ms 非阻塞状态机)

### 4.1 发送方 (TX)
1. **PREPARE**: 组包并计算硬件 CRC32，通过 HID 发出。
2. **WAIT_ACK**: 启动计时器。若收到匹配 `Seq_Num` 的 ACK 则进入下一包；若超时（建议 50ms）则递增 `retry_cnt` 并重传。
3. **IDLE**: 任务完成或彻底失败。

### 4.2 接收方 (RX)
1. **校验**: 硬件 CRC32 校验失败则丢弃，不予回复。
2. **去重**: 若收到的 `Seq_Num` 等于 `last_seq`，说明 ACK 丢失，**必须重发 ACK** 但丢弃 Payload 数据。
3. **超时**: 若 `START` 后超过 500ms 未收到后续，强制复位至 IDLE。

---

## 5. C 语言实现

### 5.1 协议帧结构
```c
#include <stdint.h>

#pragma pack(push, 1)
/**
 * @brief HID 传输协议帧 (32-byte)
 * 手动对齐以优化 32 位 MCU 性能
 */
typedef struct {
    uint8_t ctrl_len;    // Bit 7-5: Type, Bit 4-0: Len
    uint8_t data_type;   // 业务类型
    uint8_t seq;         // 序列号
    uint8_t reserved;    // 保留补位 (确保后续4字节对齐)
    uint8_t payload[24]; // 负载起始偏移 = 4
    uint32_t crc32;      // CRC32起始偏移 = 28
} HidFrame_t;
#pragma pack(pop)