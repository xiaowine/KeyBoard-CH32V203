#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "utils.h"

#define MAX_CODE 3
#define BOOT_KEY_MAX 6
#define KB_HEARTBEAT_INTERVAL 500

/* 按键类型 —— 使用常量宏定义 */
#define KEY_TYPE_KEYBOARD 0
#define KEY_TYPE_CONSUMER 1
#define KEY_TYPE_MOUSE_BUTTON 2
#define KEY_TYPE_MOUSE_WHEEL 3

typedef struct __attribute__((packed))
{
    /* modifiers: 8-bit bitmask,
       Bit mapping follows standard HID modifier bitmap:
         bit0: Left Ctrl  (0x01)
         bit1: Left Shift (0x02)
         bit2: Left Alt   (0x04)
         bit3: Left GUI   (0x08)
         bit4: Right Ctrl (0x10)
         bit5: Right Shift(0x20)
         bit6: Right Alt  (0x40)
         bit7: Right GUI  (0x80)
    */
    uint8_t modifiers;
    uint16_t codes[MAX_CODE];
    uint8_t type;
} KeyMapping;

extern KeyMapping keymap_active[KEY_TOTAL_KEYS];

static inline uint8_t keymap_get_count(const KeyMapping *m)
{
    for (uint8_t i = 0; i < MAX_CODE; ++i)
    {
        if (m->codes[i] == 0)
            return i;
    }
    return MAX_CODE;
}

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
