#ifndef KEYBOARD_RGB_LED_H
#define KEYBOARD_RGB_LED_H
#include <stdint.h>

#define RGB_LED_PIN GPIO_Pin_7
#define RGB_LED_PORT GPIOA
#define RGB_LED_TIM TIM3
#define RGB_LED_TIM_RCC RCC_APB1Periph_TIM3
#define RGB_LED_DMA DMA1_Channel3
#define RGB_LED_TIM_DMA_REQ TIM_DMA_Update

void rgb_led_init(void);
void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b);
void rgb_led_set_brightness(uint8_t brightness);
uint8_t rgb_led_is_busy(void);

#endif // KEYBOARD_RGB_LED_H