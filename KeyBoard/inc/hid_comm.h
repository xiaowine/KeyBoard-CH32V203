#ifndef KEYBOARD_HID_COMM_H
#define KEYBOARD_HID_COMM_H

#include "usb_desc.h"

#define HID_COMM_DATA_SIZE DEF_ENDP_SIZE_CUSTOM // 32 bytes

/* HID communication data received callback function type */
typedef void (*hid_comm_callback_t)(uint8_t* data, uint16_t len);

uint8_t hid_comm_send(uint8_t* data, uint16_t len);
void hid_comm_process(void);
void hid_comm_set_callback(hid_comm_callback_t callback);

#endif