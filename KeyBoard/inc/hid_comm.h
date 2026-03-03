#ifndef KEYBOARD_HID_COMM_H
#define KEYBOARD_HID_COMM_H

#include "usb_desc.h"

#define HID_COMM_DATA_SIZE DEF_ENDP_SIZE_CUSTOM // 32 bytes

/* HID communication data received callback function type */
typedef void (*hid_comm_callback_t)(u8* data, u16 len);

u8 hid_comm_send(u8* data, u16 len);
void hid_comm_process(void);
void hid_comm_set_callback(hid_comm_callback_t callback);

#endif
