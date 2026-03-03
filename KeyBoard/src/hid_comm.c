/********************* HID Communication Implementation *******************
 * File Name          : hid_comm.c
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : HID communication implementation
 *************************************************************************/
#include "hid_comm.h"
#include "usb_lib.h"
#include "debug.h"
#include <string.h>

/* External functions from usb_endp.c */
extern u8 USBD_SendCustomData(u8 *pbuf, u16 len);
extern u16 USBD_GetCustomData(u8 *pbuf, u16 max_len);

/* Static variables */
static u8 rx_buffer[HID_COMM_DATA_SIZE] = {0};
static hid_comm_callback_t data_received_callback = NULL;

/*********************************************************************
 * @fn      hid_comm_send
 *
 * @brief   Send data through HID communication (31-byte payload + Report ID)
 *
 * @param   data - pointer to data buffer
 *          len  - data length (max 31 bytes - Report ID automatically added)
 *
 * @return  Status (0=success, 1=error)
 */
RAM u8 hid_comm_send(u8 *data, u16 len)
{
  // Send via USB endpoint (Report ID will be automatically added)
  return USBD_SendCustomData(data, len);
}

/*********************************************************************
 * @fn      hid_comm_process
 *
 * @brief   Process received HID communication data
 *
 * @return  None
 */
RAM void hid_comm_process(void)
{
  u16 received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));
  u8 *data_ptr = rx_buffer;
  u16 data_len = received_len;

  if (received_len == 0)
    return;

  // // Check and remove Report ID if present
  if (received_len > 0 && rx_buffer[0] == 0x02)
  {
    // Valid output report with Report ID 2, skip the Report ID byte
    data_ptr = &rx_buffer[1];
    data_len = received_len - 1;
    PRINT("HID Comm: Received %d bytes (Report ID removed), actual data: %d bytes\r\n", received_len, data_len);
  }
  else
  {
    PRINT("HID Comm: Received %d bytes (raw data, no Report ID)\r\n", received_len);
  }

  if (data_received_callback)
  {
    data_received_callback(data_ptr, data_len);
  }
}

/*********************************************************************
 * @fn      hid_comm_set_callback
 *
 * @brief   Set callback function for received data
 *
 * @param   callback - callback function pointer
 *
 * @return  None
 */
RAM void hid_comm_set_callback(hid_comm_callback_t callback)
{
  data_received_callback = callback;
}