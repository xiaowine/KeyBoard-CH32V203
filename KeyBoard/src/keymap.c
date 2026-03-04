#include "keymap.h"
#include "key.h"
#include "usb_desc.h"
#include "usb_endp.h"
#include "utils.h"

#define KB_GROUPS CEIL_DIV(8 * HC165_COUNT, 6)

/*
 * keymap.c - 键盘按键映射与 HID 报告发送模块
 *
 * 概要：
 * 将来自 HC165 串行并行转换器的原始扫描数据转换为标准 USB HID 键盘报告并发送。
 *
 * 处理流程：
 *  1) 将 HC165 返回的字节合并为 24 位按键位掩码（按下位为 1）。
 *  2) 根据 KEY_MAP 将被按下的物理键索引映射为 HID Usage ID（每键可映射多个码）。
 *  3) 将所有要上报的码按每组最多 6 键进行分组，构造 HID 报告并通过对应端点发送。
 *
 * 要点：
 *  - 报告格式为 DEF_ENDP_SIZE_KB 字节： [modifiers, reserved, k1..k6]
 *  - 使用变化检测（prev_keys）避免重复发送；使用心跳（KB_HEARTBEAT_INTERVAL）定期重发以防同步丢失。
 *  - 按需发送：仅发送包含实际按键的分组，USBD_ENDPx_DataUp 调用完成后底层负责清理状态。
 *
 */

// 记录上一次合并的按键位掩码，用于检测扫描间的变化
static uint32_t prev_keys = 0;

// 可选心跳：每 N 次扫描重发一次完整快照（即使无变化），用于从主机/USB 丢包或重新枚举等边界情况恢复
#define KB_HEARTBEAT_INTERVAL 500
static uint16_t kb_heartbeat_counter = 0;

/**
 * 核心发送逻辑：扫描 -> 收集 -> 分组 -> 发送
 */

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT])
{
    // 1. 合并 24 位按键并取反（0变1表示按下）
    // 注意：根据你的 HC165 接线顺序，可能需要调整位移顺序
    const uint32_t raw = (uint32_t)snapshot[0] |
                         (uint32_t)snapshot[1] << 8 |
                         (uint32_t)snapshot[2] << 16;
    // 有些硬件或读取情况下，空闲时返回 0x000000（所有位为 0），
    // 直接取反会导致变成全 1（误判为所有按键被按下）。
    // 若原始值为全 0，则认为没有按下任何键；否则按原逻辑取反。
    const uint32_t keys = (raw == 0x000000) ? 0 : (~raw & 0x00FFFFFF);

    // 2. 变化检测：如果与上次位掩码相同，跳过处理以节省 CPU/USB
    // 保存原始 keys 到 local 变量，后续位扫描会修改临时变量
    uint32_t changed = keys ^ prev_keys;
    if (changed == 0)
    {
        // 如果启用了心跳且达到间隔，则强制重发一次完整报告；否则直接返回
        kb_heartbeat_counter++;
        if (kb_heartbeat_counter < KB_HEARTBEAT_INTERVAL)
        {
            return; // 无变化且未到心跳周期，跳过后续收集与发送
        }
        // 心跳触发：重置计数并继续执行（将重新发送与上次相同的报告）
        kb_heartbeat_counter = 0;
    }
    else
    {
        // 有实际变化，清零心跳计数并继续处理
        kb_heartbeat_counter = 0;
    }

    // 3. 收集所有按下的 HID Usage ID（区分键盘与媒体）
    static uint8_t kb_codes[MAX_POSSIBLE_CODES] = {0};
    static uint16_t consumer_usages[MAX_POSSIBLE_CODES] = {0};
    uint8_t kb_total = 0;
    uint8_t consumer_total = 0;
    uint8_t modifier_bits = 0; // 合并所有按下键的修饰位（仅键盘有效）

    // 使用临时变量进行位扫描，避免破坏原始 keys（用于最终更新 prev_keys）
    uint32_t scan = keys;
    while (scan)
    {
        const uint32_t idx = get_bit_index(scan);
        if (idx < (8 * HC165_COUNT))
        {
            const KeyMapping *m = &KEY_MAP[idx];
            if (m->type == KEY_TYPE_CONSUMER)
            {
                for (uint8_t i = 0; i < m->count; i++)
                {
                    if (consumer_total < MAX_POSSIBLE_CODES)
                    {
                        consumer_usages[consumer_total++] = m->codes.ccodes[i];
                    }
                }
            }
            else
            {
                // 累积修饰位（若该物理键带修饰）
                modifier_bits |= m->modifiers;
                for (uint8_t i = 0; i < m->count; i++)
                {
                    if (kb_total < MAX_POSSIBLE_CODES)
                    {
                        kb_codes[kb_total++] = m->codes.kcodes[i];
                    }
                }
            }
        }
        scan &= scan - 1; // 清除最低位的 1
    }

    /* 将上面收集到的 codes 分别发送出去；分别使用键盘与媒体发送接口 */
    /* 保留心跳行为：始终发送键盘快照以维持 host 同步 */
    USBD_SendKeyboardReports(modifier_bits, kb_codes, kb_total);
    /* 发送媒体报告（若没有媒体按键也会发送空报告以便释放之前的状态） */
    USBD_SendConsumerReport(consumer_usages, consumer_total);

    // 更新 prev_keys 为本次快照的真实键位（注意上面位扫描使用了副本 scan）
    prev_keys = keys;
}
