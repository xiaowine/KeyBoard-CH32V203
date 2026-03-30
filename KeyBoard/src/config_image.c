#include "key_map.h"
#include "config.h"
#include "rgb.h"
#include "common.h"

LINK_AND_AL(".config", 4)
const struct PACKED
{
    ConfigHeader_t header;
    RGB_Color_t rgb[CONFIG_RGB_COLOR_NUM][CONFIG_RGB_COLOR_PATH_NUM];
    Key_Map_t keymap[CONFIG_KEYMAP_LAYERS_NUM][TOTAL_KEYS];
} CONFIG_IMAGE = {
    .header =
    {
        .bits =
        {
            .boot_layer = 0,
            .rgb_color_layer = 1,
            .normal_mode = 0,
            .reserved = 0,
        },
    },
    .rgb =
    {
        {
            {.r = 48, .g = 25, .b = 52},
            {.r = 255, .g = 140, .b = 0},
            {.r = 32, .g = 160, .b = 255},
            {.r = 48, .g = 25, .b = 52},
            {.r = 48, .g = 25, .b = 52}
        },
        {
            {.r = 148, .g = 125, .b = 52},
            {.r = 255, .g = 140, .b = 0},
            {.r = 32, .g = 160, .b = 255},
            {.r = 48, .g = 25, .b = 52},
            {.r = 48, .g = 25, .b = 152}
        }
    },
    .keymap = {
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
    }
};
