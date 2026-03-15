HID 通信实现解读（基于 KeyBoard/src/hid_comm.c）

一、总体思路
- 固定 32 字节帧，硬件 CRC 覆盖头部 + payload（offset 0~27），支持单帧与分帧可靠传输。
- 发送端分片并按序发送，每帧等待 ACK；接收端按序拼装并回 ACK/NACK，支持对端重发与去重。

二、帧与字段（快速回顾）
- Frame (32B): `ctrl_len(1) | data_type(1) | seq(1) | reserved(1) | payload[24] | crc32(4)`
- `ctrl_len`: 高 3 位 = type，低 5 位 = payload 有效长度 (0~24)
- START 包：payload 前 4 字节存 total_size（uint32 little-endian）

三、发送流程（关键点）
1. 应用调用 `hid_comm_start_send(data, len, data_type)` 启动任务。
2. 若 len<=24：构造 SINGLE 并发送，state->SEND_WAIT_ACK，等待 ACK 完成后调用 `hid_comm_abort_send()` 清理。
3. 若 len>24：发送 START（首段为最多 20 字节），sent_len 记录已发送字节，state->SEND_WAIT_ACK；收到 ACK 后由 `hid_comm_process_send()` 继续按 payload 大小分片发送 DATA/END，seq 递增。
4. 超时与重传：若在 SEND_WAIT_ACK 超时，进入 SEND_RETRY，重发对应 curr_seq 帧；重试次数超过 `HID_RETRY_LIMIT` 则中止任务。
5. 收到远端 `NACK` 会触发进入重发；收到 `BUSY`/`OF`（且 seq 匹配）会直接中止。

四、接收流程（关键点）
1. `hid_comm_process_recv()` 周期性读取一帧（32B）并处理。
2. CRC 校验失败 -> NACK 返回。
3. SINGLE: seq 必须为 0；按 len 分配缓冲（malloc），复制 payload，回复 ACK，state->RECEIVE_COMPLETE。
4. START: seq 必须为 0 且 payload len >=4；读取 total_size，分配缓冲（若上次 COMPLETE 未释放则先 free），进入 RECEIVE_WAIT，回复 ACK。
5. DATA/END: 要求 frame->seq == last_seq + 1 并且 data_type 匹配；追加 payload；回复 ACK；END 时若累计长度正好等于 total_size 则 state->RECEIVE_COMPLETE，否则 NACK 并计入重试。
6. 去重：处于 RECEIVE_WAIT 且收到与 last_seq 相同 seq 的 DATA/END（说明 ACK 丢失导致对端重发），只重发 ACK，不写入 payload。
7. 超出容量或错误超过重试上限时，丢弃任务并复位（`hid_comm_reset_receive()`）。

五、内存与资源管理
- 接收缓冲由库按需 `malloc`（在 SINGLE/START 中）；完成或复位时由库 `free`。
- 若应用未及时释放已完成缓冲，下一次 START 到达时代码会先 `free` 旧缓冲以接受新任务（但建议应用主动释放以避免竞态）。

六、CRC 与硬件依赖
- 使用 `CRC_ResetDR()` + `CRC_CalcBlockCRC()` 通过硬件 CRC 计算 7 个 32-bit word（28 字节），与协议定义一致。
- 依赖底层 USB 数据接口 `USBD_GetCustomData` / `USBD_SendCustomData`。

七、日志与调试点
- CRC 错误：日志 "HID RX error: CRC mismatch"
- 长度/seq 错误：相关 NACK 日志
- 重试耗尽："HID receive retry exhausted" 或 "HID TX retry exhausted"

八、对应源码位置
- 实现： [KeyBoard/src/hid_comm.c](KeyBoard/src/hid_comm.c#L1)
- 头文件： [KeyBoard/inc/hid_comm.h](KeyBoard/inc/hid_comm.h#L1)
- 协议说明： [docs/protocol.md](docs/protocol.md#L1)

（以上为面向工程维护的要点解读，便于阅读实现或移植。）
