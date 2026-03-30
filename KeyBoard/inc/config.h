#ifndef KEYBOARD_CONFIG_H
#define KEYBOARD_CONFIG_H

#include "key_map.h"
#include "rgb.h"

typedef union
{
    uint32_t raw;

    struct
    {
        uint32_t boot_layer : 4;
        uint32_t rgb_color_layer : 3;
        uint32_t normal_mode : 1;
        uint32_t reserved : 24;
    } bits;
} ConfigHeader_t;

#define CONFIG_FLASH_PAGE_BYTE 256
#define CONFIG_FLASH_BYTE 2048
#define CONFIG_HEADER_BYTE (sizeof(ConfigHeader_t))
#define CONFIG_RGB_COLOR_NUM 5
#define CONFIG_RGB_COLOR_PATH_NUM 5
#define CONFIG_RGB_COLOR_LAYER_BYTE (sizeof(RGB_Color_t) * CONFIG_RGB_COLOR_PATH_NUM)
#define CONFIG_RGB_COLOR_TOTAL_LAYERS_BYTE (sizeof(RGB_Color_t) * CONFIG_RGB_COLOR_PATH_NUM * CONFIG_RGB_COLOR_NUM)
#define CONFIG_KEYMAP_LAYER_BYTE (sizeof(Key_Map_t) * TOTAL_KEYS)
#define CONFIG_KEYMAP_LAYERS_NUM ((CONFIG_FLASH_BYTE - CONFIG_HEADER_BYTE - CONFIG_RGB_COLOR_TOTAL_LAYERS_BYTE) / (CONFIG_KEYMAP_LAYER_BYTE))
#define CONFIG_KEYMAP_TOTAL_LAYERS_BYTE (CONFIG_KEYMAP_LAYER_BYTE * CONFIG_KEYMAP_LAYERS_NUM)
#endif
