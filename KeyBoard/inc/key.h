#ifndef __KEY_H
#define __KEY_H
#include "debug.h"

#define KEY_CE GPIO_Pin_3   // 连接到74HC165的CE 时钟使能
#define KEY_PL GPIO_Pin_4   // 连接到74HC165的PL 负载载入
#define KEY_CP GPIO_Pin_5   // 连接到74HC165的CP 时钟
#define KEY_MISO GPIO_Pin_6 // 连接到74HC165的Q7 输出
#define KEY_PORT GPIOA
#define ENCODE_A GPIO_Pin_0
#define ENCODE_B GPIO_Pin_1
#define ENCODE_PORT GPIOA

#define KEY_DISABLE_CLOCK() GPIO_SetBits(KEY_PORT, KEY_CE)
#define KEY_ENABLE_CLOCK() GPIO_ResetBits(KEY_PORT, KEY_CE)
#define KEY_LOAD_PL()                     \
    do                                    \
    {                                     \
        GPIO_ResetBits(KEY_PORT, KEY_PL); \
        Delay_Us(50);                     \
        GPIO_SetBits(KEY_PORT, KEY_PL);   \
    } while (0)

/* Number of 74HC165 shift registers */
#define HC165_COUNT 3

u8 *gekey(void);
void output_data(const u8 *data);
u8 *gekey_filtered(u8 attempts, unsigned int delay_ms);

#endif