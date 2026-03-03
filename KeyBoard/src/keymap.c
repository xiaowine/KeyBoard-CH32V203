#include "keymap.h"
#include "key.h"
#include "usb_desc.h"
#include <string.h>
#include <stdbool.h>

/* 分组数量（每组最多 6 键），是一个编译期常量 */
#define KB_GROUPS CEIL_DIV(8 * HC165_COUNT, 6)
u8 KB_Data_Pack[KB_GROUPS][DEF_ENDP_SIZE_KB] = {0};

/* HID Usage IDs for letters A-Z (Keyboard a and A -> 0x04..0x1D) */
static const u8 letter_usage[26] = {
    0x04, /* A */
    0x05, /* B */
    0x06, /* C */
    0x07, /* D */
    0x08, /* E */
    0x09, /* F */
    0x0A, /* G */
    0x0B, /* H */
    0x0C, /* I */
    0x0D, /* J */
    0x0E, /* K */
    0x0F, /* L */
    0x10, /* M */
    0x11, /* N */
    0x12, /* O */
    0x13, /* P */
    0x14, /* Q */
    0x15, /* R */
    0x16, /* S */
    0x17, /* T */
    0x18, /* U */
    0x19, /* V */
    0x1A, /* W */
    0x1B, /* X */
    0x1C, /* Y */
    0x1D /* Z */
};

/*
 * 将 24 个按键（HC165_COUNT * 8 位）打包为 4 个键盘报告。
 * 每个报告为 8 字节： [modifiers][reserved][k1..k6]
 * 我们把每个按键简单地映射为 usage id = key_index + 1（非零）。未按下的键使用 0 表示。
 * 如果某一分组内按下的键超过 6 个，多余的按键将被丢弃。
 */
static void pack_keys_to_report(const u8 snapshot[HC165_COUNT], u8 report[8], u8 group_index)
{
    /* 清空报告（modifiers + reserved + 6 个键槽） */
    memset(report, 0, 8);

    /*
     * group_index 用于选择要填充的 6 键窗口：0..3（最多支持 24 个键）
     */
    u8 start_key = group_index * 6;
    u8 filled = 0;

    for (u8 k = 0; k < 6; ++k)
    {
        u8 key_idx = start_key + k;
        if (key_idx >= HC165_COUNT * 8)
            break;

        u8 byte_idx = key_idx >> 3;
        u8 bit_idx = key_idx & 0x07;

        /*
         * 硬件为低电平有效（active-low）：0 表示按下
         */
        bool pressed = (((snapshot[byte_idx] >> bit_idx) & 0x01) == 0);
        if (pressed)
        {
            /* 使用字母映射表（若 key_idx 在 0..25 范围内），否则回退到之前的简单映射 */
            u8 usage = 0;
            usage = letter_usage[key_idx];

            report[2 + filled] = usage;
            filled++;
            if (filled >= 6)
                break;
        }
    }
}

void kb_send_snapshot(const u8 snapshot[HC165_COUNT])
{
    static u8 report[8];

    /*
     * 有 4 个键盘端点（EP1..EP4），每个端点携带一个 6-key 数组
     */
    for (int grp = 0; grp < KB_GROUPS; ++grp)
    {
        pack_keys_to_report(snapshot, report, grp);

        /*
         * 通过对应的端点发送报告（ENDP1 + grp）。每个端点期望收到 DEF_ENDP_SIZE_KB 字节。
         * 这里将 report 零扩展到端点最大包长，底层发送函数负责具体的 USB 分包。
         */
        u8 send_buf[DEF_ENDP_SIZE_KB];
        memset(send_buf, 0, sizeof(send_buf));
        memcpy(send_buf, report, sizeof(report));

        /*
         * ENDP1..ENDP4 映射为数值 1..4，传给 USBD_ENDPx_DataUp
         */
        u8 endp = (u8)(grp + 1);
        /* 阻塞式尝试发送；当前忽略返回错误 */
        USBD_ENDPx_DataUp(endp, send_buf, DEF_ENDP_SIZE_KB);
    }
}
