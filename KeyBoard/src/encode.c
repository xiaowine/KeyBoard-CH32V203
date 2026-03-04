#include "encode.h"
#include "utils.h"

volatile int circle = 0;

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

    // NVIC_InitTypeDef NVIC_InitStructure;
    // NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    // NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    // NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    // NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    // NVIC_Init(&NVIC_InitStructure);
    //
    // TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
    // TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);
    // TIM_ITConfig(TIM2, TIM_IT_CC1 | TIM_IT_CC2, ENABLE);

    TIM_Cmd(TIM2, ENABLE);
}
//
// INTF void TIM2_IRQHandler(void)
// {
//     if (TIM_GetITStatus(TIM2, TIM_IT_CC1) != RESET || TIM_GetITStatus(TIM2, TIM_IT_CC2) != RESET)
//     {
//         if (TIM_GetITStatus(TIM2, TIM_IT_CC1) != RESET)
//         {
//             TIM_ClearITPendingBit(TIM2, TIM_IT_CC1);
//         }
//         if (TIM_GetITStatus(TIM2, TIM_IT_CC2) != RESET)
//         {
//             TIM_ClearITPendingBit(TIM2, TIM_IT_CC2);
//         }
//
//         static uint16_t last_cnt = 0;
//         const uint16_t current_cnt = TIM_GetCounter(TIM2);
//
//         if (current_cnt != last_cnt)
//         {
//             PRINT("Encoder Count: %u\r\n", current_cnt);
//             last_cnt = current_cnt;
//         }
//     }
// }
