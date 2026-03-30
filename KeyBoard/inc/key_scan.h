#ifndef KEYBOARD_KEY_SCAN_H
#define KEYBOARD_KEY_SCAN_H

#include <stdint.h>

#define HC165_PL GPIO_Pin_3   // 连接到74HC165的PL 负载载入
#define HC165_CE GPIO_Pin_4   // 连接到74HC165的CE 时钟使能
#define HC165_CP GPIO_Pin_5   // 连接到74HC165的CP 时钟
#define HC165_MISO GPIO_Pin_6 // 连接到74HC165的Q7 输出
#define HC165_PORT GPIOA
#define HC165_SPI SPI1
#define HC165_SPI_RCC RCC_APB2Periph_SPI1

#define HC165_DISABLE_CLOCK() GPIO_SetBits(HC165_PORT, HC165_CE)
#define HC165_ENABLE_CLOCK() GPIO_ResetBits(HC165_PORT, HC165_CE)
#define HC165_LOAD_PL()                       \
    do                                        \
    {                                         \
        GPIO_ResetBits(HC165_PORT, HC165_PL); \
        __NOP();                              \
        GPIO_SetBits(HC165_PORT, HC165_PL);   \
    } while (0)

/* 74HC165 串联寄存器数量 */
#define HC165_COUNT 3

/* 按键与采样窗口配置 */
#define TOTAL_KEYS (HC165_COUNT * 8)
#define SCAN_SAMPLE_WINDOW 3
#define SCAN_DEBOUNCE_CONFIRM_COUNT 2
/* 扫描超时保护 */
#define SCAN_TIMEOUT_TICKS 6U

/* 按键消抖状态 */
typedef enum
{
    KEY_DEBOUNCE_IDLE = 0, /* 空闲（未按下） */
    KEY_DEBOUNCE_PRESSED, /* 按下确认 */
} Key_Debounce_State_t;

/* 每键的消抖状态跟踪 */
typedef struct
{
    Key_Debounce_State_t state; /* 当前状态 */
    uint8_t sample_count; /* 连续采样计数（用于确认转移） */
} Key_Debounce_t;

/* 键盘扫描状态机 */
typedef enum
{
    KEY_STATE_IDLE = 0, /* 空闲状态，等待扫描请求 */
    KEY_STATE_SCANNING, /* 正在进行 DMA 扫描 */
} key_Scan_State_t;

/* 过滤与四态管理接口 */
void key_store_sample(uint8_t slot, const uint8_t sample[HC165_COUNT]);
void key_do_filter_and_update(void);
Key_Debounce_State_t key_get_debounce_state(uint8_t key_idx);

/* 通过 SPI+DMA 控制 74HC165 读取的模块 API */
void key_init(void);
void key_start_scan(void);
uint8_t key_transfer_complete(void);
void key_copy_snapshot(uint8_t dest[HC165_COUNT]);

#endif
