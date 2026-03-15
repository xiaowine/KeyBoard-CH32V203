HID 通信状态机说明（发送端与接收端）

1. 发送端状态机（SendState_t）

状态枚举（见 `hid_comm.h`）
- `SEND_IDLE`：空闲，等待新发送任务。
- `SEND_START`：（代码未显式长期使用）表示已发 START 并等待确认（实现使用 `SEND_WAIT_ACK`）。
- `SEND_WAIT_ACK`：已发送帧，等待 ACK/NACK/超时。
- `SEND_RETRY`：发生超时或收到 NACK，正在执行重发逻辑。
- `SEND_ERROR`：重试耗尽或不可恢复错误（实现通过调用 `hid_comm_abort_send` 回到 `SEND_IDLE`）。

主要事件
- StartSend（应用调用 `hid_comm_start_send`）
  - 条件：`SEND_IDLE`
  - 动作：设置 `p_buf/total_len/sent_len`；发送 SINGLE 或 START；state->`SEND_WAIT_ACK`。

- RxACK（收到 ACK 且 seq==curr_seq）
  - 条件：`SEND_WAIT_ACK`
  - 动作：如果 SINGLE 或已发送完数据 -> `hid_comm_abort_send()` 返回 `SEND_IDLE`；否则构造下一 DATA/END（seq++）并发送，state 保持 `SEND_WAIT_ACK`。

- RxNACK（收到 NACK 且 seq==curr_seq）
  - 条件：`SEND_WAIT_ACK`
  - 动作：把 `status.retry_cnt++`（上限 0x7F 限制），state->`SEND_RETRY`。

- Timeout（在 `SEND_WAIT_ACK` 超时计数达到阈值）
  - 动作：若 `status.retry_cnt` 超过 `HID_RETRY_LIMIT` -> 打印并 `hid_comm_abort_send()`；否则 `status.retry_cnt++` 并 state->`SEND_RETRY`。

- RetrySend（处于 `SEND_RETRY`）
  - 条件：任何需要重发的帧（curr_seq）
  - 动作：如果 `curr_seq==0` 且 total_len>payload -> 重发 START；否则按 curr_seq 计算 offset 并重发对应 DATA/END；发送后 state->`SEND_WAIT_ACK`。

- RemoteAbort（收到 BUSY/OF 且 seq==curr_seq）
  - 动作：`hid_comm_abort_send()` 丢弃当前任务。

边界与不变量
- `curr_seq` 指示最后发送的帧序号；当发送下一帧时会 `curr_seq++`。
- `sent_len` 累计已放入传输链路的数据字节；发送后会更新该值。


2. 接收端状态机（ReceiveState_t）

状态枚举（见 `hid_comm.h`）
- `RECEIVE_IDLE`：空闲，等待 SINGLE 或 START。
- `RECEIVE_WAIT`：已收到 START，正在等待并拼装 DATA/END。
- `RECEIVE_COMPLETE`：已成功接收完整消息，等待上层取走并释放缓冲。
- `RECEIVE_ERROR`：发生不可恢复错误或重试耗尽，已复位至空闲（实现调用 `hid_comm_reset_receive`）。

主要事件与转移

- RxSINGLE
  - 条件：从 `RECEIVE_IDLE` 或任意（实现会先分配缓冲）
  - 验证：frame->seq 必须为 0；frame_len<=HID_RECEIVE_MAX_CAPACITY
  - 动作：若无缓冲则 `malloc(frame_len)`；复制 payload；state->`RECEIVE_COMPLETE`；回复 ACK。

- RxSTART
  - 条件：`RECEIVE_IDLE` 或 若上次为 `RECEIVE_COMPLETE` 则先释放旧缓冲
  - 验证：frame->seq==0 且 frame_len>=4
  - 动作：解析 total_size；若 total_size 合法且缓冲足够则分配/复用缓冲；设置 `total_len/recved_len=0/data_type/last_seq=0`；state->`RECEIVE_WAIT`；回复 ACK。

- RxDATA / RxEND
  - 条件：`RECEIVE_WAIT`
  - 验证：frame->seq == last_seq + 1 且 frame->data_type == hReceiveHid.data_type
  - 动作：检查不会溢出 `total_len`；将 payload 追加到 `p_buf`；更新 `recved_len` 与 `last_seq`；回复 ACK；若为 END 且 recved_len == total_len -> state->`RECEIVE_COMPLETE`。

- RxDuplicate（对端重发已确认帧）
  - 条件：`RECEIVE_WAIT` 且 frame->seq == last_seq 且 frame_type 为 DATA/END
  - 动作：仅重发 ACK（避免重复写入），保持 `RECEIVE_WAIT`。

- RxError（seq/类型/长度不匹配或容量超限）
  - 动作：回复 NACK 并调用 `hid_comm_on_receive_retry_fail` 增加 `retry_cnt`；若超过 `HID_RETRY_LIMIT` -> `hid_comm_reset_receive()` 并 state->`RECEIVE_ERROR`（随即回到 IDLE）。

- 上层读取行为
  - 说明：当 state==`RECEIVE_COMPLETE` 时，数据留在 `hReceiveHid.p_buf` 里，需上层读取并释放（或让库在下一次 START 时释放）。

超时与鲁棒性
- 接收侧对某些异常（如 seq 不一致）采取 NACK 并等待重传；连续 NACK 超过 `HID_RETRY_LIMIT` 则放弃当前任务。
- 去重机制避免 ACK 丢失导致的数据重复写入；重复包仅触发 ACK 重发。

3. 状态机时序建议（调用频率）
- `hid_comm_process()` 应在主循环中定期调用（例如 ~10ms 周期或更短），以驱动发送超时计数与接收处理。

4. 简要 UML 式转换（文本）

发送端（简化）:
SEND_IDLE --(StartSend)--> SEND_WAIT_ACK
SEND_WAIT_ACK --(RxACK)--> SEND_WAIT_ACK (next frame) / SEND_IDLE (完成)
SEND_WAIT_ACK --(Timeout/NACK)--> SEND_RETRY
SEND_RETRY --(RetrySend)--> SEND_WAIT_ACK
SEND_WAIT_ACK --(RetryExhaust)--> SEND_IDLE (abort)
SEND_WAIT_ACK --(RemoteAbort BUSY/OF)--> SEND_IDLE (abort)

接收端（简化）:
RECEIVE_IDLE --(RxSINGLE)--> RECEIVE_COMPLETE
RECEIVE_IDLE --(RxSTART)--> RECEIVE_WAIT
RECEIVE_WAIT --(RxDATA)--> RECEIVE_WAIT
RECEIVE_WAIT --(RxEND && complete)--> RECEIVE_COMPLETE
RECEIVE_WAIT --(seq/type/len error)--> (NACK->retry_cnt++) -> if retry_cnt>=limit -> RECEIVE_ERROR -> RECEIVE_IDLE
RECEIVE_WAIT --(duplicate seq)--> (resend ACK)

5. 代码对应函数与位置
- 发送驱动： `hid_comm_start_send()` / `hid_comm_process_send()`（KeyBoard/src/hid_comm.c）
- 接收驱动： `hid_comm_process_recv()`（KeyBoard/src/hid_comm.c）
- 主入口： `hid_comm_process()`（先驱动发送，再驱动接收）

（此文档便于开发者理解状态转换、定位触发点并做单元测试或模拟。）
