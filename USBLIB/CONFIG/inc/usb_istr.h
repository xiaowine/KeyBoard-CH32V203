/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_istr.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : This file includes the peripherals header files in the
 *                      user application.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#ifndef __USB_ISTR_H
#define __USB_ISTR_H

void USB_Istr(void);
/* Unified endpoint handlers (take endpoint number as parameter) */
void USBD_EP_IN_Handler(uint8_t endp);
void USBD_EP_OUT_Handler(uint8_t endp);

#ifdef CTR_CALLBACK
void CTR_Callback(void);
#endif

#ifdef DOVR_CALLBACK
void DOVR_Callback(void);
#endif

#ifdef ERR_CALLBACK
void ERR_Callback(void);
#endif

#ifdef WKUP_CALLBACK
void WKUP_Callback(void);
#endif

#ifdef SUSP_CALLBACK
void SUSP_Callback(void);
#endif

#ifdef RESET_CALLBACK
void RESET_Callback(void);
#endif

#ifdef SOF_CALLBACK
void SOF_Callback(void);
#endif

#ifdef ESOF_CALLBACK
void ESOF_Callback(void);
#endif

#endif /*__USB_ISTR_H*/
