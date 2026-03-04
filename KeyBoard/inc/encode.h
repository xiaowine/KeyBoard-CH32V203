#ifndef KEYBOARD_ENCODE_H
#define KEYBOARD_ENCODE_H

#include "debug.h"

#define ENCODE_A GPIO_Pin_0
#define ENCODE_B GPIO_Pin_1
#define ENCODE_PORT GPIOA

extern volatile int circle;

void encode_init(void);

#endif