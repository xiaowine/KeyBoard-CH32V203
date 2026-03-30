#ifndef KEYBOARD_UTILS_H
#define KEYBOARD_UTILS_H

#include <key_map.h>

#define INTF __attribute__((interrupt("WCH-Interrupt-fast")))
#define PACKED __attribute__((packed))
#define RAM __attribute__((section(".ramfunc")))
#define AL4 __attribute__((aligned(4)))
#define LINK_AND_AL(x,y) __attribute__((section(x), used, aligned(y)))
#define CEIL_DIV(x, y) (((x) + (y) - 1) / (y))
#define CLAMP_INPLACE(var, lo, hi) \
do                               \
{                                \
if ((var) > (hi))              \
(var) = (hi);                \
else if ((var) < (lo))         \
(var) = (lo);                \
} while (0)

uint32_t get_bit_index(uint32_t v);
uint8_t scale8_by_255(uint8_t value, uint8_t scale);
uint8_t km_get_code_count(const Key_Map_t* m);
const uint8_t* config_key_map_layer_address(uint8_t layer);

#endif
