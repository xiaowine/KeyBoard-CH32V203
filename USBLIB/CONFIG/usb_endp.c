/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_endp.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2021/08/08
 * Description        : Endpoint routines
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "hw_config.h"
#include "usb_istr.h"
#include "usb_pwr.h"
#include <string.h>

uint8_t USBD_Endp1_Busy, USBD_Endp2_Busy, USBD_Endp3_Busy, USBD_Endp4_Busy, USBD_Endp5_Busy;
u16 USB_Rx_Cnt = 0;
volatile uint8_t USBD_CustomData_Buffer[DEF_ENDP_SIZE_CUSTOM] = {0}; // Custom endpoint data buffer

/*********************************************************************
 * @fn      EP1_IN_Callback
 *
 * @brief  Endpoint 1 IN.
 *
 * @return  none
 */
void EP1_IN_Callback(void)
{
	USBD_Endp1_Busy = 0;
}

/*********************************************************************
 * @fn      EP2_IN_Callback
 *
 * @brief  Endpoint 2 IN.
 *
 * @return  none
 */
void EP2_IN_Callback(void)
{
	USBD_Endp2_Busy = 0;
}

/*********************************************************************
 * @fn      EP3_IN_Callback
 *
 * @brief  Endpoint 3 IN.
 *
 * @return  none
 */
void EP3_IN_Callback(void)
{
	USBD_Endp3_Busy = 0;
}

/*********************************************************************
 * @fn      EP4_IN_Callback
 *
 * @brief  Endpoint 4 IN.
 *
 * @return  none
 */
void EP4_IN_Callback(void)
{
	USBD_Endp4_Busy = 0;
}

/*********************************************************************
 * @fn      EP5_IN_Callback
 *
 * @brief  Endpoint 5 IN.
 *
 * @return  none
 */
void EP5_IN_Callback(void)
{
	USBD_Endp5_Busy = 0;
}

/*********************************************************************
 * @fn      EP5_OUT_Callback
 *
 * @brief  Endpoint 5 OUT.
 *
 * @return  none
 */
void EP5_OUT_Callback(void)
{
	// Read received data
	USB_Rx_Cnt = USB_SIL_Read(EP5_OUT, (uint8_t *)USBD_CustomData_Buffer);

	// Check Report ID and adjust data pointer if needed
	if (USB_Rx_Cnt > 0 && USBD_CustomData_Buffer[0] == 0x02)
	{
		// Valid output report with Report ID 2
		// Shift data to remove Report ID byte
		for (uint16_t i = 0; i < USB_Rx_Cnt - 1; i++)
		{
			USBD_CustomData_Buffer[i] = USBD_CustomData_Buffer[i + 1];
		}
		USB_Rx_Cnt--; // Decrement count to exclude Report ID
	}

	// Set EP5 RX valid for next reception
	SetEPRxValid(ENDP5);
}

/*********************************************************************
 * @fn      USBD_ENDPx_DataUp
 *
 * @brief  USBD ENDPx DataUp Function
 *
 * @param   endp - endpoint num.
 *          *pbuf - A pointer points to data.
 *          len - data length to transmit.
 *
 * @return  data up status.
 */
uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len)
{
	if (endp == ENDP1)
	{
		if (USBD_Endp1_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write(EP1_IN, pbuf, len);
		USBD_Endp1_Busy = 1;
		SetEPTxStatus(ENDP1, EP_TX_VALID);
	}
	else if (endp == ENDP2)
	{
		if (USBD_Endp2_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write(EP2_IN, pbuf, len);
		USBD_Endp2_Busy = 1;
		SetEPTxStatus(ENDP2, EP_TX_VALID);
	}
	else if (endp == ENDP3)
	{
		if (USBD_Endp3_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write(EP3_IN, pbuf, len);
		USBD_Endp3_Busy = 1;
		SetEPTxStatus(ENDP3, EP_TX_VALID);
	}
	else if (endp == ENDP4)
	{
		if (USBD_Endp4_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write(EP4_IN, pbuf, len);
		USBD_Endp4_Busy = 1;
		SetEPTxStatus(ENDP4, EP_TX_VALID);
	}
	else if (endp == ENDP5)
	{
		if (USBD_Endp5_Busy)
		{
			return USB_ERROR;
		}
		USB_SIL_Write(EP5_IN, pbuf, len);
		USBD_Endp5_Busy = 1;
		SetEPTxStatus(ENDP5, EP_TX_VALID);
	}
	else
	{
		return USB_ERROR;
	}
	return USB_SUCCESS;
}

/*********************************************************************
 * @fn      USBD_GetCustomData
 *
 * @brief  Get the custom data received from EP5_OUT
 *
 * @param   *pbuf - pointer to data buffer.
 *          *len - pointer to length variable.
 *
 * @return  Received data length.
 */
uint16_t USBD_GetCustomData(uint8_t *pbuf, uint16_t max_len)
{
	uint16_t copy_len = (USB_Rx_Cnt > max_len) ? max_len : USB_Rx_Cnt;
	if (copy_len > 0)
	{
		memcpy(pbuf, (uint8_t *)USBD_CustomData_Buffer, copy_len);
		USB_Rx_Cnt = 0; // Clear after reading
	}
	return copy_len;
}

/*********************************************************************
 * @fn      USBD_SendCustomData
 *
 * @brief  Send custom data via EP5_IN
 *
 * @param   *pbuf - pointer to data buffer.
 *          len - data length.
 *
 * @return  Send status.
 */
uint8_t USBD_SendCustomData(uint8_t *pbuf, uint16_t len)
{
	static uint8_t send_buffer[DEF_ENDP_SIZE_CUSTOM];

	if (len > DEF_ENDP_SIZE_CUSTOM - 1) // Reserve space for Report ID
		len = DEF_ENDP_SIZE_CUSTOM - 1;

	// Add Report ID for input reports
	send_buffer[0] = 0x01; // Report ID 1 for input
	memcpy(&send_buffer[1], pbuf, len);

	return USBD_ENDPx_DataUp(ENDP5, send_buffer, DEF_ENDP_SIZE_CUSTOM);
}
