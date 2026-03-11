/********************************** (C) COPYRIGHT *******************************
 * File Name          : system_ch32v20x.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/06/06
 * Description        : CH32V20x Device Peripheral Access Layer System Header File.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#ifndef __SYSTEM_ch32v20x_H
#define __SYSTEM_ch32v20x_H

extern uint32_t SystemCoreClock; /* System Clock Frequency (Core Clock) */

/* System_Exported_Functions */
extern void SystemInit(void);
extern void SystemCoreClockUpdate(void);

void Delay_Init(void);
void Delay_Us(uint32_t n);
void Delay_Ms(uint32_t n);

#endif /*__CH32V20x_SYSTEM_H */
