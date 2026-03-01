
#ifndef __APP_H
#define __APP_H
#include "debug.h"

#define KEYBOARD_SCAN_FREQUENCY_HZ 500U

/* 键盘扫描状态机 */
typedef enum
{
  KEY_STATE_IDLE = 0,  /* 空闲状态，等待扫描请求 */
  KEY_STATE_SCANNING,  /* 正在进行 DMA 扫描 */
  KEY_STATE_DATA_READY /* 数据就绪，等待处理 */
} key_scan_state_t;

void app_init(void);
RAM_FUNC void app_run(void);

#endif