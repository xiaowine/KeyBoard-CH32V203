#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H

#include "key_scan.h"

#define MAX_CODE 3
#define BOOT_KEY_MAX 6
#define KB_HEARTBEAT_INTERVAL 500

/* 按键类型 —— 使用常量宏定义 */
#define KEY_TYPE_KEYBOARD 0
#define KEY_TYPE_CONSUMER 1
#define KEY_TYPE_MOUSE_BUTTON 2
#define KEY_TYPE_MOUSE_WHEEL 3

typedef struct PACKED
{
    uint8_t modifiers;
    uint16_t codes[MAX_CODE];
    uint8_t type;
} Key_Map_t;

extern Key_Map_t config_active[TOTAL_KEYS];

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
