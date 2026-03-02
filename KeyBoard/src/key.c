#include "key.h"
#include <string.h>

/* 模块内部缓冲区与标志 */
static u8 spi_dma_rx_buf[HC165_COUNT];
static u8 hc165_snapshot[HC165_COUNT];
static volatile u8 dma_transfer_complete_flag = 0;


void key_init (void) {
    /* SPI+DMA 设置，用于通过 SPI1 读取 74HC165 */
    RCC_APB2PeriphClockCmd (RCC_APB2Periph_SPI1, ENABLE);

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
    SPI_Init (SPI1, &SPI_InitStructure);

    /* SPI DMA 配置 */
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);

    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)spi_dma_rx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = HC165_COUNT;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init (DMA1_Channel2, &DMA_InitStructure);

    static uint8_t dummy_tx[HC165_COUNT] = {0xFF, 0xFF, 0xFF};
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)dummy_tx;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable; /* 发送固定值 */
    DMA_Init (DMA1_Channel3, &DMA_InitStructure);

    DMA_ITConfig (DMA1_Channel2, DMA_IT_TC, ENABLE);

    /* DMA 通道2 的 NVIC 配置 */
    NVIC_InitTypeDef NVIC_DMA_InitStructure;
    NVIC_DMA_InitStructure.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    NVIC_DMA_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_DMA_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_DMA_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init (&NVIC_DMA_InitStructure);

    SPI_I2S_DMACmd (SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd (SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd (SPI1, ENABLE);
}

RAM u8 key_transfer_complete (void) {
    return dma_transfer_complete_flag;
}

RAM void key_copy_snapshot (u8 dest[HC165_COUNT]) {
    memcpy (dest, hc165_snapshot, HC165_COUNT);
}

RAM void key_start_scan (void) {
    DMA_Cmd (DMA1_Channel2, DISABLE); /* RX（接收）*/
    DMA_Cmd (DMA1_Channel3, DISABLE); /* TX（发送）*/

    /* 启动新传输前清除任何挂起的 TC 标志 */
    DMA_ClearITPendingBit (DMA1_IT_TC2);
    dma_transfer_complete_flag = 0;

    DMA_SetCurrDataCounter (DMA1_Channel2, HC165_COUNT);
    DMA_SetCurrDataCounter (DMA1_Channel3, HC165_COUNT);

    // static u8 data[HC165_COUNT];
    /*
     * 74HC165 时序：
     * - 保持 CE 为高（时钟禁止）同时通过 PL (/CS) 加载并行输入。
     * - 将 PL 脉冲从低变高来锁存输入。
     * - 将 CE 拉低以启用时钟并执行 SPI 时钟读取。
     */

    KEY_DISABLE_CLOCK(); /* 禁止时钟 */
    KEY_LOAD_PL();       /* 加载并行输入 */
    KEY_ENABLE_CLOCK();  /* 启用时钟 */

    //  开启 DMA，开始传输
    DMA_Cmd (DMA1_Channel2, ENABLE);
    DMA_Cmd (DMA1_Channel3, ENABLE);
}

RAM void output_data (const u8 rx_buf[HC165_COUNT]) {
    char buffer[256];
    int offset = 0;

    for (u8 i = 0; i < HC165_COUNT; ++i) {
        offset += snprintf (buffer + offset, sizeof (buffer) - offset, "line%u: ", (unsigned)i);
        if (offset >= (int)sizeof (buffer) - 10)
            break;

        for (int bit = 7; bit >= 0; --bit)
            buffer[offset++] = ((rx_buf[i] >> bit) & 1) ? '1' : '0';

        buffer[offset++] = '\r';
        buffer[offset++] = '\n';
    }

    buffer[offset] = '\0';
    PRINT ("%s", buffer);
}

/**
 * @brief 检查指定按钮是否被按下
 * @param button_index 按钮索引 (0-23, 对应3个74HC165芯片的24个输入)
 * @return 1: 按钮按下, 0: 按钮未按下或索引无效
 * @note 74HC165输入逻辑: 0=按下, 1=未按下
 */
RAM u8 key_is_pressed (u8 button_index) {
    /* 检查索引范围 */
    if (button_index >= (HC165_COUNT * 8))
        return 0;

    u8 chip_index = button_index / 8; /* 确定是哪个74HC165芯片 */
    u8 bit_index = button_index % 8;  /* 确定是芯片内的哪一位 */

    /* 检查对应位，74HC165输入逻辑：0=按下，1=未按下 */
    return !(hc165_snapshot[chip_index] & (1 << bit_index));
}

INTF void DMA1_Channel2_IRQHandler (void) {
    if (DMA_GetITStatus (DMA1_IT_TC2)) {
        DMA_ClearITPendingBit (DMA1_IT_TC2);
        DMA_Cmd (DMA1_Channel2, DISABLE);
        DMA_Cmd (DMA1_Channel3, DISABLE);
        /* 将接收的数据拷贝到模块快照 */
        memcpy (hc165_snapshot, spi_dma_rx_buf, HC165_COUNT);
        dma_transfer_complete_flag = 1;
    }
}