#ifndef KEYBOARD_CONFIG_H
#define KEYBOARD_CONFIG_H

#include "key_map.h"

typedef union
{
    uint32_t raw;

    struct
    {
        uint32_t boot_layer : 4;
        uint32_t ver : 3;
        uint32_t normal_mode : 1;
        uint32_t reserved : 24;
    } bits;
} ConfigHeader_t;

#define CONFIG_FLASH_PAGE_BYTE 256u
#define CONFIG_FLASH_BYTE 2048u
#define CONFIG_HEADER_BYTE ((uint32_t)sizeof(ConfigHeader_t))
#define CONFIG_KEYMAP_LAYER_BYTE (sizeof(Key_Map_t) * TOTAL_KEYS)
#define CONFIG_KEYMAP_LAYERS_NUM ((CONFIG_FLASH_BYTE - CONFIG_HEADER_BYTE) / (CONFIG_KEYMAP_LAYER_BYTE))

#endif //KEYBOARD_CONFIG_H
