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
#include "usb_istr.h"
#include <string.h>

volatile uint8_t USBD_EndpBusy[EP_NUM + 1]; /* index 0 unused, use 1..EP_NUM */
uint16_t USB_Rx_Cnt = 0;
volatile uint8_t USBD_HId_Comm_Data_Buffer[DEF_ENDP_SIZE_CUSTOM] = {0}; // HID communication data buffer

/*********************************************************************
 * @fn      EP1_IN_Callback
 *
 * @brief  Endpoint 1 IN.
 *
 * @return  none
 */
void USBD_EP_IN_Handler(uint8_t endp)
{
    if (endp >= 1 && endp <= EP_NUM)
    {
        USBD_EndpBusy[endp] = 0;
    }
}

/* Unified IN handler `USBD_EP_IN_Handler` is used directly from interrupts. */

/*********************************************************************
 * @fn      EP5_OUT_Callback
 *
 * @brief  Endpoint 5 OUT - minimal overhead data receive
 *
 * @return  none
 */
/* Common OUT handler that dispatches based on endpoint number */
void USBD_EP_OUT_Handler(uint8_t endp)
{
    if (endp == ENDP5)
    {
        /* Direct read from PMA to application buffer, no intermediate copy */
        USB_Rx_Cnt = USB_SIL_Read(EP5_OUT, (uint8_t*)USBD_HId_Comm_Data_Buffer);

        /* Re-enable RX for next reception */
        SetEPRxValid(ENDP5);
    }
}

/* EP OUT callbacks are handled by USBD_EP_OUT_Handler directly. */

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
uint8_t USBD_ENDPx_DataUp(uint8_t endp, uint8_t* pbuf, uint16_t len)
{
    if (endp < 1 || endp > EP_NUM)
    {
        return USB_ERROR;
    }

    if (USBD_EndpBusy[endp])
    {
        return USB_ERROR;
    }

    USB_SIL_Write(endp, pbuf, len);
    USBD_EndpBusy[endp] = 1;
    SetEPTxStatus(endp, EP_TX_VALID);
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
uint16_t USBD_GetCustomData(uint8_t* pbuf, uint16_t max_len)
{
    const uint16_t copy_len = (USB_Rx_Cnt > max_len) ? max_len : USB_Rx_Cnt;
    if (copy_len > 0)
    {
        memcpy(pbuf, (uint8_t*)USBD_HId_Comm_Data_Buffer, copy_len);
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
uint8_t USBD_SendCustomData(uint8_t* pbuf, uint16_t len)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_CUSTOM];

    if (len > DEF_ENDP_SIZE_CUSTOM - 1) // Reserve space for Report ID
        len = DEF_ENDP_SIZE_CUSTOM - 1;

    // Add Report ID for input reports
    send_buffer[0] = 0x01; // Report ID 1 for input
    memcpy(&send_buffer[1], pbuf, len);

    return USBD_ENDPx_DataUp(ENDP5, send_buffer, DEF_ENDP_SIZE_CUSTOM);
}

/**
 * @fn      USBD_SendConsumerReport
 * @brief   Send Consumer Control report via EP6 IN
 */
uint8_t USBD_SendConsumerReport(uint16_t usage)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_CONSUMER];

    /* Report ID (1 byte) + Usage (2 bytes) -> total 3 bytes; pad to endpoint size */
    send_buffer[0] = 0x06; /* Report ID 6 for consumer */
    send_buffer[1] = (uint8_t)(usage & 0xFF);
    send_buffer[2] = (uint8_t)((usage >> 8) & 0xFF);

    /* send only Report ID + 2-byte usage (3 bytes) so host parses as standard HID report */
    return USBD_ENDPx_DataUp(ENDP6, send_buffer, DEF_ENDP_SIZE_CONSUMER);
}
