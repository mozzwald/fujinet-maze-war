#include "frame.h"
#include <string.h>

void frame_reset(struct frame_rx *rx)
{
    rx->used = 0;
    rx->discarding = 0;
    rx->last_error = 0;
}

uint16_t frame_crc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xffff;
    uint8_t i;
    uint8_t bit;
    for (i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t frame_encode(const uint8_t *payload, uint8_t len,
                     uint8_t *wire, uint8_t cap)
{
    uint8_t raw[FRAME_PAYLOAD_MAX + 2];
    uint8_t rd = 0;
    uint8_t wr = 1;
    uint8_t code_at = 0;
    uint8_t code = 1;
    uint16_t crc;
    if (len == 0 || len > FRAME_PAYLOAD_MAX || cap < FRAME_WIRE_MAX)
        return 0;
    memcpy(raw, payload, len);
    crc = frame_crc16(raw, len);
    raw[len++] = (uint8_t)crc;
    raw[len++] = (uint8_t)(crc >> 8);
    while (rd < len) {
        if (raw[rd] == 0) {
            wire[code_at] = code;
            code_at = wr++;
            code = 1;
            ++rd;
        } else {
            wire[wr++] = raw[rd++];
            if (++code == 0xff) {
                wire[code_at] = code;
                code_at = wr++;
                code = 1;
            }
        }
    }
    wire[code_at] = code;
    wire[wr++] = 0;
    return wr;
}

int frame_feed(struct frame_rx *rx, uint8_t byte, uint8_t *out,
               uint8_t cap, uint8_t *out_len)
{
    uint8_t rd = 0;
    uint8_t wr = 0;
    uint8_t code;
    uint8_t i;
    uint16_t crc;
    if (byte != 0) {
        if (!rx->discarding) {
            if (rx->used == FRAME_ENCODED_MAX)
                rx->discarding = 1;
            else
                rx->encoded[rx->used++] = byte;
        }
        return 0;
    }
    if (rx->discarding || rx->used == 0) {
        uint8_t error = rx->discarding ? FRAME_ERROR_OVERLONG : FRAME_ERROR_EMPTY;
        frame_reset(rx);
        rx->last_error = error;
        return -1;
    }
    while (rd < rx->used) {
        code = rx->encoded[rd++];
        if (code == 0 || (uint8_t)(code - 1) > (uint8_t)(rx->used - rd)) {
            frame_reset(rx);
            rx->last_error = FRAME_ERROR_COBS;
            return -1;
        }
        for (i = 1; i < code; ++i)
            rx->encoded[wr++] = rx->encoded[rd++];
        if (code != 0xff && rd < rx->used)
            rx->encoded[wr++] = 0;
    }
    frame_reset(rx);
    if (wr < 3 || wr - 2 > cap) {
        rx->last_error = FRAME_ERROR_LENGTH;
        return -1;
    }
    crc = frame_crc16(rx->encoded, (uint8_t)(wr - 2));
    if (rx->encoded[wr - 2] != (uint8_t)crc ||
        rx->encoded[wr - 1] != (uint8_t)(crc >> 8))
    {
        rx->last_error = FRAME_ERROR_CRC;
        return -1;
    }
    *out_len = (uint8_t)(wr - 2);
    memcpy(out, rx->encoded, *out_len);
    return 1;
}
