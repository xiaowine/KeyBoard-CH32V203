#ifndef COMM_CONTROLLER_H
#define COMM_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>
#include "utils.h"

#define CRC_WORD_SIZE ((offsetof(FrameData, crc) + 3u) / 4u)
#define CRC_BYTES_SIZE offsetof(FrameData, crc)
#define FRAME_PAYLOAD_DATA_SIZE (sizeof(((FrameData *)0)->payload.data))
#define RETRY_MAX_CNT 10u
#define FRAME_RECV_MAX_BYTES 4096u
#define SEQ_MAX_NUM 100u

typedef enum
{
    FRAME_TYPE_ERROR = 0u,
    FRAME_TYPE_START = 1u,
    FRAME_TYPE_DATA = 2u,
    FRAME_TYPE_ACK = 3u,
    FRAME_TYPE_NACK = 4u
} FRAME_TYPE;

typedef enum
{
    DATA_TYPE_SET_LAYER_KEYMAP = 0u,
    DATA_TYPE_GET_KEY = 1u,
    DATA_TYPE_GET_LAYER_KEYMAP = 2u,
    DATA_TYPE_GET_ALL_LAYER_KEYMAP = 3u,
    DATA_TYPE_SET_LAYER = 4u
} DATA_TYPE;

typedef enum
{
    SEND_STATUS_IDLE = 0u,
    SEND_STATUS_WAIT_RESPONSE = 1u,
    SEND_STATUS_FRAME = 2u,
    SEND_STATUS_RETRY = 3u,
} SEND_STATUS;

typedef struct PACKED
{
    uint8_t seq_num;
    uint8_t type;
    uint8_t payload_length;

    struct
    {
        uint8_t type;
        uint8_t data[56];
    } payload;

    uint32_t crc;
} FrameData;

typedef struct PACKED
{
    FrameData frame_data;
    uint8_t last_sqe_num;
    uint8_t retry_count;
    uint16_t expected_payload_len;
    uint16_t received_payload_len;
    uint8_t* payload_buf;
    uint8_t need_ack;
} ReceiveHandle;

typedef struct PACKED
{
    FrameData frame_data;
    uint8_t last_sqe_unm;
    SEND_STATUS status;
    SEND_STATUS last_status;
    uint8_t retry_count;
    uint8_t send_pending;
    uint8_t receive_ack;
} SendHandle;

void comm_controller_process();

#endif
