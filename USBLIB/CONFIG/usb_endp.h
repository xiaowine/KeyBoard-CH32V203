/* usb_endp.h - Declarations for usb_endp.c functions */
#ifndef __USB_ENDP_H
#define __USB_ENDP_H

#ifdef __cplusplus
extern "C" {


#endif

uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t* pbuf, uint16_t len);
uint16_t USBD_GetCustomData(uint8_t* pbuf, uint16_t max_len);
uint8_t USBD_SendCustomData(uint8_t* pbuf, uint16_t len);
uint8_t USBD_SendConsumerReport(const uint16_t* usages, uint8_t count);
uint8_t USBD_SendKeyboardReports(uint8_t modifiers, const uint8_t* codes, uint8_t total_codes);

#ifdef __cplusplus
}
#endif

#endif /* __USB_ENDP_H */
