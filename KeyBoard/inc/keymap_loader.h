#ifndef KEYMAP_LOADER_H
#define KEYMAP_LOADER_H

#include <stdint.h>
#include "keymap.h"

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
} KeymapBootConfig;

typedef char keymap_boot_config_size_must_be_4[(sizeof(KeymapBootConfig) == 4) ? 1 : -1];

#define KEYMAP_CONFIG_HEADER_BYTES ((uint32_t)sizeof(KeymapBootConfig))
#define LAYER_SIZE (sizeof(KeyMapping) * KEY_TOTAL_KEYS)
#define KEYMAP_LAYERS ((2048 - KEYMAP_CONFIG_HEADER_BYTES) / (LAYER_SIZE))

extern KeymapBootConfig keymap_boot_config_ram;

/* Load the config header from flash into RAM, then load the selected layer. */
void keymap_loader_init(void);

/* Load one layer into keymap_active and update the RAM config on success. */
void keymap_loader_load_layer(uint8_t layer);

#endif
