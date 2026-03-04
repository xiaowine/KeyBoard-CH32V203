/* usb_endp.h - Declarations for usb_endp.c functions */
#ifndef __USB_ENDP_H
#define __USB_ENDP_H

#ifdef __cplusplus
extern "C" {

#endif

uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t* pbuf, uint16_t len);
uint16_t USBD_GetCustomData(uint8_t* pbuf, uint16_t max_len);
uint8_t USBD_SendCustomData(uint8_t* pbuf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USB_ENDP_H */
