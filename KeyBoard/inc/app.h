
#ifndef APP_H
#define APP_H
#include "debug.h"

#define KEYBOARD_SCAN_FREQUENCY_HZ 3000U

/* 键盘扫描状态机 */
typedef enum
{
  KEY_STATE_IDLE = 0, /* 空闲状态，等待扫描请求 */
  KEY_STATE_SCANNING, /* 正在进行 DMA 扫描 */
} key_scan_state_t;

void app_init(void);
void app_run(void);

u8 USBD_ENDPx_DataUp(u8 endp, u8* pbuf, u16 len);
u8 USBD_SendCustomData(u8* pbuf, u16 len);
#endif