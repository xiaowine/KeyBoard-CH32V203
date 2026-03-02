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
        __NOP();                          \
        GPIO_SetBits(KEY_PORT, KEY_PL);   \
    } while (0)

/* 74HC165 串联寄存器数量 */
#define HC165_COUNT 3

/* 通过 SPI+DMA 控制 74HC165 读取的模块 API */
void key_init(void);
RAM void key_start_scan(void);
RAM u8 key_transfer_complete(void);
RAM void key_copy_snapshot(u8 dest[HC165_COUNT]);

RAM void output_data(const u8 rx_buf[HC165_COUNT]);
RAM u8 key_is_pressed(u8 button_index);
// u8 *gekey_filtered(u8 attempts, unsigned int delay_ms);

#endif