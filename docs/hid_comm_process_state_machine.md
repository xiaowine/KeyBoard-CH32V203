# hid_comm_process 接收状态机说明

本文档说明 `hid_comm_process` 当前实现的接收状态机逻辑，以及关键设计点和后续接入方式。

## 1. 文件位置
- 实现文件：`KeyBoard/src/hid_comm.c`
- 协议头文件：`KeyBoard/inc/hid_comm.h`

## 2. 本次实现目标
- 按协议处理 `SINGLE / START / DATA / END / ACK / NACK`。
- 对每个数据帧进行 CRC32 校验。
- 支持 ACK 丢失时的重复包去重（重发 ACK，不重复写数据）。
- 在 `START -> DATA... -> END` 流程中做长度和序列保护。
- 通过 `hTxHid.status.ack_flag` 将 `ACK/NACK` 回传给发送状态机。
- 对 `BUSY/OF` 控制帧执行“丢弃当前整条发送任务”的快速失败策略。
- 接收侧支持限次重试：对可恢复错误回 NACK 并保留上下文等待重传。

## 3. 关键辅助函数

### 3.1 `hid_comm_check_crc`
- 对帧前 28 字节（offset 0~27）计算硬件 CRC32。
- 与帧内 `crc32` 比较，相等返回 1，否则返回 0。

### 3.2 `hid_comm_send_ctrl_frame`
- 发送控制帧（ACK/NACK）。
- `ctrl.len = 0`，payload 清零，自动计算并填写 CRC。

### 3.3 `hid_comm_reset_rx`
- 将接收状态机复位到 `RX_IDLE`。
- 清空 `total_len / recved_len / data_type`。

### 3.4 `hid_comm_abort_tx`
- 丢弃当前发送任务并回到 `TX_IDLE`。
- 清空 `p_buf / total_len / sent_len / data_type / curr_seq / ack_flag / retry_cnt`。

### 3.5 `hid_comm_on_rx_retry_fail`
- 记录 RX 可恢复错误次数（如 seq/type/长度不匹配）。
- 当连续失败达到 `HID_RX_RETRY_LIMIT`（当前为 5）时，丢弃当前 RX 任务并复位到 `RX_IDLE`。
- 重试计数保存在 `RXHandle_t.retry_cnt`，而非文件静态变量。

## 4. 状态机主流程（`hid_comm_process`）

### 4.1 初始化
- 首次调用时，懒初始化 `hRxHid`：
  - `p_buf = rx_task_buffer`
  - `capacity = 6144`
  - `last_seq = 0xFF`
  - `state = RX_IDLE`

### 4.2 收包入口
- 从 `USBD_GetCustomData` 取一帧。
- 仅当长度为固定 32 字节时继续处理。

### 4.3 通用预检查
1. `frame->ctrl.len` 必须 `<= 24`，否则 NACK。
2. CRC 校验失败回 NACK，通知对端执行重发。

### 4.4 控制帧处理
- `ACK`：若 seq 匹配当前发送序列，置 `hTxHid.status.ack_flag = 1`。
- `NACK`：若 seq 匹配当前发送序列，置 `ack_flag = 0`，`retry_cnt++`，并切换到 `TX_RETRY`（请求重发当前帧）。
- `BUSY/OF`：若 seq 匹配当前发送序列，直接丢弃当前整个发送任务（清空 TX 上下文并回到 `TX_IDLE`）。

### 4.5 数据帧去重
- 条件：`state == RX_WAIT` 且收到 `DATA/END` 的 `seq == last_seq`。
- 动作：仅回 ACK，不重复写入 payload。

### 4.6 分帧处理

#### A. `FRAME_TYPE_SINGLE`
- 单包完整任务。
- 检查长度不超过 `capacity`。
- 拷贝 payload，设置：
  - `total_len = recved_len = frame_len`
  - `data_type = frame->data_type`
  - `last_seq = frame->seq`
  - `state = RX_COMPLETE`
- 回 ACK。

#### B. `FRAME_TYPE_START`
- 约定 payload 前 4 字节是总长度 `total_size`。
- 要求：`frame_len >= 4` 且 `0 < total_size <= capacity`。
- 初始化接收上下文：
  - `total_len = total_size`
  - `recved_len = 0`
  - `data_type = frame->data_type`
  - `last_seq = frame->seq`
  - `state = RX_WAIT`
- 回 ACK。

#### C. `FRAME_TYPE_DATA / FRAME_TYPE_END`
- 先检查顺序与上下文：
  - 必须 `state == RX_WAIT`
  - 必须 `seq == last_seq + 1`
  - 必须 `data_type` 一致
- 再检查长度：`recved_len + frame_len <= total_len`。
- 若检查失败：回 NACK，保持 `RX_WAIT`，等待对端重发；失败计数 +1。
- 若失败计数达到上限（5）：放弃当前接收任务并复位。
- 通过后拷贝 payload，更新 `recved_len` 和 `last_seq`，并清零失败计数。
- 若是 `END`：
  - 必须满足 `recved_len + frame_len == total_len`，否则回 NACK 等待重传该 END 帧。
  - 满足时置 `RX_COMPLETE` 并 ACK。
- 若是 `DATA`：直接 ACK。

#### D. 其他未知类型
- 统一 NACK。

## 5. RX_COMPLETE 后的数据位置
- 完整任务数据保存在 `hRxHid.p_buf`。
- 当前默认指向 `rx_task_buffer`。
- 长度在 `hRxHid.total_len`，业务类型在 `hRxHid.data_type`。
- 接收侧重试次数在 `hRxHid.retry_cnt`。

## 6. 当前实现边界与注意点
1. 还未实现“START 后 500ms 超时复位”（协议文档建议项）。
2. 当前 `START` 使用 payload 前 4 字节作为总长度。
3. `total_len` 是 `uint16_t`，与最大 6144 字节兼容。
4. `ACK/NACK` 帧仅用于状态推进，不携带业务 payload。
5. `BUSY/OF` 被视为不可继续信号：收到后立即放弃当前 TX 任务（不等待超时）。
6. RX 的重试是“接收侧发 NACK + 保留上下文等待对端重发”，不是本端主动重发数据。

## 7. 建议的后续对接
1. 新增上层接口，例如：
   - `uint8_t hid_comm_rx_available(void);`
   - `uint16_t hid_comm_rx_read(uint8_t *out, uint16_t max_len, uint8_t *data_type);`
2. 上层在读取完成后调用复位函数，进入下一次接收。
3. 若需要严格可靠链路，补上 500ms 超时逻辑和重发计数统计。

## 8. 与 USB 发送路径相关的一处修正
- `USBD_SendCustomData` 已改为发送固定 32 字节、无 Report ID 的原始报文。
- 该行为与当前 custom HID 描述符（32-byte IN/OUT report）一致。
