#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "app.h"
#include "utils.h"

#define MAX_CODE 3
#define MAX_POSSIBLE_CODES (HC165_COUNT * MAX_CODE)
#define KB_GROUPS CEIL_DIV(8 * HC165_COUNT, 6)
#define BOOT_KEY_MAX 6
#define KB_HEARTBEAT_INTERVAL 100

/* 按键类型 */
typedef enum
{
    KEY_TYPE_KEYBOARD,
    KEY_TYPE_CONSUMER,
    KEY_TYPE_MOUSE_BUTTON,
    KEY_TYPE_MOUSE_WHEEL,
} KeyType;

// 每个物理按键对应的映射，最多支持 3 个同时触发的操作码（可调）
typedef struct
{
    uint8_t count;
    uint8_t modifiers; // HID 8-bit modifier mask (standard USB HID modifier bits)
    union
    {
        uint8_t kcodes[MAX_CODE];
        uint16_t ccodes[MAX_CODE];
        uint8_t mouse_buttons[MAX_CODE];
        int8_t mouse_wheel;
    } codes;

    KeyType type; /* KEY_TYPE_KEYBOARD or KEY_TYPE_CONSUMER or KEY_TYPE_MOUSE */
} KeyMapping;

// 标准 USB HID modifier 位定义
#define MOD_LCTRL 0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT 0x04
#define MOD_LGUI 0x08
#define MOD_RCTRL 0x10
#define MOD_RSHIFT 0x20
#define MOD_RALT 0x40
#define MOD_RGUI 0x80

static const KeyMapping KEY_MAP[24] = {
    {1, 0x00, {.mouse_buttons = 0x01}, KEY_TYPE_MOUSE_BUTTON}, // Key 0 -> Mouse Left (button bit0)
    {1, 0x00, {.mouse_wheel = 2}, KEY_TYPE_MOUSE_WHEEL}, // Key 1 -> Mouse Wheel Up
    {1, MOD_LCTRL, {.kcodes = {0x06}}, KEY_TYPE_KEYBOARD}, // Key 2 -> 'C' (Ctrl+C)
    {1, 0x00, {.kcodes = {0x07}}, KEY_TYPE_KEYBOARD}, // Key 3 -> 'D'
    {1, 0x00, {.kcodes = {0x08}}, KEY_TYPE_KEYBOARD}, // Key 4 -> 'E'
    {1, 0x00, {.kcodes = {0x09}}, KEY_TYPE_KEYBOARD}, // Key 5 -> 'F'
    {1, 0x00, {.kcodes = {0x0A}}, KEY_TYPE_KEYBOARD}, // Key 6 -> 'G'
    {1, 0x00, {.kcodes = {0x0B}}, KEY_TYPE_KEYBOARD}, // Key 7 -> 'H'
    {1, 0x00, {.kcodes = {0x0C}}, KEY_TYPE_KEYBOARD}, // Key 8 -> 'I'
    {1, 0x00, {.kcodes = {0x0D}}, KEY_TYPE_KEYBOARD}, // Key 9 -> 'J'
    {1, 0x00, {.kcodes = {0x0E}}, KEY_TYPE_KEYBOARD}, // Key 10 -> 'K'
    {1, 0x00, {.kcodes = {0x0F}}, KEY_TYPE_KEYBOARD}, // Key 11 -> 'L'
    {1, 0x00, {.kcodes = {0x10}}, KEY_TYPE_KEYBOARD}, // Key 12 -> 'M'
    {1, 0x00, {.kcodes = {0x11}}, KEY_TYPE_KEYBOARD}, // Key 13 -> 'N'
    {1, 0x00, {.kcodes = {0x12}}, KEY_TYPE_KEYBOARD}, // Key 14 -> 'O'
    {1, 0x00, {.kcodes = {0x13}}, KEY_TYPE_KEYBOARD}, // Key 15 -> 'P'
    {1, 0x00, {.kcodes = {0x14}}, KEY_TYPE_KEYBOARD}, // Key 16 -> 'Q'
    {1, 0x00, {.kcodes = {0x15}}, KEY_TYPE_KEYBOARD}, // Key 17 -> 'R'
    {1, 0x00, {.kcodes = {0x16}}, KEY_TYPE_KEYBOARD}, // Key 18 -> 'S'
    {1, 0x00, {.kcodes = {0x17}}, KEY_TYPE_KEYBOARD}, // Key 19 -> 'T'
    {1, 0x00, {.kcodes = {0x18}}, KEY_TYPE_KEYBOARD}, // Key 20 -> 'U'
    {1, 0x00, {.kcodes = {0x19}}, KEY_TYPE_KEYBOARD}, // Key 21 -> 'V'
    {1, 0x00, {.ccodes = {0x00CD}}, KEY_TYPE_CONSUMER}, // Key 22 -> Play/Pause
    {1, 0x00, {.kcodes = {0x1A}}, KEY_TYPE_KEYBOARD}, // Key 23 -> 'W'
};

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
