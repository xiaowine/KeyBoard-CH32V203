#include "keymap.h"
#include "keymap_loader.h"

/* 控制从 FLASH 加载哪些层,放在 .keymap 段开头，运行时 loader 会读取此掩码决定加载哪些层到 RAM。 */
/* 紧凑布局：首字节为单字节层索引（image header），后跟按 4 字节对齐的层数据 */
/* 值为 0 表示加载 layer0 */
__attribute__((section(".keymap"), used, aligned(1))) const uint8_t KEYMAP_LOAD_MASK = 0;

__attribute__((section(".keymap"), used, aligned(4))) const KeyMapping KEY_MAP[KEYMAP_LAYERS][KEY_TOTAL_KEYS] = {
    /* Layer 0: 默认映射（原有内容） */
    {
        {1, 0x00, {.mouse_buttons = 0x01}, KEY_TYPE_MOUSE_BUTTON}, // Key 0 -> Mouse Left (button bit0)
        {1, 0x00, {.mouse_wheel = 1}, KEY_TYPE_MOUSE_WHEEL},       // Key 1 -> Mouse Wheel Up
        {1, 0x01, {.kcodes = {0x06}}, KEY_TYPE_KEYBOARD},          // Key 2 -> 'C' (Ctrl+C)
        {1, 0x00, {.kcodes = {0x07}}, KEY_TYPE_KEYBOARD},          // Key 3 -> 'D'
        {1, 0x00, {.kcodes = {0x08}}, KEY_TYPE_KEYBOARD},          // Key 4 -> 'E'
        {1, 0x00, {.kcodes = {0x09}}, KEY_TYPE_KEYBOARD},          // Key 5 -> 'F'
        {1, 0x00, {.kcodes = {0x0A}}, KEY_TYPE_KEYBOARD},          // Key 6 -> 'G'
        {1, 0x00, {.kcodes = {0x0B}}, KEY_TYPE_KEYBOARD},          // Key 7 -> 'H'
        {1, 0x00, {.kcodes = {0x0C}}, KEY_TYPE_KEYBOARD},          // Key 8 -> 'I'
        {1, 0x00, {.kcodes = {0x0D}}, KEY_TYPE_KEYBOARD},          // Key 9 -> 'J'
        {1, 0x00, {.kcodes = {0x0E}}, KEY_TYPE_KEYBOARD},          // Key 10 -> 'K'
        {1, 0x00, {.kcodes = {0x0F}}, KEY_TYPE_KEYBOARD},          // Key 11 -> 'L'
        {1, 0x00, {.kcodes = {0x10}}, KEY_TYPE_KEYBOARD},          // Key 12 -> 'M'
        {1, 0x00, {.kcodes = {0x11}}, KEY_TYPE_KEYBOARD},          // Key 13 -> 'N'
        {1, 0x00, {.kcodes = {0x12}}, KEY_TYPE_KEYBOARD},          // Key 14 -> 'O'
        {1, 0x00, {.kcodes = {0x13}}, KEY_TYPE_KEYBOARD},          // Key 15 -> 'P'
        {1, 0x00, {.kcodes = {0x14}}, KEY_TYPE_KEYBOARD},          // Key 16 -> 'Q'
        {1, 0x00, {.kcodes = {0x15}}, KEY_TYPE_KEYBOARD},          // Key 17 -> 'R'
        {1, 0x00, {.kcodes = {0x16}}, KEY_TYPE_KEYBOARD},          // Key 18 -> 'S'
        {1, 0x00, {.kcodes = {0x17}}, KEY_TYPE_KEYBOARD},          // Key 19 -> 'T'
        {1, 0x00, {.kcodes = {0x18}}, KEY_TYPE_KEYBOARD},          // Key 20 -> 'U'
        {1, 0x00, {.kcodes = {0x19}}, KEY_TYPE_KEYBOARD},          // Key 21 -> 'V'
        {1, 0x00, {.ccodes = {0x00CD}}, KEY_TYPE_CONSUMER},        // Key 22 -> Play/Pause
        {1, 0x00, {.kcodes = {0x1A}}, KEY_TYPE_KEYBOARD},          // Key 23 -> 'W'
    }};
