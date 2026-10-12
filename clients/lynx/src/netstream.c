#include "netstream.h"
#include "frame.h"
#include "protocol.h"

#include <lynx.h>
#include <serial.h>
#include <string.h>

#define FUJI_DEVICE 0x70
#define FUJI_NETSTREAM 0xf0
#define FUJI_ACK 0x06
#define FUJI_NAK 0x15
#define RX_RING_CAP 128
#define HELLO_RETRY_FRAMES 6
#define HELLO_DEADLINE_FRAMES 600
#define SERIAL_ERROR_MASK (SER_STATUS_PE | SER_STATUS_FE | SER_STATUS_OE)

struct link_stats link_stats;
enum link_state link_state = LINK_STARTING;

static struct frame_rx frame_rx;
static uint8_t rx_ring[RX_RING_CAP];
static uint8_t rx_read;
static uint8_t rx_write;
static uint8_t rx_count;
static uint16_t handshake_frames;
static uint8_t hello_cooldown;

static uint8_t command_put(uint8_t byte)
{
    uint16_t attempts = 4000;
    while (attempts--) {
        if (ser_put((char)byte) == SER_ERR_OK)
            return 1;
    }
    return 0;
}

static int command_ack(void)
{
    uint16_t attempts = 30000;
    char byte;
    while (attempts--) {
        if (ser_get(&byte) == SER_ERR_OK) {
            if ((uint8_t)byte == FUJI_ACK)
                return 1;
            if ((uint8_t)byte == FUJI_NAK)
                return 0;
        }
    }
    return -1;
}

static uint8_t enable_raw_tcp(const char *host, uint16_t port)
{
    uint8_t payload[68];
    uint8_t len = 0;
    uint8_t checksum = 0;
    uint8_t i;
    char stale;
    /* Only stale command-mode bytes are discarded. Never drain after raw
       mode starts: that can eat the server's first reply. */
    for (i = 0; i < 128 && ser_get(&stale) == SER_ERR_OK; ++i) {}
    payload[len++] = FUJI_NETSTREAM;
    payload[len++] = (uint8_t)port;
    payload[len++] = (uint8_t)(port >> 8);
    while (*host)
        payload[len++] = (uint8_t)*host++;
    payload[len++] = 0;
    payload[len++] = 0x01;
    if (!command_put(FUJI_DEVICE) || !command_put(0) ||
        !command_put(len))
        return 0;
    for (i = 0; i < len; ++i) {
        checksum ^= payload[i];
        if (!command_put(payload[i]))
            return 0;
    }
    if (!command_put(checksum))
        return 0;
    return command_ack() == 1 && command_ack() == 1;
}

static void handle_frame(const uint8_t *payload, uint8_t len)
{
    if (len == 5 && payload[0] == 0x47 && game_receive(payload, len)) {
        link_stats.round_id = payload[2];
        link_stats.round_phase = payload[3];
        link_stats.kill_limit = payload[4];
        link_state = LINK_WELCOME;
    } else if (len == 3 && payload[0] == 0x48) {
        link_stats.reject_reason = payload[2];
        link_state = LINK_REJECT;
    } else if (link_state == LINK_WELCOME) {
        game_receive(payload, len);
        link_stats.round_id = game.round_id;
        link_stats.round_phase = game.phase;
        link_stats.kill_limit = game.kill_limit;
    }
}

static void parse_ring(void)
{
    uint8_t payload[FRAME_PAYLOAD_MAX];
    uint8_t len;
    uint8_t byte;
    int result;
    while (rx_count) {
        byte = rx_ring[rx_read++];
        if (rx_read == RX_RING_CAP)
            rx_read = 0;
        --rx_count;
        result = frame_feed(&frame_rx, byte, payload, sizeof(payload), &len);
        if (result == 1) {
            ++link_stats.valid_frames;
            handle_frame(payload, len);
        } else if (result < 0) {
            ++link_stats.bad_frames;
            link_stats.last_frame_error = frame_rx.last_error;
            if (frame_rx.last_error == FRAME_ERROR_CRC)
                ++link_stats.crc_errors;
        }
    }
}

