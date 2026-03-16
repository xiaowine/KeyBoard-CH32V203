# HID 双向可靠传输（代码解读）

## 目标
本报告逐步解读项目中基于 HID 的可靠双向传输实现（协议 + 代码），目标读者是未接触过此项目的新开发者，阅读后能快速理解协议细节、代码结构与运行流程。

## 相关文件
- 协议规范： [docs/protocol.md](docs/protocol.md)
- 接口与类型： [KeyBoard/inc/hid_comm.h](KeyBoard/inc/hid_comm.h)
- 实现核心： [KeyBoard/src/hid_comm.c](KeyBoard/src/hid_comm.c)

## 协议概览（精简）
- 帧固定 32 字节（HID 报文固定长度）。
- 帧字段：
  - Offset 0: `Ctrl+Len`（1B，bit7-5 = Type，bit4-0 = Len，Len 最大 24）
  - Offset 1: `data_type`（1B）
  - Offset 2: `seq`（1B，0~255 循环）
  - Offset 3: `reserved`（1B，4 字节对齐）
  - Offset 4-27: `payload`（24B）
  - Offset 28-31: `crc32`（4B，覆盖偏移 0~27）
- 包类型包括：`SINGLE, START, DATA, END, ACK, NACK, BUSY, OF`。

设计要点：利用 4 字节对齐优化硬件 CRC/DMA，支持单包与多包重组，最多 256 帧 × 24B = 6144B 重组空间。

## 关键数据结构（来自头文件）
- `HidFrame_t`：协议帧结构（32B）。
- `TXHandle_t`/`RXHandle_t`：TX/RX 状态句柄，分别记录发送任务与接收重组状态。
- 全局句柄：`hSendHid`（发送），`hReceiveHid`（接收）。

## 全局缓冲与硬件依赖
- `rx_buffer[32]`：一帧临时接收缓冲。
- `rx_task_buffer[24 * 256]`：重组缓冲（6144B），默认绑定到 `hReceiveHid.p_buf`。
- CRC 由硬件 `CRC_CalcBlockCRC` 计算（需要硬件 CRC 外设）。

## 使用入口（API）
- `uint8_t hid_comm_start_send(const uint8_t *data, uint16_t len)`：启动一次非阻塞发送任务，底层调用 `USBD_SendCustomData`。
- `void hid_comm_process(void)`：主处理函数，需在主循环或定时器中周期调用以处理收到的帧并驱动状态机。

## 核心函数逐步解读：`hid_comm_process()`

以下按执行顺序逐步说明代码逻辑，便于没有上下文的人理解每一个决策点。

1) 懒初始化 RX 句柄
- 首次调用将 `hReceiveHid.p_buf` 指向 `rx_task_buffer`，并设定 `capacity = 6144`、`last_seq = 0xFF`、`state = RX_IDLE`。
- 目的：保证接收端有默认重组缓冲，方便上层直接取数据。

2) 读取一帧
- 调用 `USBD_GetCustomData(rx_buffer, sizeof(rx_buffer))`，期望返回 32。
- 若无数据或长度异常（非 32），直接返回（非阻塞行为）。

3) 长度与 CRC 基本检查
- 解析 `frame_len = frame->ctrl.len`（0~24）并检查越界。
- 用 `hid_comm_check_crc()` 校验 CRC（使用硬件 CRC，覆盖前 28 字节）。
- CRC 错误或长度非法时，发送 `NACK` 给对端并返回。

4) 控制帧的优先处理（ACK / NACK）
  若收到 `ACK`/`NACK`：检查是否为当前发送任务相关（seq 匹配 `hSendHid.curr_seq`）并修改 `hSendHid`：
  - `ACK` -> 标记 `ack_flag = 1`（发送方可推进到下一帧或结束）。
  - `NACK` -> `ack_flag = 0`，增加重试计数并把 `state` 置为 `TX_RETRY`。
- 处理完控制帧后返回，不进入数据处理分支。

5) 远端 BUSY / OF
- 若收到 `BUSY` 或 `OF` 且与当前 TX seq 匹配，则调用 `hid_comm_abort_send()` 放弃本次发送任务（清除发送句柄）。

6) 去重逻辑（防止重复写入）
- 场景：处于 `RX_WAIT`（多包传输中），对端重发上一个 DATA/END（seq == last_seq），这通常是因为 ACK 丢失。
- 实现：此时不重复写入 payload，只重新发送 `ACK` 给对端，避免数据重复拼接。

