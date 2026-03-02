/********************* Custom Endpoint Implementation *******************
 * File Name          : custom_endpoint.c
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : Custom endpoint communication implementation
 *************************************************************************/
#include "custom_endpoint.h"
#include "usb_lib.h"
#include "debug.h"
#include <string.h>

/* External functions from usb_endp.c */
extern uint8_t USBD_SendCustomData(uint8_t *pbuf, uint16_t len);
extern uint16_t USBD_GetCustomData(uint8_t *pbuf, uint16_t max_len);

/* Static variables */
static uint16_t sequence_number = 0;
static uint8_t rx_buffer[CUSTOM_DATA_PACKET_SIZE];

/*********************************************************************
 * @fn      custom_endpoint_init
 *
 * @brief   Initialize custom endpoint
 *
 * @return  None
 */
void custom_endpoint_init(void)
{
  sequence_number = 0;
  memset(rx_buffer, 0, sizeof(rx_buffer));
  PRINT("Custom Endpoint Initialized\r\n");
}

/*********************************************************************
 * @fn      custom_endpoint_send
 *
 * @brief   Send data through custom endpoint (31-byte payload + Report ID)
 *
 * @param   data - pointer to data buffer
 *          len  - data length (max 31 bytes - Report ID automatically added)
 *
 * @return  Status (0=success, 1=error)
 */
uint8_t custom_endpoint_send(uint8_t *data, uint16_t len)
{
  if (len > DEF_ENDP_SIZE_CUSTOM - 1)
  {
    len = DEF_ENDP_SIZE_CUSTOM - 1; // Reserve space for Report ID
  }

  // Send via USB endpoint (Report ID will be automatically added)
  return USBD_SendCustomData(data, len);
}

/*********************************************************************
 * @fn      custom_endpoint_receive
 *
 * @brief   Receive data from custom endpoint
 *
 * @param   data    - pointer to data buffer
 *          max_len - maximum data length
 *
 * @return  Received data length
 */
uint16_t custom_endpoint_receive(uint8_t *data, uint16_t max_len)
{
  uint16_t received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));

  if (received_len > 0 && received_len <= max_len)
  {
    memcpy(data, rx_buffer, received_len);
    return received_len;
  }

  return 0;
}

/*********************************************************************
 * @fn      custom_endpoint_process
 *
 * @brief   Process received custom endpoint data (auto-detect format)
 *
 * @return  None
 */
void custom_endpoint_process(void)
{
  uint16_t received_len = USBD_GetCustomData(rx_buffer, sizeof(rx_buffer));

  if (received_len == 0)
    return;

  PRINT("Custom EP: Received %d bytes\r\n", received_len);

  // Try to detect if this is a structured packet or raw data
  if (received_len >= CUSTOM_CMD_HEADER_SIZE)
  {
    custom_packet_t *packet = (custom_packet_t *)rx_buffer;

    // Simple heuristic: check if first byte looks like a command
    if (packet->cmd >= CUSTOM_CMD_GET_VERSION && packet->cmd <= CUSTOM_CMD_RESET)
    {
      // Process as structured packet
      PRINT("Custom EP: Structured packet - cmd=0x%02X, len=%d, seq=%d\r\n",
            packet->cmd, packet->len, packet->seq);

      switch (packet->cmd)
      {
      case CUSTOM_CMD_GET_VERSION:
      {
        uint8_t version_data[4] = {1, 0, 0, 1}; // v1.0.0.1
        custom_endpoint_send_packet(CUSTOM_CMD_GET_VERSION, version_data, 4);
        break;
      }

      case CUSTOM_CMD_GET_STATUS:
      {
        uint8_t status = 0x01; // OK status
        custom_endpoint_send_packet(CUSTOM_CMD_GET_STATUS, &status, 1);
        break;
      }

      case CUSTOM_CMD_SEND_DATA:
      {
        // Echo received data back
        packet->seq++; // Increment sequence for response
        USBD_SendCustomData((uint8_t *)packet, CUSTOM_DATA_PACKET_SIZE);
        break;
      }

      case CUSTOM_CMD_RESET:
      {
        PRINT("Custom EP: Reset command received\r\n");
        custom_endpoint_init();
        break;
      }

      default:
      {
        PRINT("Custom EP: Unknown command 0x%02X\r\n", packet->cmd);
        break;
      }
      }
    }
    else
    {
      // Process as raw data
      PRINT("Custom EP: Raw data received, first bytes: %02X %02X %02X %02X\r\n",
            rx_buffer[0], rx_buffer[1], rx_buffer[2], rx_buffer[3]);
    }
  }
  else
  {
    // Short data, treat as raw
    PRINT("Custom EP: Short raw data received\r\n");
  }

  // Call application callback if defined
  if (custom_data_received_callback)
  {
    custom_packet_t *packet = (custom_packet_t *)rx_buffer;
    custom_data_received_callback(packet);
  }
}

/*********************************************************************
 * @fn      custom_endpoint_send_raw
 *
 * @brief   Send raw data without packet structure (for advanced users)
 *
 * @param   data - pointer to raw data buffer
 *          len  - data length (max 32 bytes)
 *
 * @return  Status (0=success, 1=error)
 */
uint8_t custom_endpoint_send_raw(uint8_t *data, uint16_t len)
{
  if (len > CUSTOM_DATA_PACKET_SIZE)
  {
    len = CUSTOM_DATA_PACKET_SIZE;
  }

  return USBD_SendCustomData(data, len);
}

/*********************************************************************
 * @fn      custom_endpoint_send_packet
 *
 * @brief   Send data using structured packet format
 *
 * @param   cmd  - command byte
 *          data - pointer to data buffer
 *          len  - data length (max 28 bytes for payload)
 *
 * @return  Status (0=success, 1=error)
 */
uint8_t custom_endpoint_send_packet(uint8_t cmd, uint8_t *data, uint16_t len)
{
  custom_packet_t packet = {0};

  if (len > sizeof(packet.data))
  {
    len = sizeof(packet.data); // Limit to available payload space
  }

  // Build packet
  packet.cmd = cmd;
  packet.len = len;
  packet.seq = sequence_number++;
  if (data && len > 0)
  {
    memcpy(packet.data, data, len);
  }

  // Send via USB endpoint
  return USBD_SendCustomData((uint8_t *)&packet, CUSTOM_DATA_PACKET_SIZE);
}