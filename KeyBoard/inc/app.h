#ifndef KEYBOARD_APP_H
#define KEYBOARD_APP_H

#include <stdint.h>

#define KEYBOARD_SCAN_FREQUENCY_HZ 3000U
#define MS_TICK (KEYBOARD_SCAN_FREQUENCY_HZ/1000U)

/* 定时器命名统一 (键盘扫描定时器) */
#define KEYSCAN_TIM TIM1
#define KEYSCAN_TIM_RCC RCC_APB2Periph_TIM1
#define KEYSCAN_TIM_IRQn TIM1_UP_IRQn

void app_init(void);
void app_run(void);
void app_comm_rx_callback(uint8_t payload_type, const uint8_t* payload, uint16_t payload_len);

#endif
