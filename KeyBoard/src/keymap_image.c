#include "keymap.h"
#include "keymap_loader.h"

/* 控制从 FLASH 加载哪些层,放在 .keymap 段开头，运行时 loader 会读取此掩码决定加载哪些层到 RAM。 */
/* 紧凑布局：首字节为单字节层索引（image header），后跟按 4 字节对齐的层数据 */
/* 值为 0 表示加载 layer0 */
__attribute__((section(".keymap"), used, aligned(1))) const uint8_t KEYMAP_LOAD_MASK = 0;

__attribute__((section(".keymap"), used, aligned(4))) const KeyMapping KEY_MAP[KEYMAP_LAYERS][KEY_TOTAL_KEYS] = {
    /* Layer 0: 默认映射（原有内容） */
    {
        {1, 0x00, {0x01}, KEY_TYPE_MOUSE_BUTTON}, // 物理(3,4) -> 左键
        {1, 0x00, {1}, KEY_TYPE_MOUSE_WHEEL},     // 物理(2,5) -> 滚轮上
        {1, (1 << 0), {0x06}, KEY_TYPE_KEYBOARD}, // 物理(滚轮) -> CTRL+C
        {1, 0x00, {0x07}, KEY_TYPE_KEYBOARD},     // 物理(3,5) -> D
        {1, 0x00, {0x08}, KEY_TYPE_KEYBOARD},     // 物理(4,5) -> E
        {1, 0x00, {0x09}, KEY_TYPE_KEYBOARD},     // 物理(5,4) -> F
        {1, 0x00, {0x0A}, KEY_TYPE_KEYBOARD},     // 物理(5,3) -> G
        {1, 0x00, {0x0B}, KEY_TYPE_KEYBOARD},     // 物理(4,4) -> H
        {1, 0x00, {0x0C}, KEY_TYPE_KEYBOARD},     // 物理(4,3) -> I
        {1, 0x00, {0x0D}, KEY_TYPE_KEYBOARD},     // 物理(3,1) -> J
        {1, 0x00, {0x0E}, KEY_TYPE_KEYBOARD},     // 物理(3,2) -> K
        {1, 0x00, {0x0F}, KEY_TYPE_KEYBOARD},     // 物理(3,3) -> L
        {1, 0x00, {0x10}, KEY_TYPE_KEYBOARD},     // 物理(4,1) -> M
        {1, 0x00, {0x11}, KEY_TYPE_KEYBOARD},     // 物理(5,1) -> N
        {1, 0x00, {0x12}, KEY_TYPE_KEYBOARD},     // 物理(5,2) -> O
        {1, 0x00, {0x13}, KEY_TYPE_KEYBOARD},     // 物理(4,2) -> P
        {1, 0x00, {0x14}, KEY_TYPE_KEYBOARD},     // 物理(1,1) -> Q
        {1, 0x00, {0x15}, KEY_TYPE_KEYBOARD},     // 物理(1,2) -> R
        {1, 0x00, {0x16}, KEY_TYPE_KEYBOARD},     // 物理(1,3) -> S
        {1, 0x00, {0x17}, KEY_TYPE_KEYBOARD},     // 物理(空) -> T
        {1, 0x00, {0x18}, KEY_TYPE_KEYBOARD},     // 物理(2,4) -> U
        {1, 0x00, {0x19}, KEY_TYPE_KEYBOARD},     // 物理(2,3) -> V
        {1, 0x00, {0x00CD}, KEY_TYPE_CONSUMER},   // 物理(2,1) -> 播放/暂停
        {1, 0x00, {0x1A}, KEY_TYPE_KEYBOARD},     // 物理(2,2) -> W
    }};
