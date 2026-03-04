#ifndef KEYBOARD_APP_H
#define KEYBOARD_APP_H

#define KEYBOARD_SCAN_FREQUENCY_HZ 3000U

/* 键盘扫描状态机 */
typedef enum
{
    KEY_STATE_IDLE = 0, /* 空闲状态，等待扫描请求 */
    KEY_STATE_SCANNING, /* 正在进行 DMA 扫描 */
} key_scan_state_t;

void app_init(void);
void app_run(void);
#endif