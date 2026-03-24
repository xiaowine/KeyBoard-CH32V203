#include "keymap.h"
#include "keymap_loader.h"

/* 控制从 FLASH 加载哪一层,放在 .config 段开头，运行时 loader 会读取此层索引并加载到 RAM。 */
/* 紧凑布局：首字节为单字节层索引（image header），后跟按 4 字节对齐的层数据 */
/* 值为 0 表示加载 layer0 */
/* Header 32-bit layout: [3:0]=boot_layer, [6:4]=ver, [31:7]=reserved */
__attribute__((section(".config"), used, aligned(4))) const KeymapBootConfig KEYMAP_BOOT_CONFIG = {
    .bits =
    {
        .boot_layer = 0,
        .ver = 0,
        .normal_mode = 0,
        .reserved = 0,
    },
};

__attribute__((section(".config"), used, aligned(4))) const KeyMapping KEY_MAP[KEYMAP_LAYERS][KEY_TOTAL_KEYS] = {
    /* Layer 0: 默认映射（原有内容） */
    {
        {0x00, {0x01}, KEY_TYPE_MOUSE_BUTTON}, // 物理(3,4) -> 左键
        {0x00, {2}, KEY_TYPE_MOUSE_WHEEL}, // 物理(2,5) -> 滚轮上
        {(1 << 0), {0x04}, KEY_TYPE_MOUSE_BUTTON}, // 物理(滚轮) -> 中键
        {0x00, {0x07}, KEY_TYPE_KEYBOARD}, // 物理(3,5) -> D
        {0x00, {0x08}, KEY_TYPE_KEYBOARD}, // 物理(4,5) -> E
        {0x00, {0x09}, KEY_TYPE_KEYBOARD}, // 物理(5,4) -> F
        {0x00, {0x0A}, KEY_TYPE_KEYBOARD}, // 物理(5,3) -> G
        {0x00, {0x0B}, KEY_TYPE_KEYBOARD}, // 物理(4,4) -> H
        {0x00, {0x0C}, KEY_TYPE_KEYBOARD}, // 物理(4,3) -> I
        {0x00, {0x0D}, KEY_TYPE_KEYBOARD}, // 物理(3,1) -> J
        {0x00, {0x0E}, KEY_TYPE_KEYBOARD}, // 物理(3,2) -> K
        {0x00, {0x0F}, KEY_TYPE_KEYBOARD}, // 物理(3,3) -> L
        {0x00, {0x10}, KEY_TYPE_KEYBOARD}, // 物理(4,1) -> M
        {0x00, {0x11}, KEY_TYPE_KEYBOARD}, // 物理(5,1) -> N
        {0x00, {0x12}, KEY_TYPE_KEYBOARD}, // 物理(5,2) -> O
        {0x00, {0x13}, KEY_TYPE_KEYBOARD}, // 物理(4,2) -> P
        {0x00, {0x14}, KEY_TYPE_KEYBOARD}, // 物理(1,1) -> Q
        {0x00, {0x15}, KEY_TYPE_KEYBOARD}, // 物理(1,2) -> R
        {0x00, {0x16}, KEY_TYPE_KEYBOARD}, // 物理(1,3) -> S
        {0x00, {0x17}, KEY_TYPE_KEYBOARD}, // 物理(空) -> T
        {0x00, {0x18}, KEY_TYPE_KEYBOARD}, // 物理(2,4) -> U
        {0x00, {0x19}, KEY_TYPE_KEYBOARD}, // 物理(2,3) -> V
        {0x00, {0x00CD}, KEY_TYPE_CONSUMER}, // 物理(2,1) -> 播放/暂停
        {0x00, {0x1A}, KEY_TYPE_KEYBOARD}, // 物理(2,2) -> W
    }
};
