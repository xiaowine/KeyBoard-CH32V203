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

/* 明确定义的运行时 RAM 数组：startup 会把 FLASH 中的 keymap 镜像拷贝到该数组。 */
extern KeyMapping keymap_ram[KEY_TOTAL_KEYS];

// NOLINTNEXTLINE(*-reserved-identifier)
extern uint8_t _keymap_lma[]; /* 镜像在 FLASH 中的加载地址（LMA） */
// NOLINTNEXTLINE(*-reserved-identifier)
extern uint8_t _keymap_vma[]; /* 镜像在 RAM 中的运行地址（VMA），startup 会把镜像拷贝到此 */
// NOLINTNEXTLINE(*-reserved-identifier)
extern uint8_t _keymap_end[]; /* 镜像在 RAM 中的结束地址（VMA） */

static size_t keymap_size(void)
{
    return (size_t)&_keymap_end - (size_t)&_keymap_vma;
}

void kb_send_snapshot(const uint8_t snapshot[HC165_COUNT]);

#endif
