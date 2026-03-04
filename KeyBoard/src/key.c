#include "key.h"

#include <string.h>

#include "utils.h"

/* 模块内部缓冲区与标志 */
static uint8_t spi_dma_rx_buf[HC165_COUNT];
static uint8_t hc165_snapshot[HC165_COUNT];
static volatile uint8_t dma_transfer_complete_flag = 0;

/* 3kHz 采样与过滤管理 */
static uint8_t key_sample_buffer[KEY_SAMPLE_WINDOW][HC165_COUNT]; /* 3 次采样缓存 */
static uint8_t key_filtered_state[HC165_COUNT]; /* 1ms 多数投票结果 */
static key_debounce_t key_debounce_state[KEY_TOTAL_KEYS]; /* 每键状态跟踪 */

void key_init(void)
{
    /* SPI+DMA 设置，用于通过 SPI1 读取 74HC165 */
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

    /* SPI DMA 配置 */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_InitTypeDef DMA_InitStructure = {0};
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) & SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)spi_dma_rx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = HC165_COUNT;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    static uint8_t dummy_tx[HC165_COUNT] = {0xFF, 0xFF, 0xFF};
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)dummy_tx;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable; /* 发送固定值 */
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    DMA_ITConfig(DMA1_Channel2, DMA_IT_TC, ENABLE);

    /* DMA 通道2 的 NVIC 配置 */
    NVIC_InitTypeDef NVIC_DMA_InitStructure;
    NVIC_DMA_InitStructure.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    NVIC_DMA_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_DMA_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_DMA_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_DMA_InitStructure);

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI1, ENABLE);
}

uint8_t key_transfer_complete(void)
{
    return dma_transfer_complete_flag;
}

void key_copy_snapshot(uint8_t dest[HC165_COUNT])
{
    memcpy(dest, hc165_snapshot, HC165_COUNT);
}

void key_start_scan(void)
{
    DMA_Cmd(DMA1_Channel2, DISABLE); /* RX（接收）*/
    DMA_Cmd(DMA1_Channel3, DISABLE); /* TX（发送）*/

    /* 启动新传输前清除任何挂起的 TC 标志 */
    DMA_ClearITPendingBit(DMA1_IT_TC2);
    dma_transfer_complete_flag = 0;

    DMA_SetCurrDataCounter(DMA1_Channel2, HC165_COUNT);
    DMA_SetCurrDataCounter(DMA1_Channel3, HC165_COUNT);

    // static uint8_t data[HC165_COUNT];
    /*
     * 74HC165 时序：
     * - 保持 CE 为高（时钟禁止）同时通过 PL (/CS) 加载并行输入。
     * - 将 PL 脉冲从低变高来锁存输入。
     * - 将 CE 拉低以启用时钟并执行 SPI 时钟读取。
     */

    KEY_DISABLE_CLOCK(); /* 禁止时钟 */
    KEY_LOAD_PL(); /* 加载并行输入 */
    KEY_ENABLE_CLOCK(); /* 启用时钟 */

    //  开启 DMA，开始传输
    DMA_Cmd(DMA1_Channel2, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}

void output_data(const uint8_t rx_buf[HC165_COUNT])
{
    char buffer[256];
    int offset = 0;

    for (uint8_t i = 0; i < HC165_COUNT; ++i)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "line%u: ", (unsigned)i);
        if (offset >= (int)sizeof(buffer) - 10)
            break;

        for (int bit = 7; bit >= 0; --bit)
            buffer[offset++] = ((rx_buf[i] >> bit) & 1) ? '1' : '0';

        buffer[offset++] = '\r';
        buffer[offset++] = '\n';
    }

    buffer[offset] = '\0';
    PRINT("%s", buffer);
}

/**
 * @brief 将本次采样存入指定槽位
 * @param slot 采样槽位 (0~2)
 * @param sample 待存采样数据
 */
void key_store_sample(uint8_t slot, const uint8_t sample[HC165_COUNT])
{
    if (slot >= KEY_SAMPLE_WINDOW)
        return;
    memcpy(key_sample_buffer[slot], sample, HC165_COUNT);
}

/**
 * @brief 执行 2/3 多数投票并更新每键四态（连续 2 次确认转移）
 * @note 应在凑够 3 次采样后调用（每 1ms 一次）
 */
void key_do_filter_and_update(void)
{
    /* 2/3 多数投票：利用布尔代数一次性处理整个字节
     * 对三个采样 a, b, c，结果为 (a & b) | (b & c) | (a & c)
     * 这表示至少 2 个采样为 1 的位位置
     */
    for (uint8_t byte_idx = 0; byte_idx < HC165_COUNT; byte_idx++)
    {
        uint8_t sample0 = key_sample_buffer[0][byte_idx];
        uint8_t sample1 = key_sample_buffer[1][byte_idx];
        uint8_t sample2 = key_sample_buffer[2][byte_idx];

        /* 布尔代数方式：(a & b) | (b & c) | (a & c) 给出都至少 2 个为 1 的位 */
        uint8_t filtered = (sample0 & sample1) | (sample1 & sample2) | (sample0 & sample2);
        key_filtered_state[byte_idx] = filtered;
    }

    /* 更新每键的四态状态机（连续 2 次确认转移） */
    for (uint8_t key_idx = 0; key_idx < KEY_TOTAL_KEYS; key_idx++)
    {
        uint8_t byte_idx = key_idx >> 3; /* key_idx / 8 */
        uint8_t bit_idx = key_idx & 0x07; /* key_idx % 8 */
        uint8_t key_level = (key_filtered_state[byte_idx] >> bit_idx) & 1;

        key_debounce_t* state = &key_debounce_state[key_idx];

        switch (state->state)
        {
        case KEY_DEBOUNCE_IDLE:
            /* 检测到按下（低电平有效：bit=0） */
            if (key_level == 0)
            {
                state->sample_count++;
                if (state->sample_count >= 2)
                {
                    state->state = KEY_DEBOUNCE_PRESSED;
                    state->sample_count = 0;
                }
            }
            else
            {
                state->sample_count = 0;
            }
            break;

        case KEY_DEBOUNCE_PRESSED:
            /* 检测到释放（高电平：bit=1） */
            if (key_level == 1)
            {
                state->sample_count++;
                if (state->sample_count >= 2)
                {
                    state->state = KEY_DEBOUNCE_IDLE;
                    state->sample_count = 0;
                }
            }
            else
            {
                state->sample_count = 0;
            }
            break;

        default:
            break;
        }
    }
}

/**
 * @brief 获取指定按键的消抖状态
 * @param key_idx 按键索引 (0-23)
 * @return 按键状态 (KEY_DEBOUNCE_IDLE 或 KEY_DEBOUNCE_PRESSED)
 */
key_debounce_state_e key_get_debounce_state(uint8_t key_idx)
{
    if (key_idx >= KEY_TOTAL_KEYS)
        return KEY_DEBOUNCE_IDLE;
    return key_debounce_state[key_idx].state;
}

INTF void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC2))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        DMA_Cmd(DMA1_Channel2, DISABLE);
        DMA_Cmd(DMA1_Channel3, DISABLE);
        /* 将接收的数据拷贝到模块快照 */
        memcpy(hc165_snapshot, spi_dma_rx_buf, HC165_COUNT);
        dma_transfer_complete_flag = 1;
    }
}