7) 各帧类型处理
  `FRAME_TYPE_SINGLE`：单帧任务，payload 直接拷贝到 `hReceiveHid.p_buf`，设置 `total_len=recved_len=frame_len`，`state=RX_COMPLETE`，发送 `ACK`。
- `FRAME_TYPE_START`：多帧开始，payload 前 4 字节为 `total_size`（小端）。验证 `total_size` 在 `(0, capacity]` 范围后进入 `RX_WAIT` 并 `ACK`。
- `FRAME_TYPE_DATA` / `FRAME_TYPE_END`：严格顺序到达（期望 `seq = last_seq + 1` 且 `data_type` 匹配）。
  - 若 seq/type/长度不匹配：发 `NACK` 并调用 `hid_comm_on_rx_retry_fail()`；在超过 `HID_RX_RETRY_LIMIT`（5）后放弃任务。
  - 复制 payload 到 `p_buf+recved_len`，更新 `recved_len`、`last_seq`。
  - `END` 包要求累计长度恰好等于 `total_len`，满足则 `state=RX_COMPLETE` 并 `ACK`。
- 其它未定义类型：发送 `NACK`。

8) 完成态
  当 `hReceiveHid.state == RX_COMPLETE`，数据已完整重组并保存在 `hReceiveHid.p_buf`，上层应读取并处理该缓冲区中的数据。

## 发送（TX）逻辑说明（推断与缺失项）
- 文件中已有 `hSendHid` 结构与若干辅助函数（如 `hid_comm_abort_send()`），但完整的发送状态机实现（例如如何生成 START/DATA/END、何处调用 `hid_comm_start_send()`、超时计时器逻辑）未在 `hid_comm.c` 片段中展示。
- 由 `hid_comm_process()` 可见：发送方依赖收到的控制帧（ACK/NACK/BUSY/OF）来推进或重试，因此需要额外实现：
  - 一个发送调度器：把 `p_buf` 分片为 24B 帧，填充 `HidFrame_t` 并调用 `hid_comm_start_send()` 或构造帧并调用库的发送接口。
  - 超时与重试机制：当等待 ACK 超时（建议 50ms）时递增 `retry_cnt` 并重发；当 `retry_cnt` 达到 `HID_RETRY_LIMIT` 时，发送端会先将状态置为 `SEND_ERROR`（使错误态可被日志或监控观察到），随后在下一次 `hid_comm_process_send()` 调用中由 `SEND_ERROR` 分支调用 `hid_comm_abort_send()` 执行清理并恢复到 `SEND_IDLE`，从而完成任务中止。

## 使用示例（调用要点）
- 在主循环或定时器中频繁调用：

```c
// 主循环示例
for (;;) {
  hid_comm_process(); // 处理收到的帧与控制帧
  // 其它任务...
}
```

- 发送数据（伪代码示例，需实现发送调度器）：

```c
// 1. 准备 hSendHid.p_buf / total_len / data_type 等
// 2. 触发发送任务（发送 START，然后分 DATA，最后 END）
// 3. hid_comm_process() 会处理 ACK/NACK，以推进或重试
```

## 常见问题与建议
- 定时器：协议在文档中建议 ACK 超时 ~50ms、START 后超时 ~500ms，但代码中未见计时器，需在平台上实现以避免挂死在 `RX_WAIT` 或等待 ACK。
- CRC 依赖硬件：若移植到无硬件 CRC 平台，需要改用软件 CRC32 实现。
  并发与线程安全：`hid_comm_process()` 假定单线程调用；若在中断/任务并发环境下使用，需对 `hSendHid`/`hReceiveHid` 做适当的同步保护。
- 重组容量：`rx_task_buffer` 为 6KB，若上层需要更大数据，需在应用层实现分段或流控策略并考虑序列号回绕。

## 下一步建议
- 若需要我可以：
  - 在仓库中补全发送（TX）状态机示例代码与超时机制；
  - 添加使用示例与单元测试模拟（离线 CRC 校验与帧解析测试）；
  - 把本文件注册到 README 链接中以便新开发者快速定位。

----
作者注：本解读依据 `docs/protocol.md`、`KeyBoard/inc/hid_comm.h` 与 `KeyBoard/src/hid_comm.c`（片段）编写，若仓库中存在补充实现（如发送调度、计时器或 USB 端点实现），应一并参考以获得完整行为。
