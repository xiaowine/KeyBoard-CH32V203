# comm_controller 通信控制器说明

## 1. 模块职责

`comm_controller` 是键盘固件中自定义 HID 通道的收发控制模块，负责：

- 解析并校验固定 64 字节协议帧（含 CRC）。
- 将 `START + DATA` 分片重组为完整业务载荷并回调上层。
- 管理发送状态机，处理 ACK/NACK/超时重试。
- 按优先级调度控制帧（ACK/NACK/ERROR）和业务回复帧。
- 实现单会话策略，避免并发会话冲突。

对应源码：

- `KeyBoard/inc/comm_controller.h`
- `KeyBoard/src/comm_controller.c`

## 2. 对外接口

### `void comm_controller_process(void);`

- 周期调度入口，建议在主循环固定周期调用（当前工程在 `app_run()` 的 5ms 节拍调用）。
- 内部顺序固定为“先接收处理，再发送处理”。

### `void comm_register_rx_callback(comm_rx_callback_t callback);`

- 注册上层业务回调函数。
- 回调签名：
  `void (*comm_rx_callback_t)(uint8_t payload_type, const uint8_t* payload, uint16_t payload_len);`

### `void comm_queue_reply(uint8_t payload_type, const uint8_t* data, uint16_t len);`

- 入队一条业务回复。
- 当已有待发/在发业务回复时，新回复会覆盖旧回复（单会话策略）。
- `len > FRAME_SEND_MAX_BYTES` 或 `len > 0 && data == NULL` 时直接忽略。

## 3. 协议帧结构

单帧结构体为 `FrameData`（固定 64 字节）：

- `seq_num`：序号。`START` 固定 0，`DATA` 从 1 递增。
- `type`：帧类型。
- `payload_length`：`payload.data` 的有效长度。
- `payload.type`：业务类型。
- `payload.data[56]`：数据区。
- `crc`：对 `crc` 字段之前所有字节计算得到。

帧类型（`FRAME_TYPE`）：

- `ERROR(0)`：协议/状态错误。
- `START(1)`：会话开始帧，`payload.data[0..1]` 放总长度（小端）。
- `DATA(2)`：数据分片帧。
- `ACK(3)`：确认帧。
- `NACK(4)`：否认帧，请求重发。

## 4. 接收流程（RX）

`comm_recv_process()` 的核心流程：

1. 从 USB 端点读取 64 字节帧。
2. 校验 `seq_num` 合法性与 CRC。
3. 按帧类型处理：
   - `START`：
     - 仅接受 `seq_num == 0`。
     - 读取总长度并校验上限。
     - 新会话会抢占并中止旧业务回复会话。
     - 初始化接收上下文，入队 `ACK`。
     - 若总长度为 0，立即回调上层。
   - `DATA`：
     - 校验序号连续性、长度合法性、payload_type 一致性、缓存边界。
     - 追加到接收缓存，入队 `ACK`。
     - 收齐后回调上层并释放缓存。
   - `ACK/NACK`：
     - 仅置位发送状态机消费标志。
   - `ERROR`：
     - 释放当前接收会话缓存。
4. 若接收方向重试计数超过 `RETRY_MAX_CNT`，复位接收状态。

## 5. 发送流程（TX）

`comm_send_process()` 是发送状态机，状态定义见 `SEND_STATUS`：

- `IDLE`：空闲，可调度下一帧。
- `WAIT_RESPONSE`：已发送，等待 ACK/NACK。
- `RETRY`：重发当前帧并累计计数。

`prepare_next_frame()` 的调度优先级：

1. 控制帧队列（ACK/NACK/ERROR）
2. 业务回复 `START`
3. 业务回复 `DATA`

处理规则：

- 收到 `ACK`：按当前 `TX_SOURCE` 推进状态（清控制队列或推进回复会话）。
- 收到 `NACK` 或超时：进入重试。
- 重试超过 `RETRY_MAX_CNT`：
  - 若是业务回复帧，清空回复会话。
  - 入队 `ERROR` 控制帧。

## 6. 单会话与抢占策略

- 控制帧优先级高于业务回复。
- 新接收会话（`START`）到来时，会抢占并中止旧的业务回复会话。
- 新调用 `comm_queue_reply()` 时，会覆盖旧的待发/在发业务回复。
- 该策略保证协议在资源受限 MCU 上始终保持单会话一致性。

## 7. 上层接入关系

在当前工程中：

- `app_init()` 中调用 `comm_register_rx_callback(app_comm_rx_callback)` 注册回调。
- `app_run()` 中每 5ms 调用 `comm_controller_process()`。
- `app_comm_rx_callback()` 根据 `payload_type` 处理请求并通过 `comm_queue_reply()` 回包。

## 8. 调试与测试建议

- 串口日志可观察重试溢出和接收完成打印。
- 主机侧可使用 `scripts/hid_comm_test.py` 覆盖以下场景：
  - 正常分片收发
  - CRC 错误后的 NACK/重发恢复
  - 会话抢占与旧数据续传拒绝
  - `DATA_TYPE_GET_KEY/GET_LAYER_KEYMAP/GET_ALL_LAYER_KEYMAP` 请求-应答流程

---

如需扩展协议，建议优先保持以下不变量：

- 帧长固定 64 字节。
- `START` 描述总长度，`DATA` 严格按序号递增。
- 控制帧优先于业务帧发送。
- 任何异常都能回落到可恢复状态（释放缓存 + 重置状态机）。

## 9. ACK/NACK 匹配规则（2026-03-18）

为降低旧帧响应串扰，发送侧新增以下约束：

- 仅当发送状态为 `SEND_STATUS_WAIT_RESPONSE` 时，才接受对端 `ACK/NACK`。
- 仅当 `ACK/NACK` 的 `seq_num` 与当前在途发送帧 `send_handle.frame_data.seq_num` 一致时，才视为有效响应。
- 不满足上述条件的 `ACK/NACK` 视为过期或无关响应，直接忽略，不推进发送状态机。

实现位置：`KeyBoard/src/comm_controller.c` 中 `comm_recv_process()` 的 `FRAME_TYPE_ACK/FRAME_TYPE_NACK` 分支。
