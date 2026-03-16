# HID 通信回调接口文档

## 概述
本文件描述在库中新增的单一回调接口（定义见 `KeyBoard/inc/hid_comm.h`），说明注册方法、事件含义、参数所有权与使用示例。回调在 `hid_comm_process()` 的上下文中同步触发，回调应尽量简短并将耗时工作交由应用层异步线程/任务处理。

## API 简要
- `typedef void (*hid_comm_callback_t)(HidComm_Event_t evt, const HidComm_EventParam_t *param);`
- `void hid_comm_register_callback(hid_comm_callback_t cb);` — 注册回调；传 `NULL` 可取消注册。

> 相关声明请参见： [KeyBoard/inc/hid_comm.h](KeyBoard/inc/hid_comm.h)

## 事件说明（`HidComm_Event_t`）
- `HID_COMM_EVT_RX_COMPLETE`
  - 含义：接收完成（单包或拼包完成）。
  - 有效字段：`param->p`（指向接收缓冲区）、`param->len`、`param->data_type`、`param->seq`。
  - 注意：库拥有 `param->p` 的生命周期；应用若需长期保存数据，必须立即复制。

- `HID_COMM_EVT_RX_ERROR`
  - 含义：接收出错（长度非法、CRC 错误、malloc 失败、重试耗尽等）。
  - 有效字段：`param->data_type`、`param->seq`、`param->reason`（详见下方宏）。

- `HID_COMM_EVT_TX_COMPLETE`
  - 含义：发送完成（库确认所有帧已被对端 ACK）。
  - 有效字段：`param->data_type`、`param->len`。

- `HID_COMM_EVT_TX_ERROR`
  - 含义：发送失败（重试耗尽等）。
  - 有效字段：`param->data_type`、`param->len`、`param->reason`。

- `HID_COMM_EVT_TX_ABORT`
  - 含义：远端以 BUSY 或 OF 控制帧中止发送。
  - 有效字段：`param->data_type`、`param->seq`、`param->reason`（等于远端控制帧类型，即 BUSY/OF）。

## `reason` 宏（定义位置：`KeyBoard/inc/hid_comm.h`）
- `HID_COMM_REASON_INVALID_LEN` — 数据长度非法
- `HID_COMM_REASON_CRC_ERROR` — CRC 校验失败
- `HID_COMM_REASON_CAPACITY_EXCEEDED` — 单帧请求超出最大接收容量
- `HID_COMM_REASON_MALLOC_FAIL` — 动态内存分配失败
- `HID_COMM_REASON_START_HEADER_SHORT` — START 包头长度不足
- `HID_COMM_REASON_INVALID_TOTAL_SIZE` — START 中声明的总长度非法
- `HID_COMM_REASON_INSUFFICIENT_CAPACITY` — 现有缓冲区容量不足
- `HID_COMM_REASON_RETRY_EXHAUSTED` — 接收侧连续重试耗尽
- `HID_COMM_REASON_TX_RETRY_EXHAUSTED` — 发送侧重试耗尽

> 你可以在头文件中按需扩展这些宏。

## 内存与生命周期约定
- 对于 `HID_COMM_EVT_RX_COMPLETE`：`param->p` 指向由库分配的接收缓冲区，库将在下一次任务重置或发生错误时释放该缓冲区。
- 应用若需保存数据：在回调内立即 `malloc` 并 `memcpy` 出来，然后在合适时机 `free`。
- 回调在主循环（或调用 `hid_comm_process()` 的上下文）中被调用，回调应避免阻塞行为。

## 示例代码（回调实现与注册）
```c
#include "hid_comm.h"
#include <stdlib.h>
#include <string.h>

static void my_hid_cb(HidComm_Event_t evt, const HidComm_EventParam_t *param)
{
    switch (evt)
    {
    case HID_COMM_EVT_RX_COMPLETE:
        if (param && param->p && param->len > 0)
        {
            uint8_t *copy = malloc(param->len);
            if (copy)
            {
                memcpy(copy, param->p, param->len);
                // 将 copy 放入应用队列/任务处理；处理完成后 free(copy)
            }
        }
        break;

    case HID_COMM_EVT_RX_ERROR:
        // 记录错误，参考 param->reason
        break;

    case HID_COMM_EVT_TX_COMPLETE:
        // 发送成功，可做后续处理
        break;

    case HID_COMM_EVT_TX_ABORT:
    case HID_COMM_EVT_TX_ERROR:
        // 处理发送中止或错误，查看 param->reason
        break;

    default:
        break;
    }
}

// 注册
void app_init(void)
{
    hid_comm_register_callback(my_hid_cb);
}

// 取消注册（如果需要）
void app_deinit(void)
{
    hid_comm_register_callback(NULL);
}
```

## 建议与注意事项
- 回调应尽量短小；将耗时或阻塞的工作交由后台任务处理。
- 若需要在回调内访问外部资源（例如 RTOS 队列），请使用线程安全的接口。
- 如果希望 `param->p` 在回调返回后仍然有效，不要依赖库内存——务必复制。

---
文档已创建为 `docs/hid_callback.md`。如需我把示例生成为独立的源码文件（例如 `KeyBoard/app/hid_cb_example.c`）并加入构建配置，请告诉我。