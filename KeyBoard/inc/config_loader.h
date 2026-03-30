#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "keymap.h"
#include "rgb_led.h"

typedef union
{
  uint32_t raw;

  struct
  {
    uint32_t boot_layer : 4;
    uint32_t color_layer : 3;
    uint32_t normal_mode : 1;
    uint32_t reserved : 24;
  } bits;
} KeymapBootConfig;

#define CONFIG_PAGE_BYTES 256u
#define CONFIG_FLASH_BYTES 2048u

#define MAX_COLOR 5
#define MAX_COLOR_PATHS 5
#define COLOR_PATH_SIZE (sizeof(Color) * MAX_COLOR)
#define COLOR_PATHS_BYTES (MAX_COLOR_PATHS * COLOR_PATH_SIZE)
#define KEYMAP_CONFIG_HEADER_BYTES ((uint32_t)sizeof(KeymapBootConfig))
#define KEYMAP_LAYER_SIZE (sizeof(KeyMapping) * KEY_TOTAL_KEYS)
#define KEYMAP_LAYERS ((CONFIG_FLASH_BYTES - KEYMAP_CONFIG_HEADER_BYTES - COLOR_PATHS_BYTES) / (KEYMAP_LAYER_SIZE))

extern KeymapBootConfig keymap_boot_config_ram;
/* Only keep the currently active color path in RAM (single path of MAX_COLOR entries). */
extern Color color_ram[MAX_COLOR];

/* Read a single color path from flash into RAM (index 0..MAX_COLOR_PATHS-1). */
void config_loader_read_color_path(uint8_t path_index);

/* Read the config header directly from flash into the caller-provided buffer. */
void config_loader_read_boot_config(KeymapBootConfig *config);

/* Load the config header from flash into RAM, then load the selected layer. */
void config_loader_init(void);

/* Load one layer into keymap_active and update the RAM config on success. */
void config_loader_load_layer(uint8_t layer);

/* Persist the config header to flash without updating the RAM shadow copy. */
uint8_t config_loader_write_boot_config(const KeymapBootConfig *config);

/* Persist one layer mapping to flash. */
uint8_t config_loader_write_layer(uint8_t layer, const uint8_t *layer_data, uint16_t layer_data_len);

/* Persist all layer mappings to flash. */
uint8_t config_loader_write_all_layers(const uint8_t *layers_data, uint16_t layers_data_len);

#endif
