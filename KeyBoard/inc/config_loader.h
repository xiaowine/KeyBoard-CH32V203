#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "config.h"

extern ConfigHeader_t config_boot_config_ram;

/* Read the config header directly from flash into the caller-provided buffer. */
void config_loader_read_boot_config(ConfigHeader_t* config);

/* Load the config header from flash into RAM, then load the selected layer. */
void config_loader_init(void);

/* Load one layer into config_active and update the RAM config on success. */
void config_loader_load_layer(uint8_t layer);

/* Persist the config header to flash without updating the RAM shadow copy. */
uint8_t config_loader_write_boot_config(const ConfigHeader_t* config);

/* Persist one layer mapping to flash. */
uint8_t config_loader_write_layer(uint8_t layer, const uint8_t* layer_data, uint16_t layer_data_len);

/* Persist all layer mappings to flash. */
uint8_t config_loader_write_all_layers(const uint8_t* layers_data, uint16_t layers_data_len);

#endif
