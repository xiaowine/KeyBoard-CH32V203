#include "rgb_led.h"
#include "debug.h"
#include "utils.h"

static uint8_t s_brightness = 80U;
static Color s_last_rgb = {0U, 0U, 0U};
static uint16_t s_dma_frame[WS2812_FRAME_LEN] = {0U};
static volatile uint8_t s_dma_busy = 0U;
static uint16_t s_period_ticks = 0U;
static uint16_t s_t0h_ticks = 0U;
static uint16_t s_t1h_ticks = 0U;

void rgb_led_init(void)
{
    s_period_ticks = (uint16_t)(SystemCoreClock / WS2812_BIT_FREQ_HZ);
    s_t0h_ticks = (uint16_t)(s_period_ticks * 28U / 100U);
    s_t1h_ticks = (uint16_t)(s_period_ticks * 64U / 100U);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RGB_LED_TIM_RCC, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.GPIO_Pin = RGB_LED_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RGB_LED_PORT, &gpio_init);

    TIM_TimeBaseInitTypeDef tim_base_init = {0};
    tim_base_init.TIM_Prescaler = 0;
    tim_base_init.TIM_Period = s_period_ticks - 1U;
    tim_base_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_base_init.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(RGB_LED_TIM, &tim_base_init);

    TIM_OCInitTypeDef tim_oc_init = {0};
    tim_oc_init.TIM_OCMode = TIM_OCMode_PWM1;
    tim_oc_init.TIM_OutputState = TIM_OutputState_Enable;
    tim_oc_init.TIM_Pulse = 0;
    tim_oc_init.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC2Init(RGB_LED_TIM, &tim_oc_init);

    TIM_OC2PreloadConfig(RGB_LED_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(RGB_LED_TIM, ENABLE);

    DMA_DeInit(RGB_LED_DMA);

    DMA_InitTypeDef dma_init = {0};
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&RGB_LED_TIM->CH2CVR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)s_dma_frame;
    dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
    dma_init.DMA_BufferSize = WS2812_FRAME_LEN;
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_Priority = DMA_Priority_VeryHigh;
    dma_init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(RGB_LED_DMA, &dma_init);

    DMA_ITConfig(RGB_LED_DMA, DMA_IT_TC, ENABLE);

    NVIC_InitTypeDef nvic_dma_init = {0};
    nvic_dma_init.NVIC_IRQChannel = DMA1_Channel3_IRQn;
    nvic_dma_init.NVIC_IRQChannelPreemptionPriority = 1;
    nvic_dma_init.NVIC_IRQChannelSubPriority = 1;
    nvic_dma_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_dma_init);

    TIM_SetCompare2(RGB_LED_TIM, 0);
    rgb_led_set_color(0, 0, 0);
}

void rgb_led_build_frame(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t i = 0;
    const uint8_t sr = scale8_by_255(r, s_brightness);
    const uint8_t sg = scale8_by_255(g, s_brightness);
    const uint8_t sb = scale8_by_255(b, s_brightness);
    const uint32_t grb = ((uint32_t)sg << 16) | ((uint32_t)sr << 8) | (uint32_t)sb;

    for (i = 0; i < WS2812_BITS_PER_LED; i++)
    {
        s_dma_frame[i] = (grb & (1UL << (23U - i))) ? s_t1h_ticks : s_t0h_ticks;
    }

    for (i = WS2812_BITS_PER_LED; i < WS2812_FRAME_LEN; i++)
    {
        s_dma_frame[i] = 0U;
    }
}

void rgb_led_set_color(const uint8_t r, const uint8_t g, const uint8_t b)
{
    if (s_dma_busy)
    {
        return;
    }

    s_last_rgb.r = r;
    s_last_rgb.g = g;
    s_last_rgb.b = b;

    rgb_led_build_frame(r, g, b);
    s_dma_busy = 1U;

    DMA_Cmd(RGB_LED_DMA, DISABLE);
    DMA_ClearITPendingBit(DMA1_IT_TC3);
    DMA_SetCurrDataCounter(RGB_LED_DMA, WS2812_FRAME_LEN);
    DMA_Cmd(RGB_LED_DMA, ENABLE);

    TIM_DMACmd(RGB_LED_TIM, RGB_LED_TIM_DMA_REQ, ENABLE);
    TIM_Cmd(RGB_LED_TIM, ENABLE);
}

void rgb_led_set_brightness(const uint8_t brightness)
{
    s_brightness = brightness;

    if (!s_dma_busy)
    {
        rgb_led_set_color(s_last_rgb.r, s_last_rgb.g, s_last_rgb.b);
    }
}

uint8_t rgb_led_is_busy(void)
{
    return s_dma_busy;
}

static void gradient_prepare_segment(Gradient* grad, const uint8_t seg_idx)
{
    const Color* start = &grad->path[seg_idx];
    const Color* end = &grad->path[seg_idx + 1U];

    grad->seg_idx = seg_idx;
    grad->cur_r = (int32_t)start->r << 8;
    grad->cur_g = (int32_t)start->g << 8;
    grad->cur_b = (int32_t)start->b << 8;
    grad->step_r = (((int32_t)end->r - (int32_t)start->r) << 8) / (int32_t)grad->steps_per_segment;
    grad->step_g = (((int32_t)end->g - (int32_t)start->g) << 8) / (int32_t)grad->steps_per_segment;
    grad->step_b = (((int32_t)end->b - (int32_t)start->b) << 8) / (int32_t)grad->steps_per_segment;
    grad->remaining_steps = grad->steps_per_segment;
}

void start_gradient(Gradient* grad, const Color* path, const uint8_t path_len, uint16_t steps_per_segment,
                    const uint8_t loop)
{
    if (grad == 0 || path == 0 || path_len < 2U)
    {
        return;
    }

    if (steps_per_segment == 0U)
    {
        steps_per_segment = 1U;
    }

    grad->path = path;
    grad->path_len = path_len;
    grad->steps_per_segment = steps_per_segment;
    grad->loop = (loop != 0U) ? 1U : 0U;
    grad->is_running = 1U;

    gradient_prepare_segment(grad, 0U);
}

uint8_t update_gradient(Gradient* grad, Color* out_color)
{
    if ((grad == 0) || (out_color == 0) || (grad->is_running == 0U))
    {
        return 0U;
    }

    while (1)
    {
        if (grad->remaining_steps > 0U)
        {
            grad->cur_r += grad->step_r;
            grad->cur_g += grad->step_g;
            grad->cur_b += grad->step_b;

            out_color->r = (uint8_t)(grad->cur_r >> 8);
            out_color->g = (uint8_t)(grad->cur_g >> 8);
            out_color->b = (uint8_t)(grad->cur_b >> 8);

            grad->remaining_steps--;
            return 1U;
        }

        if ((grad->seg_idx + 1U) < (uint8_t)(grad->path_len - 1U))
        {
            gradient_prepare_segment(grad, (uint8_t)(grad->seg_idx + 1U));
            continue;
        }

        if (grad->loop != 0U)
        {
            gradient_prepare_segment(grad, 0U);
            continue;
        }

        grad->is_running = 0U;
        return 0U;
    }
}

INTF void DMA1_Channel3_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC3) != RESET)
    {
        DMA_ClearITPendingBit(DMA1_IT_TC3);

        TIM_DMACmd(RGB_LED_TIM, RGB_LED_TIM_DMA_REQ, DISABLE);
        DMA_Cmd(RGB_LED_DMA, DISABLE);
        TIM_SetCompare2(RGB_LED_TIM, 0);
        s_dma_busy = 0U;
    }
}
