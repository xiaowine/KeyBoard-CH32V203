#ifndef KEYBOARD_KEY_H
#define KEYBOARD_KEY_H
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

/* 按键与采样窗口配置 */
#define KEY_TOTAL_KEYS (HC165_COUNT * 8)
#define KEY_SAMPLE_WINDOW 3
#define KEY_DEBOUNCE_CONFIRM_COUNT 2

/* 按键消抖状态 */
typedef enum
{
    KEY_DEBOUNCE_IDLE = 0, /* 空闲（未按下） */
    KEY_DEBOUNCE_PRESSED, /* 按下确认 */
} key_debounce_state_e;

/* 每键的消抖状态跟踪 */
typedef struct
{
    key_debounce_state_e state; /* 当前状态 */
    uint8_t sample_count; /* 连续采样计数（用于确认转移） */
} key_debounce_t;

/* 过滤与四态管理接口 */
void key_store_sample(uint8_t slot, const uint8_t sample[HC165_COUNT]);
void key_do_filter_and_update(void);
key_debounce_state_e key_get_debounce_state(uint8_t key_idx);

/* 通过 SPI+DMA 控制 74HC165 读取的模块 API */
void key_init(void);
void key_start_scan(void);
uint8_t key_transfer_complete(void);
void key_copy_snapshot(uint8_t dest[HC165_COUNT]);

void output_data(const uint8_t rx_buf[HC165_COUNT]);

#endif