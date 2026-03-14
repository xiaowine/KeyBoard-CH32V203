#include "keymap.h"
#include "key.h"
#include "usb_endp.h"
#include "utils.h"
#include <string.h>
#include <stdbool.h>
#include "keymap_loader.h"

// 记录上一次合并的按键位掩码，用于检测扫描间的变化
static uint32_t prev_keys = 0;
static uint16_t kb_heartbeat_counter = 0;

/* 运行时单层缓冲区：loader 将把选中的 layer 拷贝到此处 */
KeyMapping keymap_active[KEY_TOTAL_KEYS] __attribute__((section(".data"), used, aligned(4)));

/**
 * 核心发送逻辑：扫描 -> 收集 -> 分组 -> 发送
 */
void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT])
{
    (void)0;
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
    const uint32_t changed = keys ^ prev_keys;
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
    /* 传统 boot 报告只需最多 6 个键；用小数组并去重以节省空间与避免重复 */
    static uint8_t kb_codes[BOOT_KEY_MAX] = {0};
    /* NKRO 位图（120 bits -> 15 bytes）在扫描时直接构建 */
    uint8_t nkro_bitmap[15] = {0};
    static uint8_t prev_nkro[15] = {0};
    /* consumer usages 限定为最多 MAX_CODE（描述符支持 3 个） */
    static uint16_t consumer_usages[MAX_CODE] = {0};
    uint8_t kb_total = 0;
    uint8_t consumer_total = 0;
    uint8_t modifier_bits = 0;      // 合并所有按下键的修饰位（仅键盘有效）
    uint8_t mouse_buttons_mask = 0; /* bits 0..4 */
    int16_t mouse_wheel_sum = 0;
    // 使用临时变量进行位扫描，避免破坏原始 keys（用于最终更新 prev_keys）
    const KeyMapping *km = keymap_active;
    uint32_t scan = keys;
    while (scan)
    {
        const uint32_t idx = get_bit_index(scan);
        if (idx < (8 * HC165_COUNT))
        {
            const KeyMapping *m = &km[idx];
            if (m->count == 0 && m->modifiers == 0 && m->type == KEY_TYPE_KEYBOARD)
            {
                /* 无效映射，跳过 */
                scan &= scan - 1;
                continue;
            }
            if (m->type == KEY_TYPE_CONSUMER)
            {
                for (uint8_t i = 0; i < m->count; i++)
                {
                    /* 只收集最多 MAX_CODE（通常为 3）个 consumer usages */
                    if (consumer_total < MAX_CODE)
                    {
                        consumer_usages[consumer_total++] = m->codes[i];
                    }
                }
            }
            else if (m->type == KEY_TYPE_MOUSE_BUTTON)
            {
                /* 鼠标按钮映射：累积按钮掩码（每个映射可包含多个按钮掩码） */
                for (uint8_t i = 0; i < m->count; i++)
                {
                    mouse_buttons_mask |= (m->codes[i] & 0x1F);
                }
            }
            else if (m->type == KEY_TYPE_MOUSE_WHEEL)
            {
                /* 鼠标滚轮映射：累积滚轮增量（codes[0] 存储滚轮增量，按需转为有符号） */
                mouse_wheel_sum += (int8_t)(m->codes[0] & 0xFF);
            }
            else
            {
                // 累积修饰位（若该物理键带修饰）
                modifier_bits |= m->modifiers;
                for (uint8_t i = 0; i < m->count; i++)
                {
                    uint8_t code = (uint8_t)(m->codes[i] & 0xFF);
                    /* 在收集阶段限制为 BOOT_KEY_MAX 并去重 */
                    if (kb_total < BOOT_KEY_MAX)
                    {
                        bool found = false;
                        for (uint8_t j = 0; j < kb_total; j++)
                        {
                            if (kb_codes[j] == code)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            kb_codes[kb_total++] = code;
                        }
                    }
                    /* 直接构建 NKRO 位图（仅处理 0..119） */
                    if (code < 120)
                    {
                        const uint8_t byte_idx = code / 8;
                        const uint8_t bit = (uint8_t)(1u << (code & 7));
                        nkro_bitmap[byte_idx] |= bit;
                    }
                }
            }
        }
        scan &= scan - 1; // 清除最低位的 1
    }

    /* 将上面收集到的 codes 分别发送出去；分别使用键盘与媒体发送接口 */
    /* 保留心跳行为：仅在有变更或心跳触发时发送 */
    // if (kb_total > 0 || modifier_bits != 0 || kb_heartbeat_counter == 0)
    // {
    //     USBD_SendKeyboardReports(modifier_bits, kb_codes, kb_total);
    // }

    /* 仅在 NKRO 位图与上次不同或心跳时发送 */
    if (memcmp(nkro_bitmap, prev_nkro, sizeof(nkro_bitmap)) != 0 || kb_heartbeat_counter == 0)
    {
        USBD_SendNKROBitmap(nkro_bitmap);
        memcpy(prev_nkro, nkro_bitmap, sizeof(nkro_bitmap));
    }

    if (consumer_total > 0 || kb_heartbeat_counter == 0)
    {
        /* 发送媒体报告（若没有媒体按键也会发送空报告以便释放之前的状态） */

        USBD_SendConsumerReport(consumer_usages, consumer_total);
    }
    if (mouse_buttons_mask != 0 || mouse_wheel_sum != 0 || kb_heartbeat_counter == 0)
    {
        /* 发送鼠标报告（若有鼠标按键或滚轮动作） */
        USBD_SendMouseReport(mouse_buttons_mask, mouse_wheel_sum);
    }

    // 更新 prev_keys 为本次快照的真实键位（注意上面位扫描使用了副本 scan）
    prev_keys = keys;
}
