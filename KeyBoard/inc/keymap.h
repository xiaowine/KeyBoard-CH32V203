#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "utils.h"

#define MAX_CODE 3
#define BOOT_KEY_MAX 6
#define KB_HEARTBEAT_INTERVAL 100

/* 支持的 keymap 层数（最多 3 层） */
#define KEYMAP_LAYERS 3

/* 按键类型 —— 使用常量宏定义 */
#define KEY_TYPE_KEYBOARD 0
#define KEY_TYPE_CONSUMER 1
#define KEY_TYPE_MOUSE_BUTTON 2
#define KEY_TYPE_MOUSE_WHEEL 3

typedef struct __attribute__((packed))
{
    uint8_t count;     /* 1 字节 */
    uint8_t modifiers; /* 1 字节 */
    union
    {
        uint8_t kcodes[MAX_CODE];
        uint16_t ccodes[MAX_CODE];
        uint8_t mouse_buttons[MAX_CODE];
        int8_t mouse_wheel;
    } codes;      /* 可变字段：紧跟在 modifiers 之后打包 */
    uint8_t type; /* 1 字节，使用 KEY_TYPE_* 宏 */
} KeyMapping;

extern KeyMapping keymap_active[KEY_TOTAL_KEYS];

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
