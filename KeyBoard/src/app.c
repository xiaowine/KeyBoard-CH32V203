#include "app.h"
#include "key.h"

void app_init(void)
{

    // Io Init
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

        GPIO_InitTypeDef GPIO_InitStructure = {0};
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

        GPIO_InitStructure.GPIO_Pin = ENCODE_A | ENCODE_B;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(ENCODE_PORT, &GPIO_InitStructure);

        GPIO_InitStructure.GPIO_Pin = KEY_CE | KEY_PL;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);
        GPIO_SetBits(KEY_PORT, KEY_CE | KEY_PL);

        GPIO_InitStructure.GPIO_Pin = KEY_MISO;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);

        GPIO_InitStructure.GPIO_Pin = KEY_CP;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_Init(KEY_PORT, &GPIO_InitStructure);
    }

#pragma region
// Encode Init (TIM)
// {
//     RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

//     TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
//     TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
//     TIM_TimeBaseStructure.TIM_Prescaler = 0x0;
//     TIM_TimeBaseStructure.TIM_Period = 0xFFFF;
//     TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
//     TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
//     TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

//     TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);

//     TIM_ICInitTypeDef TIM_ICInitStructure;
//     TIM_ICStructInit(&TIM_ICInitStructure);
//     TIM_ICInitStructure.TIM_ICFilter = 10;
//     TIM_ICInit(TIM2, &TIM_ICInitStructure);

//     NVIC_InitTypeDef NVIC_InitStructure;
//     NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
//     NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
//     NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
//     NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
//     NVIC_Init(&NVIC_InitStructure);

//     TIM_ClearFlag(TIM2, TIM_FLAG_Update);
//     TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
//     TIM_SetCounter(TIM2, (TIM_TimeBaseStructure.TIM_Period + 1) / 2);
//     TIM_Cmd(TIM2, ENABLE);
// }
#pragma endregion

    // Key Init (SPI)
    {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

        SPI_InitTypeDef SPI_InitStructure = {0};
        SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
        SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
        SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
        SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
        SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
        SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
        SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
        SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
        SPI_InitStructure.SPI_CRCPolynomial = 0;
        SPI_Init(SPI1, &SPI_InitStructure);

        // SPI DMA
        // DMA_InitTypeDef DMA_InitStructure = {0};

        // RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);

        // DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&SPI1->DATAR;
        // DMA_InitStructure.DMA_MemoryBaseAddr = (u32)RxData;
        // DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
        // DMA_InitStructure.DMA_BufferSize = Size;
        // DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        // DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
        // DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
        // DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
        // DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
        // DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
        // DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
        // DMA_Init (DMA1_Channel2, &DMA_InitStructure);

        // DMA_Cmd (DMA1_Channel2, DISABLE);
        // DMA_SetCurrDataCounter (DMA1_Channel2, Size);
        // DMA_Cmd (DMA1_Channel2, ENABLE);

        // SPI_I2S_DMACmd (SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
        SPI_Cmd(SPI1, ENABLE);
    }

    // TIM Init
    {
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

        TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

        // 设置定时器频率为 10 kHz（即 0.1 ms 的分辨率）
        const u16 tick_freq = 10000U;
        // 计算预分频值
        u16 prescaler = SystemCoreClock / tick_freq;
        // 预分频值至少为 1
        if (prescaler == 0)
            prescaler = 1;
        prescaler -= 1;

        // 计算周期：100 ms = 0.1 s -> ticks = tick_freq * 0.1 = tick_freq / 10
        u16 period_ticks = tick_freq / KEYBOARD_SCAN_FREQUENCY_HZ;
        // 周期至少为 1
        if (period_ticks == 0)
            period_ticks = 1;

        TIM_TimeBaseInitStructure.TIM_Prescaler = (u16)prescaler;
        TIM_TimeBaseInitStructure.TIM_Period = (u16)(period_ticks - 1);
        TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;

        TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        NVIC_InitTypeDef NVIC_InitStructure;
        NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
        TIM_Cmd(TIM3, ENABLE);
    }
}

void app_run(void)
{
    u8 *RxData = gekey();
    output_data(RxData);
    Delay_Ms(1000);
}

__attribute__((interrupt("WCH-Interrupt-fast"))) void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
    }
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}