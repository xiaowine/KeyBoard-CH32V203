#ifndef KEYBOARD_UTILS_H
#define KEYBOARD_UTILS_H

#include "app.h"
u32 get_bit_index(u32 v);

#define INTF __attribute__((interrupt("WCH-Interrupt-fast")))
#define RAM __attribute__((section(".ramfunc")))
#define AL4 __attribute__((aligned(4)))
#define CEIL_DIV(x,y) (((x) + (y) - 1) / (y))

#endif
