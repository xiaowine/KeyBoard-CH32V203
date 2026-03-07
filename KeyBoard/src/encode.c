#include "encode.h"
#include "utils.h"
#include "ch32v20x.h"

volatile int circle = 0;

/* Extended absolute encoder count (in counts). Updated from TIM2 IRQ on overflow/underflow. */
volatile int32_t encoder_abs = 0;
/* Last hardware 16-bit counter value (kept for debug/consistency). */
static volatile uint16_t last_hw_cnt = 0;
/* Last reported absolute value to callers (for delta computation). */
static volatile int32_t last_reported_abs = 0;

void encode_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = ENCODE_A | ENCODE_B;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ENCODE_PORT, &GPIO_InitStructure);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
    TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_ICInitTypeDef TIM_ICInitStructure;
    TIM_ICStructInit(&TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICFilter = 0x0F;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;
    TIM_ICInitStructure.TIM_ICFilter = 0x0F;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}

int16_t encode_get_count(void)
{
    /* Disable TIM2 IRQ briefly to read a consistent absolute value */
    NVIC_DisableIRQ(TIM2_IRQn);

    const uint16_t hw = TIM_GetCounter(TIM2);
    const int32_t cur_abs = encoder_abs + (int32_t)hw;

    const int16_t ret = (int16_t)((last_reported_abs - cur_abs) / 4);
    last_reported_abs = cur_abs;

    NVIC_EnableIRQ(TIM2_IRQn);
    return ret;
}

INTF void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        uint16_t cnt = TIM_GetCounter(TIM2);

        /* If DIR bit is set, the timer is counting down; on update event that's an underflow (wrap down)
         * Otherwise it's an overflow (wrap up). Adjust encoder_abs by +/- 65536 accordingly. */
        if (TIM2->CTLR1 & TIM_DIR)
        {
            encoder_abs -= 65536;
        }
        else
        {
            encoder_abs += 65536;
        }

        last_hw_cnt = cnt;
    }
}
