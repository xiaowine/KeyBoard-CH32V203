#include "key_map.h"
#include "config.h"
#include "utils.h"

LINK_AND_AL(".config", 1) const ConfigHeader_t CONFIG_HEADER = {
    .bits =
    {
        .boot_layer = 0,
        .ver = 0,
        .normal_mode = 0,
        .reserved = 0,
    },
};

LINK_AND_AL(".config", 4) const Key_Map_t CONFIG_KEYMAP[CONFIG_KEYMAP_LAYERS_NUM][TOTAL_KEYS] = {
    /* Layer 0: 默认映射（原有内容） */
    {
        {0x00, {0x01}, KEY_TYPE_MOUSE_BUTTON},
        {0x00, {2}, KEY_TYPE_MOUSE_WHEEL},
        {(1 << 0), {0x04}, KEY_TYPE_MOUSE_BUTTON},
        {0x00, {0x07}, KEY_TYPE_KEYBOARD},
        {0x00, {0x08}, KEY_TYPE_KEYBOARD},
        {0x00, {0x09}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0A}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0B}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0C}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0D}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0E}, KEY_TYPE_KEYBOARD},
        {0x00, {0x0F}, KEY_TYPE_KEYBOARD},
        {0x00, {0x10}, KEY_TYPE_KEYBOARD},
        {0x00, {0x11}, KEY_TYPE_KEYBOARD},
        {0x00, {0x12}, KEY_TYPE_KEYBOARD},
        {0x00, {0x13}, KEY_TYPE_KEYBOARD},
        {0x00, {0x14}, KEY_TYPE_KEYBOARD},
        {0x00, {0x15}, KEY_TYPE_KEYBOARD},
        {0x00, {0x16}, KEY_TYPE_KEYBOARD},
        {0x00, {0x17}, KEY_TYPE_KEYBOARD},
        {0x00, {0x18}, KEY_TYPE_KEYBOARD},
        {0x00, {0x19}, KEY_TYPE_KEYBOARD},
        {0x00, {0x00CD}, KEY_TYPE_CONSUMER},
        {0x00, {0x1A}, KEY_TYPE_KEYBOARD},
    }
};
