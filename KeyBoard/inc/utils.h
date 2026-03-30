#ifndef KEYBOARD_UTILS_H
#define KEYBOARD_UTILS_H

#include <stdint.h>
#include "keymap.h"

uint32_t get_bit_index(uint32_t v);
uint8_t scale8_by_255(uint8_t value, uint8_t scale);
uint8_t km_get_code_count(const KeyMap_t* m);
const uint8_t* config_keymap_layer_address(uint8_t index);
const uint8_t* config_rgb_color_layer_address(uint8_t index);
#endif
