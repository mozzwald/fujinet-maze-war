#ifndef MAZE_LYNX_FRAME_H
#define MAZE_LYNX_FRAME_H

#include <stdint.h>

#define FRAME_PAYLOAD_MAX 60
#define FRAME_ENCODED_MAX 64
#define FRAME_WIRE_MAX 65

#define FRAME_ERROR_EMPTY 1
#define FRAME_ERROR_OVERLONG 2
#define FRAME_ERROR_COBS 3
#define FRAME_ERROR_LENGTH 4
#define FRAME_ERROR_CRC 5

struct frame_rx {
    uint8_t encoded[FRAME_ENCODED_MAX];
    uint8_t used;
    uint8_t discarding;
    uint8_t last_error;
};

void frame_reset(struct frame_rx *rx);
uint16_t frame_crc16(const uint8_t *data, uint8_t len);
/* Returns wire length or zero for invalid input/capacity. Includes delimiter. */
uint8_t frame_encode(const uint8_t *payload, uint8_t len,
                     uint8_t *wire, uint8_t cap);
/* Returns 1 for a frame, 0 while incomplete, -1 for an invalid frame. */
int frame_feed(struct frame_rx *rx, uint8_t byte, uint8_t *out,
               uint8_t cap, uint8_t *out_len);

#endif
