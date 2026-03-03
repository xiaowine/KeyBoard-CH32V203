#ifndef KEYBOARD_KEYMAP_H
#define KEYBOARD_KEYMAP_H
#include "key.h"
#include "app.h"
#include "utils.h"

#define MAX_CODE 3

// 每个物理按键对应的映射，最多支持 3 个同时触发的操作码（可调）
typedef struct
{
    u8 count;
    u8 modifiers; // HID 8-bit modifier mask (standard USB HID modifier bits)
    u8 codes[MAX_CODE];
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

AL4 static const KeyMapping KEY_MAP[24] = {
    [0] = {1, 0x00, {0x04}},      // Key 0 -> 'A'
    [1] = {1, 0x00, {0x05}},      // Key 1 -> 'B'
    [2] = {1, MOD_LCTRL, {0x06}}, // Key 2 -> 'C'
    [3] = {1, 0x00, {0x07}},      // Key 3 -> 'D'
    [4] = {1, 0x00, {0x08}},      // Key 4 -> 'E'
    [5] = {1, 0x00, {0x09}},      // Key 5 -> 'F'
    [6] = {1, 0x00, {0x0A}},      // Key 6 -> 'G'
    [7] = {1, 0x00, {0x0B}},      // Key 7 -> 'H'
    [8] = {1, 0x00, {0x0C}},      // Key 8 -> 'I'
    [9] = {1, 0x00, {0x0D}},      // Key 9 -> 'J'
    [10] = {1, 0x00, {0x0E}},
    [11] = {1, 0x00, {0x0F}},
    [12] = {1, 0x00, {0x10}},
    [13] = {1, 0x00, {0x11}},
    [14] = {1, 0x00, {0x12}},
    [15] = {1, 0x00, {0x13}},
    [16] = {1, 0x00, {0x14}},
    [17] = {1, 0x00, {0x15}},
    [18] = {1, 0x00, {0x16}},
    [19] = {1, 0x00, {0x17}},
    [20] = {1, 0x00, {0x18}},
    [21] = {1, 0x00, {0x19}},
    [22] = {1, 0x00, {0x1A}},
    [23] = {1, 0x00, {0x1B}},
};

void kb_send_snapshot(const u8 snapshot[HC165_COUNT]);

#endif
