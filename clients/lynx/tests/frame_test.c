#include "frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t reference_crc(const uint8_t *data, uint8_t len)
{
    uint16_t crc=0xffff;
    uint8_t bit;
    while (len--) {
        crc^=(uint16_t)*data++<<8;
        for(bit=0;bit<8;++bit)
            crc=(crc&0x8000) ? (uint16_t)((crc<<1)^0x1021) : (uint16_t)(crc<<1);
    }
    return crc;
}

static void expect_frame(const uint8_t *payload, uint8_t len)
{
    uint8_t wire[FRAME_WIRE_MAX];
    uint8_t out[FRAME_PAYLOAD_MAX];
    uint8_t out_len = 0;
    uint8_t wire_len;
    uint8_t i;
    struct frame_rx rx;
    frame_reset(&rx);
    wire_len = frame_encode(payload, len, wire, sizeof(wire));
    assert(wire_len > 0);
    for (i = 0; i < wire_len - 1; ++i)
        assert(frame_feed(&rx, wire[i], out, sizeof(out), &out_len) == 0);
    assert(frame_feed(&rx, wire[i], out, sizeof(out), &out_len) == 1);
    assert(out_len == len);
    assert(memcmp(out, payload, len) == 0);
}

int main(void)
{
    static const uint8_t hello[] = { 0x46, 1 };
    static const uint8_t with_zero[] = { 0x47, 1, 0, 0, 25 };
    static const uint8_t check[] = "123456789";
    uint8_t wire[FRAME_WIRE_MAX];
    uint8_t out[FRAME_PAYLOAD_MAX];
    uint8_t len;
    uint8_t wire_len;
    uint8_t i;
    uint8_t max_payload[FRAME_PAYLOAD_MAX];
    struct frame_rx rx;
    assert(frame_crc16(check, 9) == 0x29b1);
    /* Verify the lookup implementation against the old polynomial loop
       across every supported length and all possible first-byte values. */
    {
        unsigned value, n;
        for(value=0;value<256;++value) {
            for(n=0;n<sizeof(max_payload);++n) max_payload[n]=(uint8_t)(value+n*37);
            for(n=0;n<=sizeof(max_payload);++n)
                assert(frame_crc16(max_payload,(uint8_t)n)==reference_crc(max_payload,(uint8_t)n));
        }
    }
    expect_frame(hello, sizeof(hello));
    expect_frame(with_zero, sizeof(with_zero));
    memset(max_payload, 0xff, sizeof(max_payload));
    expect_frame(max_payload, sizeof(max_payload));
    frame_reset(&rx);
    wire_len = frame_encode(hello, sizeof(hello), wire, sizeof(wire));
    wire[2] ^= 0x20;
    for (i = 0; i < wire_len - 1; ++i)
        assert(frame_feed(&rx, wire[i], out, sizeof(out), &len) == 0);
    assert(frame_feed(&rx, 0, out, sizeof(out), &len) == -1);
    assert(rx.last_error == FRAME_ERROR_CRC);
    wire_len = frame_encode(hello, sizeof(hello), wire, sizeof(wire));
    for (i = 0; i < wire_len - 1; ++i)
        assert(frame_feed(&rx, wire[i], out, sizeof(out), &len) == 0);
    assert(frame_feed(&rx, 0, out, sizeof(out), &len) == 1);
    assert(len == sizeof(hello) && memcmp(out, hello, len) == 0);
    frame_reset(&rx);
    for (i = 0; i < FRAME_ENCODED_MAX + 1; ++i)
        assert(frame_feed(&rx, 1, out, sizeof(out), &len) == 0);
    assert(frame_feed(&rx, 0, out, sizeof(out), &len) == -1);
    assert(rx.last_error == FRAME_ERROR_OVERLONG);
    assert(frame_feed(&rx, 3, out, sizeof(out), &len) == 0);
    assert(frame_feed(&rx, 0, out, sizeof(out), &len) == -1);
    assert(rx.last_error == FRAME_ERROR_COBS);
    wire_len = frame_encode(hello, sizeof(hello), wire, sizeof(wire));
    for (i = 0; i < wire_len - 1; ++i)
        assert(frame_feed(&rx, wire[i], out, sizeof(out), &len) == 0);
    assert(frame_feed(&rx, 0, out, sizeof(out), &len) == 1);
    assert(len == sizeof(hello) && memcmp(out, hello, len) == 0);
    puts("frame tests passed");
    return 0;
}
