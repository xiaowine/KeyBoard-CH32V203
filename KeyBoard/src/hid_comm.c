/********************* HID Communication Implementation *******************
 * File Name          : hid_comm.c
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : HID communication implementation
 *************************************************************************/
#include "hid_comm.h"
#include "usb_endp.h"
#include "debug.h"

/* External functions from usb_endp.c */
/* Static variables */
static uint8_t rx_buffer[HID_COMM_DATA_SIZE] = {0};

/*********************************************************************
 * @fn      hid_comm_send
 *
 * @brief   Send data through HID communication (up to 32-byte payload)

 * @param   data - pointer to data buffer
 * @param len should not exceed HID_COMM_DATA_SIZE
 *
 * @return  Status (0=success, 1=error)
 */
uint8_t hid_comm_send(const uint8_t* data, const uint16_t len)
{
    return USBD_SendCustomData(data, len);
}

/*********************************************************************
 * @fn      hid_comm_process
 *
 * @brief   Process received HID communication data
 *
 * @return  None
 */
void hid_comm_process(void)
{
    const uint16_t received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));

    if (received_len == 0)
    {
        return;
    }
    PRINT("HID Communication: Received %d bytes\r\n", received_len);
    // HidFrame_t *frame = (HidFrame_t *)rx_buffer;
    // PRINT("Received HID Frame: Type=%d, DataType=%d, Seq=%d, PayloadLen=%d\r\n",
    //       frame->ctrl.type, frame->data_type, frame->seq, frame->ctrl.len);
}
