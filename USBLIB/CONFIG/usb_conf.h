/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_conf.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/20
 * Description        : This file contains all the functions prototypes for the
 *                      USB configration firmware library.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#ifndef __USB_CONF_H
#define __USB_CONF_H

#define EP_NUM (7)

/* Buffer Description Table */
/* buffer table base address */
/* buffer table base address */
#define BTABLE_ADDRESS (0x00)

/* EP0  */
/* rx/tx buffer base address */
#define ENDP0_RXADDR (0x40)
#define ENDP0_TXADDR (0x80)

/* EP1  */
/* tx buffer base address */
#define ENDP1_TXADDR (0xC0)
#define ENDP2_TXADDR (ENDP1_TXADDR + 0x10)
#define ENDP3_TXADDR (ENDP2_TXADDR + 0x10)
#define ENDP4_TXADDR (ENDP3_TXADDR + 0x10)
#define ENDP5_TXADDR (ENDP4_TXADDR + 0x10)
#define ENDP5_RXADDR (ENDP5_TXADDR + 0x40)
#define ENDP6_TXADDR (ENDP5_RXADDR + 0x40)

/* ISTR events */
/* IMR_MSK */
/* mask defining which events has to be handled */
/* by the device application software */
#define IMR_MSK (CNTR_CTRM | CNTR_WKUPM | CNTR_SUSPM | CNTR_ERRM | CNTR_SOFM | CNTR_ESOFM | CNTR_RESETM)

/* #define CTR_CALLBACK */
/* #define DOVR_CALLBACK */
/* #define ERR_CALLBACK */
/* #define WKUP_CALLBACK */
/* #define SUSP_CALLBACK */
/* #define RESET_CALLBAC K*/
/* #define SOF_CALLBACK */
/* #define ESOF_CALLBACK */

#endif /* __USB_CONF_H */
