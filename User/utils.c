#include "utils.h"

#define DEBUG_DATA0_ADDRESS ((volatile uint32_t *)0xE0000380)
#define DEBUG_DATA1_ADDRESS ((volatile uint32_t *)0xE0000384)

/*********************************************************************
 * @fn      USART_Printf_Init
 *
 * @brief   Initializes the USARTx peripheral.
 *
 * @param   baudrate - USART communication baud rate.
 *
 * @return  None
 */
void USART_Printf_Init(uint32_t baudrate)
{
#ifdef DEBUG
    GPIO_InitTypeDef GPIO_InitStructure;
#endif
    USART_InitTypeDef USART_InitStructure;

#if (DEBUG == DEBUG_UART1)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#elif (DEBUG == DEBUG_UART2)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

#elif (DEBUG == DEBUG_UART3)
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

#endif

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;

#if (DEBUG == DEBUG_UART1)
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);

#elif (DEBUG == DEBUG_UART2)
    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);

#elif (DEBUG == DEBUG_UART3)
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);

#endif
}

/*********************************************************************
 * @fn      SDI_Printf_Enable
 *
 * @brief   Initializes the SDI printf Function.
 *
 * @param   None
 *
 * @return  None
 */
void SDI_Printf_Enable(void)
{
    *(DEBUG_DATA0_ADDRESS) = 0;
    Delay_Init();
    Delay_Ms(1);
}

/*********************************************************************
 * @fn      _write
 *
 * @brief   Support Printf Function
 *
 * @param   *buf - UART send Data.
 *          size - Data length
 *
 * @return  size: Data length
 */
__attribute__((used)) int _write(int fd, char *buf, int size)
{
    int i = 0;

#if (SDI_PRINT == SDI_PR_OPEN)
    int writeSize = size;

    do
    {

        /**
         * data0  data1 8 byte
         * data0 The storage length of the lowest byte, with a maximum of 7 bytes.
         */

        while ((*(DEBUG_DATA0_ADDRESS) != 0u))
        {
        }

        if (writeSize > 7)
        {
            *(DEBUG_DATA1_ADDRESS) = (*(buf + i + 3)) | (*(buf + i + 4) << 8) | (*(buf + i + 5) << 16) | (*(buf + i + 6) << 24);
            *(DEBUG_DATA0_ADDRESS) = (7u) | (*(buf + i) << 8) | (*(buf + i + 1) << 16) | (*(buf + i + 2) << 24);

            i += 7;
            writeSize -= 7;
        }
        else
        {
            *(DEBUG_DATA1_ADDRESS) = (*(buf + i + 3)) | (*(buf + i + 4) << 8) | (*(buf + i + 5) << 16) | (*(buf + i + 6) << 24);
            *(DEBUG_DATA0_ADDRESS) = (writeSize) | (*(buf + i) << 8) | (*(buf + i + 1) << 16) | (*(buf + i + 2) << 24);

            writeSize = 0;
        }

    } while (writeSize);

#else
    for (i = 0; i < size; i++)
    {
#if (DEBUG == DEBUG_UART1)
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
            ;
        USART_SendData(USART1, *buf++);
#elif (DEBUG == DEBUG_UART2)
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
            ;
        USART_SendData(USART2, *buf++);
#elif (DEBUG == DEBUG_UART3)
        while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
            ;
        USART_SendData(USART3, *buf++);
#endif
    }
#endif
    return size;
}

/*********************************************************************
 * @fn      _sbrk
 *
 * @brief   Change the spatial position of data segment.
 *
 * @return  size: Data length
 */
__attribute__((used)) void *_sbrk(ptrdiff_t incr)
{
    extern char _end[];
    extern char _heap_end[];
    static char *curbrk = _end;

    if ((curbrk + incr < _end) || (curbrk + incr > _heap_end))
        return NULL - 1;

    curbrk += incr;
    return curbrk - incr;
}

// De Bruijn 序列表 (长度 32)
//
// 说明：
// 该表与一个 De Bruijn 常数 0x077CB531U 配合使用，用来在 O(1) 时间内
// 通过将仅包含最低置位的值乘以该常数并右移得到的索引，快速映射到最低置位的位位置。
// 实现步骤：
//  1. v & -v -> 仅保留最低的 1 位（LSB）。
//  2. 将结果乘以 De Bruijn 常数 0x077CB531U。
//  3. 右移 27 位得到一个 0..31 的索引，用于查表。
//
// 注意：
//  - 本表与所选常数和位宽（32 位）配套使用，适用于 32 位无符号整数。
//  - 当输入 v == 0 时，本方法并不表示任何置位；当前实现会返回表的第 0 项（即 0），
//    调用者如需区分 v==0 的情况，应在调用前进行检查。
static const uint8_t DeBruijnTable[32] = {
    0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
    31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9};

/**
 * @brief 获取最低置位（least significant 1 bit）的位索引（0-31）。
 *
 * 该函数使用 De Bruijn 技术在常数时间内计算出最低置位的索引。
 * 实现细节见上方 De BruijnTable 注释。
 *
 * @param v 输入值（uint32_t）。函数内部会执行 v & -v，仅保留最低 1 位。
 * @return uint32_t 返回最低置位的索引（0-31）。当输入 v==0 时，返回 0（调用者若需特别处理应先检查）。
 */
uint32_t get_bit_index(uint32_t v)
{
    // 只保留最低的 1 位
    v &= -v;

    // 与 De Bruijn 常数相乘后右移得到索引
    const uint32_t hash = v * 0x077CB531U;

    const uint32_t index = hash >> 27;

    return DeBruijnTable[index];
}