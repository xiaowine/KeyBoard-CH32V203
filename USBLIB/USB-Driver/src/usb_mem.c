/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_mem.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Utility functions for memory transfers to/from PMA
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/ 
#include "usb_lib.h"


/*******************************************************************************
 * @fn           UserToPMABufferCopy
 *
 * @brief        Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param        pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes: no. of bytes to be copied.
 *
 * @param        None	.
 */
/*
 * Optimized version: 
 * - Pre-calculate PMA address once
 * - Use single pointer increment for contiguous writes
 * - Avoid redundant temp variables where possible
 */
void UserToPMABufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint32_t n = (wNBytes + 1) >> 1;   
  uint32_t i;
  uint16_t *pdwVal;
  uint8_t temp1, temp2;
  
  /* PMA地址计算：每个PMA地址对应2字节物理存储，所以乘以2后加基址 */
  pdwVal = (uint16_t *)(wPMABufAddr * 2 + PMAAddr);
	
  /* 字对齐写入（每次写16bit），跳过 PMA 内部空隙 */
  for (i = n; i != 0; i--)
  {
    temp1 = *pbUsrBuf++;
    temp2 = *pbUsrBuf++;
    /* 直接在一条语句中合并并写入，减少指令数 */
    *pdwVal++ = (uint16_t)temp1 | ((uint16_t)temp2 << 8);
    pdwVal++;  /* PMA 每个16bit字后跳过16bit（USB PMA 特性） */
  }
}

/*******************************************************************************
 * @fn          PMAToUserBufferCopy
 *
 * @brief       Copy a buffer from user memory area to packet memory area (PMA)
 *
 * @param       pbUsrBuf: pointer to user memory area.
 *                  wPMABufAddr: address into PMA.
 *                  wNBytes:  no. of bytes to be copied.
 *
 * @param       None. 
 */
/*
 * Optimized receive path:
 * - Streamlined pointer calculation
 * - Improved data fetching pattern
 */
void PMAToUserBufferCopy(uint8_t *pbUsrBuf, uint16_t wPMABufAddr, uint16_t wNBytes)
{
  uint32_t n = (wNBytes + 1) >> 1;
  uint32_t i;
  uint32_t *pdwVal;
  uint16_t data_word;
	
  pdwVal = (uint32_t *)(wPMABufAddr * 2 + PMAAddr);
	
  /* Read PMA data and extract to user buffer */
  for (i = n; i != 0; i--)
  {
    data_word = (uint16_t)*pdwVal++;
    *pbUsrBuf++ = (uint8_t)(data_word & 0xFF);
    *pbUsrBuf++ = (uint8_t)((data_word >> 8) & 0xFF);
  } 
}