void netstream_poll(void)
{
    uint8_t pass;
    uint8_t budget;
    uint8_t status;
    uint8_t result;
    char byte;
    if (link_state == LINK_STARTING || link_state == LINK_COMMAND_ERROR ||
        link_state == LINK_SERIAL_ERROR)
        return;
    for (pass = 0; pass < 2; ++pass) {
        budget = 96;
        while (budget-- && rx_count < RX_RING_CAP) {
            result = ser_get(&byte);
            if (result == SER_ERR_NO_DATA)
                break;
            if (result != SER_ERR_OK) {
                ++link_stats.serial_errors;
                break;
            }
            rx_ring[rx_write++] = (uint8_t)byte;
            if (rx_write == RX_RING_CAP)
                rx_write = 0;
            ++rx_count;
            ++link_stats.rx_bytes;
        }
        if (rx_count == RX_RING_CAP)
            ++link_stats.rx_overflows;
        parse_ring();
    }
    if (ser_status(&status) == SER_ERR_OK) {
        status &= SERIAL_ERROR_MASK;
        if (status && status != link_stats.serial_status)
            ++link_stats.serial_errors;
        link_stats.serial_status |= status;
    }
}

static uint8_t send_payload(const uint8_t *payload, uint8_t payload_len)
{
    uint8_t wire[FRAME_WIRE_MAX];
    uint8_t length;
    uint8_t i;
    uint16_t attempts;
    length = frame_encode(payload, payload_len, wire, sizeof(wire));
    if (!length)
        return 0;
    for (i = 0; i < length; ++i) {
        attempts = 512;
        while (ser_put((char)wire[i]) != SER_ERR_OK) {
            netstream_poll();
            if (--attempts == 0) {
                /* Abandoned COBS bytes must not merge with the next retry. */
                attempts = 512;
                while (ser_put(0) != SER_ERR_OK && --attempts) {}
                ++link_stats.tx_bytes;
                return 0;
            }
        }
        ++link_stats.tx_bytes;
        netstream_poll();
    }
    return 1;
}

static uint8_t send_hello(void)
{
    static const uint8_t hello[2] = { 0x46, 1 };
    if (!send_payload(hello, sizeof(hello)))
        return 0;
    ++link_stats.hello_attempts;
    return 1;
}

uint8_t netstream_start(const char *host, uint16_t port)
{
    const struct ser_params params = {
        SER_BAUD_62500, SER_BITS_8, SER_STOP_1, SER_PAR_ODD, SER_HS_NONE
    };
    size_t host_len;
    size_t host_index;
    memset(&link_stats, 0, sizeof(link_stats));
    game_reset();
    frame_reset(&frame_rx);
    rx_read = rx_write = rx_count = 0;
    handshake_frames = 0;
    hello_cooldown = 0;
    link_state = LINK_STARTING;
    if (!host || !port) {
        link_state = LINK_COMMAND_ERROR;
        return 0;
    }
    host_len = strlen(host);
    if (host_len == 0 || host_len > 62) {
        link_state = LINK_COMMAND_ERROR;
        return 0;
    }
    for (host_index = 0; host_index < host_len; ++host_index) {
        char c = host[host_index];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
            link_state = LINK_COMMAND_ERROR;
            return 0;
        }
    }
    if (ser_install(lynx_comlynx_ser) != SER_ERR_OK) {
        link_state = LINK_SERIAL_ERROR;
        return 0;
    }
    if (ser_open(&params) != SER_ERR_OK) {
        ser_uninstall();
        link_state = LINK_SERIAL_ERROR;
        return 0;
    }
    if (!enable_raw_tcp(host, port)) {
        link_state = LINK_COMMAND_ERROR;
        return 0;
    }
    link_state = LINK_WAIT_WELCOME;
    return 1;
}

void netstream_tick(void)
{
    uint8_t payload[11];
    uint8_t len;
    uint8_t i;
    netstream_poll();
    if (link_state == LINK_WELCOME) {
        game_step();
        for (i = 0; i < 2; ++i) {
            len = game_next_outbound(payload, sizeof(payload));
            if (!len) break;
            if (!send_payload(payload, len)) {
                link_state = LINK_TX_ERROR;
                break;
            }
        }
        return;
    }
    if (link_state != LINK_WAIT_WELCOME)
        return;
    if (++handshake_frames >= HELLO_DEADLINE_FRAMES) {
        link_state = LINK_TIMEOUT;
        return;
    }
    if (hello_cooldown)
        --hello_cooldown;
    if (!hello_cooldown) {
        if (!send_hello()) {
            link_state = LINK_TX_ERROR;
            return;
        }
        hello_cooldown = HELLO_RETRY_FRAMES;
    }
}
