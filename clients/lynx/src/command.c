#include "command.h"
#ifdef LYNX_COMMAND_HOST
extern uint8_t command_io_open(void);
extern uint8_t command_io_get(uint8_t *b);
extern uint8_t command_io_put(uint8_t b);
#else
#include <lynx.h>
#include <serial.h>
#include <time.h>
static uint8_t command_io_open(void)
{
    const struct ser_params params = {
        SER_BAUD_62500, SER_BITS_8, SER_STOP_1, SER_PAR_ODD, SER_HS_NONE
    };
    if (ser_install(lynx_comlynx_ser) != SER_ERR_OK) return 0;
    if (ser_open(&params) == SER_ERR_OK) return 1;
    ser_uninstall();
    return 0;
}
static uint8_t command_io_get(uint8_t *b)
{
    return ser_get((char *)b) == SER_ERR_OK;
}
static uint8_t command_io_put(uint8_t b)
{
    return ser_put((char)b) == SER_ERR_OK;
}
uint16_t command_clock(void) { return (uint16_t)clock(); }
#endif

static uint8_t opened, raw;
static uint16_t started;
/* Whole transaction deadline, including a firmware HTTP/TLS request. */
static uint8_t expired(void)
{
    return (uint16_t)(command_clock() - started) >= 900;
}
static uint8_t put(uint8_t b)
{
    while (!command_io_put(b)) if (expired()) return 0;
    return 1;
}
static uint8_t get(uint8_t *b)
{
    while (!command_io_get(b)) if (expired()) return 0;
    return 1;
}
uint8_t command_open(void)
{
    if (raw) return 0;
    if (!opened) opened = command_io_open();
    return opened;
}
void command_enter_raw(void) { raw = 1; }
static uint8_t begin(uint8_t device, const uint8_t *payload, uint16_t len)
{
    uint16_t i;
    uint8_t ck = 0, b;
    if (raw || !opened || !payload || !len) return 0;
    started = command_clock();
    /* Only discard stale command-mode bytes, never live stream bytes. */
    for (i = 0; i < 512 && command_io_get(&b); ++i) {}
    if (!put(device) || !put((uint8_t)(len >> 8)) || !put((uint8_t)len)) return 0;
    for (i = 0; i < len; ++i) {
        ck ^= payload[i];
        if (!put(payload[i])) return 0;
    }
    if (!put(ck) || !get(&b)) return 0;
    return b == 6;
}
uint8_t command_complete(uint8_t device, const uint8_t *payload, uint16_t len)
{
    uint8_t b;
    return begin(device, payload, len) && get(&b) && b == 6;
}
uint8_t command_read(uint8_t device, const uint8_t *payload, uint16_t len,
                     uint8_t *out, uint16_t cap, uint16_t *received)
{
    uint8_t hi, lo, b, ck = 0, got_ck;
    uint16_t count, i;
    *received = 0;
    if (!begin(device, payload, len) || !get(&hi)) return 0;
    /* A command error is a single NAK, not a response length. */
    if (hi == 0x15 || !get(&lo)) return 0;
    count = ((uint16_t)hi << 8) | lo;
    for (i = 0; i < count; ++i) {
        if (!get(&b)) { put(0x15); return 0; }
        ck ^= b;
        if (i < cap) out[i] = b;
    }
    if (!get(&got_ck)) { put(0x15); return 0; }
    /* Consume and ACK an oversized but intact reply before returning failure. */
    if (!put(ck == got_ck ? 6 : 0x15) || ck != got_ck || count > cap) return 0;
    *received = count;
    return 1;
}
