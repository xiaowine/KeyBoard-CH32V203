#include "encode.h"
#include "common.h"
#include "ch32v20x.h"
#include "usb_endp.h"


void encode_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(ENCODE_TIM_RCC, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = ENCODE_A | ENCODE_B;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(ENCODE_PORT, &GPIO_InitStructure);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
    TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
    TIM_TimeBaseStructure.TIM_Period = 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(ENCODE_TIM, &TIM_TimeBaseStructure);

    TIM_EncoderInterfaceConfig(ENCODE_TIM, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

    TIM_ICInitTypeDef TIM_ICInitStructure;
    TIM_ICStructInit(&TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICFilter = 0x7;
    TIM_ICInit(ENCODE_TIM, &TIM_ICInitStructure);
    TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;
    TIM_ICInitStructure.TIM_ICFilter = 0x7;
    TIM_ICInit(ENCODE_TIM, &TIM_ICInitStructure);

    TIM_ClearITPendingBit(ENCODE_TIM, TIM_IT_Update);
    TIM_ITConfig(ENCODE_TIM, TIM_IT_Update, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(ENCODE_TIM, ENABLE);
}

INTF void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(ENCODE_TIM, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(ENCODE_TIM, TIM_IT_Update);
        if (ENCODE_TIM->CTLR1 & TIM_DIR)
        {
            USBD_SendMouseReport(0, -1);
        }
        else
        {
            USBD_SendMouseReport(0, 1);
        }
    }
}
