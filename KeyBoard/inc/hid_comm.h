/********************* HID Communication Header File **************************
 * File Name          : hid_comm.h
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : Header for HID communication
 ****************************************************************************/
#ifndef __HID_COMM_H
#define __HID_COMM_H

#include "usb_desc.h"

#define HID_COMM_DATA_SIZE DEF_ENDP_SIZE_CUSTOM // 32 bytes

/* HID communication data received callback function type */
typedef void (*hid_comm_callback_t)(u8 *data, u16 len);

u8 hid_comm_send(u8 *data, u16 len);
void hid_comm_process(void);
void hid_comm_set_callback(hid_comm_callback_t callback);

#endif /* __HID_COMM_H */