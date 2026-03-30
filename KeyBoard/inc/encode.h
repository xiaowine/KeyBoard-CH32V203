#ifndef KEYBOARD_ENCODE_H
#define KEYBOARD_ENCODE_H

#include "debug.h"

#define ENCODE_A GPIO_Pin_6
#define ENCODE_B GPIO_Pin_7
#define ENCODE_PORT GPIOB
#define ENCODE_TIM TIM4
#define ENCODE_TIM_RCC RCC_APB1Periph_TIM4

/**
 * Initialize encoder hardware (TIM2 encoder interface).
 */
void encode_init(void);

#endif
