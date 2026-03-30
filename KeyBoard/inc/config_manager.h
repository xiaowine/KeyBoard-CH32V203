#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "config.h"

extern ConfigHeader_t config_header_ram;

/* Read the config header directly from flash into the caller-provided buffer. */
void config_read_header(ConfigHeader_t *config);

/* Load the config header from flash into RAM, then load the selected layer. */
void config_manager_init(void);

/* Load one layer into config_active and update the RAM config on success. */
uint8_t config_load_keymap_layer(uint8_t layer_index);

/* Persist the config header to flash without updating the RAM shadow copy. */
uint8_t config_write_header(const ConfigHeader_t *config);

/* Persist one layer mapping to flash. */
uint8_t config_write_keymap_layer(uint8_t layer, const uint8_t *layer_data, uint16_t layer_data_len);

/* Persist all layer mappings to flash. */
uint8_t config_write_all_keymap_layer(const uint8_t *layers_data, uint16_t layers_data_len);

/* Load one RGB color layer into config_active and update the RAM config on success. */
uint8_t config_load_rgb_color_layer(uint8_t layer_index);

/* Persist one RGB color layer to flash. */
uint8_t config_write_rgb_color_layer(uint8_t layer, const uint8_t *color_data, uint16_t color_data_len);

#endif
