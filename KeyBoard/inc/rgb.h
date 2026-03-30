#ifndef KEYBOARD_RGB_H
#define KEYBOARD_RGB_H

#include <stdint.h>
#include "utils.h"

#define rgb_PIN GPIO_Pin_7
#define rgb_PORT GPIOA
#define rgb_TIM TIM3
#define rgb_TIM_RCC RCC_APB1Periph_TIM3
#define rgb_DMA DMA1_Channel3
#define rgb_TIM_DMA_REQ TIM_DMA_Update

#define WS2812_BITS_PER_LED 24U
#define WS2812_RESET_SLOTS 64U
#define WS2812_FRAME_LEN (WS2812_BITS_PER_LED + WS2812_RESET_SLOTS)

#define WS2812_BIT_FREQ_HZ 800000U

typedef struct PACKED
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color_t;

typedef struct
{
    const Color_t *path;
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
} Gradient_t;

void rgb_init(void);
void rgb_set_color_t(uint8_t r, uint8_t g, uint8_t b);
void rgb_set_brightness(uint8_t brightness);
uint8_t rgb_is_busy(void);

void start_Gradient_t(Gradient_t *grad, const Color_t *path, uint8_t path_len, uint16_t steps_per_segment, uint8_t loop);
uint8_t update_Gradient_t(Gradient_t *grad, Color_t *out_Color_t);

#endif
