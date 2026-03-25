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

#define KEYMAP_CONFIG_PAGE_BYTES 256u
#define KEYMAP_CONFIG_FLASH_BYTES 2048u
#define KEYMAP_CONFIG_HEADER_BYTES ((uint32_t)sizeof(KeymapBootConfig))
#define LAYER_SIZE (sizeof(KeyMapping) * KEY_TOTAL_KEYS)
#define KEYMAP_LAYERS ((KEYMAP_CONFIG_FLASH_BYTES - KEYMAP_CONFIG_HEADER_BYTES) / (LAYER_SIZE))

extern KeymapBootConfig keymap_boot_config_ram;

/* Read the config header directly from flash into the caller-provided buffer. */
void keymap_loader_read_boot_config(KeymapBootConfig* config);

/* Load the config header from flash into RAM, then load the selected layer. */
void keymap_loader_init(void);

/* Load one layer into keymap_active and update the RAM config on success. */
void keymap_loader_load_layer(uint8_t layer);

/* Persist the config header to flash without updating the RAM shadow copy. */
uint8_t keymap_loader_write_boot_config(const KeymapBootConfig* config);

/* Persist one layer mapping to flash. */
uint8_t keymap_loader_write_layer(uint8_t layer, const uint8_t* layer_data, uint16_t layer_data_len);

/* Persist all layer mappings to flash. */
uint8_t keymap_loader_write_all_layers(const uint8_t* layers_data, uint16_t layers_data_len);

#endif
