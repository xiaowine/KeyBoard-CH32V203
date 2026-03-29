#ifndef KEYBOARD_ENCODE_H
#define KEYBOARD_ENCODE_H

#include "debug.h"

#define ENCODE_A GPIO_Pin_6
#define ENCODE_B GPIO_Pin_7
#define ENCODE_PORT GPIOB
#define ENCODE_TIM TIM4
#define ENCODE_TIM_RCC RCC_APB1Periph_TIM4

extern volatile int circle;

/**
 * Initialize encoder hardware (TIM2 encoder interface).
 */
void encode_init(void);

/**
 * Return the signed delta (difference) since the last call.
 * - Returns 0 on the first call (establishes baseline).
 * - Returns an int16_t delta; if the absolute change exceeds int16_t range,
 *   the value will be truncated.
 */
// int16_t encode_get_count(void);

#endif
