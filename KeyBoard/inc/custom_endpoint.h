/********************* Custom Endpoint Header File **************************
 * File Name          : custom_endpoint.h
 * Author             : Your Name
 * Version            : V1.0.0
 * Date               : 2026/03/02
 * Description        : Header for custom endpoint communication
 ****************************************************************************/
#ifndef __CUSTOM_ENDPOINT_H
#define __CUSTOM_ENDPOINT_H

#include "ch32v20x.h"
#include "usb_desc.h"

/* Custom data packet definitions */
#define CUSTOM_DATA_PACKET_SIZE DEF_ENDP_SIZE_CUSTOM // 32 bytes
#define CUSTOM_CMD_HEADER_SIZE 4                     // Command header size

/* Custom protocol command definitions */
typedef enum
{
  CUSTOM_CMD_GET_VERSION = 0x01, // Get firmware version
  CUSTOM_CMD_SET_CONFIG = 0x02,  // Set device configuration
  CUSTOM_CMD_GET_STATUS = 0x03,  // Get device status
  CUSTOM_CMD_SEND_DATA = 0x04,   // Send custom data
  CUSTOM_CMD_RESET = 0xFF        // Reset command
} custom_cmd_t;

/* Custom data packet structure */
typedef struct
{
  uint8_t cmd;                                                    // Command byte
  uint8_t len;                                                    // Data length
  uint16_t seq;                                                   // Sequence number
  uint8_t data[CUSTOM_DATA_PACKET_SIZE - CUSTOM_CMD_HEADER_SIZE]; // Payload
} custom_packet_t;

/* Function declarations */
void custom_endpoint_init(void);
uint8_t custom_endpoint_send(uint8_t *data, uint16_t len);                     // Simple raw data send
uint8_t custom_endpoint_send_packet(uint8_t cmd, uint8_t *data, uint16_t len); // Structured packet send
uint8_t custom_endpoint_send_raw(uint8_t *data, uint16_t len);                 // Advanced raw send
uint16_t custom_endpoint_receive(uint8_t *data, uint16_t max_len);
void custom_endpoint_process(void);

/* Application callback - implement this in your application */
extern void custom_data_received_callback(custom_packet_t *packet);

#endif /* __CUSTOM_ENDPOINT_H */