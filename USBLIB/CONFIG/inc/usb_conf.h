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
#define BTABLE_ADDRESS (0x00)

/* EP0  */
/* rx/tx buffer base address */
#define ENDP0_RXADDR (0x40)
#define ENDP0_TXADDR (0x80)

/* EP1  */
/* tx buffer base address */
#define ENDP1_TXADDR (0xC0)
/* ENDP2: NKRO (16 bytes), kept aligned to 0x10 boundary */
#define ENDP2_TXADDR (ENDP1_TXADDR + 0x10)
/* ENDP3: Custom */
#define ENDP3_TXADDR (ENDP2_TXADDR + 0x10)
#define ENDP3_RXADDR (ENDP3_TXADDR + 0x40)
/* ENDP4: Consumer Control IN endpoint */
#define ENDP4_TXADDR (ENDP3_RXADDR + 0x40)
/* ENDP5: Mouse IN endpoint (allocated after ENDP4_TXADDR) */
#define ENDP5_TXADDR (ENDP4_TXADDR + 0x10)

/* ISTR events */
/* IMR_MSK */
/* mask defining which events has to be handled */
/* by the device application software */
#define IMR_MSK (CNTR_CTRM | CNTR_RESETM)

/* #define CTR_CALLBACK */
/* #define DOVR_CALLBACK */
/* #define ERR_CALLBACK */
/* #define WKUP_CALLBACK */
/* #define SUSP_CALLBACK */
/* #define RESET_CALLBACK */
/* #define SOF_CALLBACK */
/* #define ESOF_CALLBACK */

#endif /* __USB_CONF_H */
