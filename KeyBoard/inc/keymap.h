#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "utils.h"

#define MAX_CODE 3
#define BOOT_KEY_MAX 6
#define KB_HEARTBEAT_INTERVAL 100

/* 按键类型 —— 使用常量宏定义 */
#define KEY_TYPE_KEYBOARD 0
#define KEY_TYPE_CONSUMER 1
#define KEY_TYPE_MOUSE_BUTTON 2
#define KEY_TYPE_MOUSE_WHEEL 3

typedef struct __attribute__((packed))
{
    uint8_t count;
    uint8_t modifiers;
    uint16_t codes[MAX_CODE];
    uint8_t type;
} KeyMapping;

extern KeyMapping keymap_active[KEY_TOTAL_KEYS];

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
