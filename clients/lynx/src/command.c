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
static uint8_t device_id, opcode, phase, last_byte;
static char failure[24];
const char *command_error_text(void) { return failure; }
static uint8_t failed(uint8_t error)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t values[6], i, status = 0;
#ifndef LYNX_COMMAND_HOST
    ser_status(&status);
#endif
    values[0] = device_id; values[1] = opcode; values[2] = phase;
    values[3] = error; values[4] = last_byte; values[5] = status;
    for (i = 0; i < 6; ++i) {
        failure[i*4] = "DCPEBS"[i];
        failure[i*4+1] = hex[values[i] >> 4];
        failure[i*4+2] = hex[values[i] & 15];
        failure[i*4+3] = ' ';
    }
    failure[23] = 0;
    return 0;
}
/* Whole transaction deadline, including a firmware HTTP/TLS request. */
static uint8_t expired(void)
{
    return (uint16_t)(command_clock() - started) >= 900;
}
static uint8_t put(uint8_t b)
{
    while (!command_io_put(b)) if (expired()) return failed(1);
    return 1;
}
static uint8_t get(uint8_t *b)
{
    while (!command_io_get(b)) if (expired()) return failed(1);
    last_byte = *b;
    return 1;
}
/* Match FujiRealm's command-mode handoff: an empty software RX queue does
   not mean the last byte/ACK has finished crossing the half-duplex wire.
   Wait for 1200 consecutive empty polls, resetting on every trailing byte.
   Keep the whole transaction deadline even if noise never stops. */
static uint8_t drain_until_quiet(void)
{
    uint16_t quiet = 1200;
    uint8_t b;
    while (quiet) {
        if (expired()) return failed(1);
        if (command_io_get(&b)) quiet = 1200;
        else --quiet;
    }
    return 1;
}
static uint8_t wait_for_ack(void)
{
    uint8_t b;
    while (get(&b)) {
        if (b == 6) return 1;
        if (b == 0x15) return failed(2);
        /* ComLynx can leave trailing/echo bytes around a bus handoff.
           Scan only in ACK phases; response lengths/data stay exact. */
    }
    return 0;
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
    uint8_t ck = 0;
    if (raw || !opened || !payload || !len) return 0;
    device_id = device; opcode = payload[0]; phase = 1; last_byte = 0;
    started = command_clock();
    /* Only discard stale command-mode bytes, never live stream bytes. */
    if (!drain_until_quiet()) return 0;
    phase = 2;
    if (!put(device) || !put((uint8_t)(len >> 8)) || !put((uint8_t)len)) return 0;
    for (i = 0; i < len; ++i) {
        ck ^= payload[i];
        if (!put(payload[i])) return 0;
    }
    if (!put(ck)) return 0;
    phase = 3;
    return wait_for_ack();
}
uint8_t command_complete(uint8_t device, const uint8_t *payload, uint16_t len)
{
    if (!begin(device, payload, len)) return 0;
    phase = 4;
    return wait_for_ack();
}
uint8_t command_read(uint8_t device, const uint8_t *payload, uint16_t len,
                     uint8_t *out, uint16_t cap, uint16_t *received)
{
    uint8_t hi, lo, b, ck = 0, got_ck;
    uint16_t count, i;
    *received = 0;
    if (!begin(device, payload, len)) return 0;
    phase = 5;
    if (!get(&hi)) return 0;
    /* A command error is a single NAK, not a response length. */
    if (hi == 0x15) return failed(2);
    if (!get(&lo)) return 0;
    count = ((uint16_t)hi << 8) | lo;
    phase = 6;
    for (i = 0; i < count; ++i) {
        if (!get(&b)) { put(0x15); return 0; }
        ck ^= b;
        if (i < cap) out[i] = b;
    }
    phase = 7;
    if (!get(&got_ck)) { put(0x15); return 0; }
    /* Consume and ACK an oversized but intact reply before returning failure. */
    phase = 8;
    if (!put(ck == got_ck ? 6 : 0x15)) return 0;
    if (ck != got_ck) return failed(3);
    if (count > cap) return failed(4);
    *received = count;
    return 1;
}
