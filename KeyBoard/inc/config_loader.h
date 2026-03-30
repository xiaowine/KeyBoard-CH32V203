#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

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
} ConfigBootConfig;

typedef char config_boot_config_size_must_be_4[(sizeof(ConfigBootConfig) == 4) ? 1 : -1];

#define CONFIG_PAGE_BYTES 256u
#define CONFIG_FLASH_BYTES 2048u
#define CONFIG_HEADER_BYTES ((uint32_t)sizeof(ConfigBootConfig))
#define CONFIG_LAYER_SIZE (sizeof(KeyMapping) * KEY_TOTAL_KEYS)
#define CONFIG_LAYERS ((CONFIG_FLASH_BYTES - CONFIG_HEADER_BYTES) / (CONFIG_LAYER_SIZE))

extern ConfigBootConfig config_boot_config_ram;

/* Read the config header directly from flash into the caller-provided buffer. */
void config_loader_read_boot_config(ConfigBootConfig *config);

/* Load the config header from flash into RAM, then load the selected layer. */
void config_loader_init(void);

/* Load one layer into config_active and update the RAM config on success. */
void config_loader_load_layer(uint8_t layer);

/* Persist the config header to flash without updating the RAM shadow copy. */
uint8_t config_loader_write_boot_config(const ConfigBootConfig *config);

/* Persist one layer mapping to flash. */
uint8_t config_loader_write_layer(uint8_t layer, const uint8_t *layer_data, uint16_t layer_data_len);

/* Persist all layer mappings to flash. */
uint8_t config_loader_write_all_layers(const uint8_t *layers_data, uint16_t layers_data_len);

#endif
