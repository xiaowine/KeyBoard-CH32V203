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
    if (endp == ENDP3)
    {
        /* Direct read from PMA to application buffer for custom OUT endpoint */
        USB_Rx_Cnt = USB_SIL_Read(EP3_OUT, (uint8_t*)USBD_HId_Comm_Data_Buffer);

        /* Re-enable RX for next reception */
        SetEPRxValid(ENDP3);
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
 * @param pbuf - pointer to data buffer to send
 * @param len
 *
 * @return  data up status.
 */
uint8_t USBD_ENDPx_DataUp(const uint8_t endp, uint8_t* pbuf, uint16_t len)
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
 * @param pbuf pointer to data buffer.
 * @param max_len maximum length of data to copy.
 *
 * @return  Received data length.
 */
uint16_t USBD_GetCustomData(uint8_t* pbuf, const uint16_t max_len)
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
 * @param pbuf pointer to data buffer to send.
 * @param len length of data to send.
 *
 * @return  Send status.
 */
uint8_t USBD_SendCustomData(const uint8_t* pbuf, uint16_t len)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_CUSTOM];

    if (len > DEF_ENDP_SIZE_CUSTOM - 1) // Reserve space for Report ID
        len = DEF_ENDP_SIZE_CUSTOM - 1;

    // Add Report ID for input reports
    send_buffer[0] = 0x01; // Report ID 1 for input
    memcpy(&send_buffer[1], pbuf, len);

    return USBD_ENDPx_DataUp(ENDP3, send_buffer, DEF_ENDP_SIZE_CUSTOM);
}

/**
 * @fn      USBD_SendConsumerReport
 * @brief   Send Consumer Control report via EP6 IN
 */
uint8_t USBD_SendConsumerReport(const uint16_t* usages, uint8_t count)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_CONSUMER];
    uint16_t report_len = 1 + 2 * 3; /* Report ID + 3 usages (fixed length expected by host) */

    if (count > 3)
        count = 3; /* descriptor supports up to 3 usages */

    /* Clear the buffer (ensure unused slots are zero) and set Report ID (1 byte) */
    memset(send_buffer, 0, sizeof(send_buffer));
    send_buffer[0] = 0x06; /* Report ID 6 for consumer */

    for (uint8_t i = 0; i < count; i++)
    {
        send_buffer[1 + i * 2] = (uint8_t)(usages[i] & 0xFF);
        send_buffer[1 + i * 2 + 1] = (uint8_t)(usages[i] >> 8 & 0xFF);
    }
    /* send the full report length (pad unused usages with zeros) */
    return USBD_ENDPx_DataUp(ENDP4, send_buffer, (uint16_t)report_len);
}

/**
 * @fn      USBD_SendMouseReport
 * @brief   Send Boot Mouse report via ENDP5 IN
 */
uint8_t USBD_SendMouseReport(uint8_t buttons_mask, int16_t wheel)
{
    uint8_t buf[DEF_ENDP_SIZE_MOUSE];
    const uint16_t send_len = 4; /* boot/report 4-byte format */

    memset(buf, 0, sizeof(buf));

    /* buttons: lower 5 bits used */
    buf[0] = buttons_mask & 0x1F;

    /* clamp wheel to signed 8-bit range */
    if (wheel > 127)
        wheel = 127;
    if (wheel < -127)
        wheel = -127;
    buf[3] = (uint8_t)(int8_t)wheel;

    return USBD_ENDPx_DataUp(ENDP5, buf, send_len);
}

/**
 * @fn      USBD_SendKeyboardReports
 * @brief   Pack and send keyboard HID reports (6-key rollover) across endpoints.
 *
 * The function will create reports of size DEF_ENDP_SIZE_KB with layout:
 * [modifiers, reserved, k1..k6]
 * It sends up to 6 usages per report to endpoints starting at ENDP1.
 */
uint8_t USBD_SendKeyboardReports(const uint8_t modifiers, const uint8_t* codes, const uint8_t total_codes)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_KB];
    /* Standard boot-compatible 6-key rollover report sent on ENDP1 */
    send_buffer[0] = modifiers;
    send_buffer[1] = 0; /* reserved */

    /* Clear the six key slots in one call then copy up to 6 codes */
    memset(&send_buffer[2], 0, 6);
    const uint8_t to_copy = (uint8_t)(total_codes > 6 ? 6 : total_codes);
    if (to_copy)
    {
        memcpy(&send_buffer[2], codes, to_copy);
    }

    return USBD_ENDPx_DataUp(ENDP1, send_buffer, DEF_ENDP_SIZE_KB);
}

// /**
//  * @fn      USBD_SendNKROReport
//  * @brief   Send bitmap NKRO report via ENDP2.
//  *          Report format: [ReportID=2][15 bytes bitmap covering usages 0..119]
//  */
// uint8_t USBD_SendNKROReport(const uint8_t *codes, uint8_t total_codes)
// {
//     static uint8_t send_buffer[DEF_ENDP_SIZE_NKRO];
//     /* Clear buffer and set Report ID */
//     memset(send_buffer, 0, sizeof(send_buffer));
//     send_buffer[0] = 0x02; /* Report ID 2 */

//     /* Bitmap payload starts at offset 1, covers 120 bits (15 bytes) */
//     for (uint8_t i = 0; i < total_codes; i++)
//     {
//         uint8_t code = codes[i];
//         if (code >= 120)
//             continue; /* ignore out-of-range usages */
//         uint8_t byte_idx = 1 + (code / 8);
//         uint8_t bit = (uint8_t)(1u << (code & 7));
//         send_buffer[byte_idx] |= bit;
//     }

//     return USBD_ENDPx_DataUp(ENDP2, send_buffer, DEF_ENDP_SIZE_NKRO);
// }

/**
 * @fn      USBD_SendNKROBitmap
 * @brief   Send NKRO bitmap report via ENDP2.
 *          Report format: [ReportID=2][15 bytes bitmap covering usages 0..119]
 */
uint8_t USBD_SendNKROBitmap(const uint8_t* bitmap)
{
    static uint8_t send_buffer[DEF_ENDP_SIZE_NKRO];
    /* Clear buffer and set Report ID */
    memset(send_buffer, 0, sizeof(send_buffer));
    send_buffer[0] = 0x02; /* Report ID 2 */

    /* Copy 15-byte bitmap into payload (offset 1) */
    memcpy(&send_buffer[1], bitmap, 15);

    return USBD_ENDPx_DataUp(ENDP2, send_buffer, DEF_ENDP_SIZE_NKRO);
}
