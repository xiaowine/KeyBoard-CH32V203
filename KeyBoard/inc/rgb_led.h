#ifndef KEYBOARD_RGB_LED_H
#define KEYBOARD_RGB_LED_H

#include <stdint.h>
#include "utils.h"

#define RGB_LED_PIN GPIO_Pin_7
#define RGB_LED_PORT GPIOA
#define RGB_LED_TIM TIM3
#define RGB_LED_TIM_RCC RCC_APB1Periph_TIM3
#define RGB_LED_DMA DMA1_Channel3
#define RGB_LED_TIM_DMA_REQ TIM_DMA_Update

#define WS2812_BITS_PER_LED 24U
#define WS2812_RESET_SLOTS 64U
#define WS2812_FRAME_LEN (WS2812_BITS_PER_LED + WS2812_RESET_SLOTS)

#define WS2812_BIT_FREQ_HZ 800000U

typedef struct PACKED
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef struct
{
    const Color *path;
    uint8_t path_len;
    uint8_t seg_idx;
    uint8_t loop;
    uint8_t is_running;
    uint16_t steps_per_segment;
    uint16_t remaining_steps;
    int32_t cur_r;
    int32_t cur_g;
    int32_t cur_b;
    int32_t step_r;
    int32_t step_g;
    int32_t step_b;
} Gradient;

void rgb_led_init(void);
void rgb_led_set_color(uint8_t r, uint8_t g, uint8_t b);
void rgb_led_set_brightness(uint8_t brightness);
uint8_t rgb_led_is_busy(void);

void start_gradient(Gradient *grad, const Color *path, uint8_t path_len, uint16_t steps_per_segment, uint8_t loop);
uint8_t update_gradient(Gradient *grad, Color *out_color);

#endif // KEYBOARD_RGB_LED_H
