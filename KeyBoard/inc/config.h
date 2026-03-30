#ifndef KEYBOARD_CONFIG_H
#define KEYBOARD_CONFIG_H

#include "keymap.h"
#include "rgb.h"

typedef union
{
    uint32_t raw;

    struct
    {
        uint32_t boot_layer : 4;
        uint32_t rgb_color_layer : 3;
        uint32_t open_rgb_led : 1;
        uint32_t normal_mode : 1;
        uint32_t reserved : 23;
    } bits;
} ConfigHeader_t;


#define CONFIG_FLASH_PAGE_BYTE 256 // 页大小
#define CONFIG_FLASH_BYTE 2048 // 配置区大小

#define CONFIG_HEADER_BYTE (sizeof(ConfigHeader_t)) // 配置头占用字节数

#define CONFIG_RGB_COLOR_NUM 5 // RGB 层数数量
#define CONFIG_RGB_COLOR_PATH_NUM 5 // 每层的 RGB 颜色配置数量（即路径数量）
#define CONFIG_RGB_COLOR_LAYER_BYTE (sizeof(RGB_Color_t) * CONFIG_RGB_COLOR_PATH_NUM) // 每层 RGB 颜色配置占用字节数
#define CONFIG_RGB_COLOR_TOTAL_LAYERS_BYTE (sizeof(RGB_Color_t) * CONFIG_RGB_COLOR_PATH_NUM * CONFIG_RGB_COLOR_NUM) // 所有 RGB 层的颜色配置占用字节数



#define CONFIG_KEYMAP_LAYER_BYTE (sizeof(KeyMap_t) * TOTAL_KEYS) // 每层按键映射占用字节数
#define CONFIG_KEYMAP_LAYERS_NUM ((CONFIG_FLASH_BYTE - CONFIG_HEADER_BYTE - CONFIG_RGB_COLOR_TOTAL_LAYERS_BYTE) / (CONFIG_KEYMAP_LAYER_BYTE)) // 按键映射层数数量，根据剩余空间计算



#define CONFIG_KEYMAP_TOTAL_LAYERS_BYTE (CONFIG_KEYMAP_LAYER_BYTE * CONFIG_KEYMAP_LAYERS_NUM) // 所有按键映射层占用字节数
#endif
