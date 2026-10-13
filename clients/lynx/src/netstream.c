#include "netstream.h"
#include "frame.h"
#include "protocol.h"
#include "command.h"

#ifdef LYNX_NETSTREAM_HOST
#include "serial_host.h"
#else
#include <lynx.h>
#include <serial.h>
#endif
#include <string.h>

#define FUJI_DEVICE 0x70
#define FUJI_NETSTREAM 0xf0
#define FUJI_ACK 0x06
#define FUJI_NAK 0x15
#define RX_RING_CAP 128
#define HELLO_RETRY_TICKS 6
#define HELLO_DEADLINE_TICKS 600
#define STALE_TICKS 180 /* Three seconds without authoritative progress. */
#define REJOIN_TICKS 600 /* Short gaps may resume without retiring the seat. */
#define REJOIN_DEADLINE_TICKS 1800
#define REJOIN_SEND_TICKS 30
#define RX_FRAME_WAIT_TICKS 2 /* Longest wire frame is under 12 ms at 62500. */
#ifndef RX_QUIET_TICKS
#define RX_QUIET_TICKS 2 /* At least one whole VBL without received bytes. */
#endif
/* lynx-comlynx.ser returns raw Mikey bits plus software RX overflow, not
   the generic serial.h status layout. Include RX break, exclude PARBIT. */
#define SERIAL_ERROR_MASK 0x9e

struct link_stats link_stats;
uint8_t link_state = LINK_STARTING;

static struct frame_rx frame_rx;
static uint8_t rx_ring[RX_RING_CAP];
static uint8_t rx_read;
static uint8_t rx_write;
static uint8_t rx_count;
static uint16_t phase_at, hello_at, last_valid_at, last_snapshot_at;
static uint8_t rejoin_leave;
static uint16_t last_rx_at;
static uint8_t rx_seen;
static uint16_t quiet_counted;

static uint8_t enable_raw_tcp(const char *host, uint16_t port)
{
    uint8_t payload[68];
    uint8_t len = 0;
    payload[len++] = FUJI_NETSTREAM;
    payload[len++] = (uint8_t)port;
    payload[len++] = (uint8_t)(port >> 8);
    while (*host) payload[len++] = (uint8_t)*host++;
    payload[len++] = 0;
    payload[len++] = 0x01;
    if (!command_complete(FUJI_DEVICE, payload, len)) return 0;
    command_enter_raw();
    return 1;
}

static void handle_frame(const uint8_t *payload, uint8_t len)
{
    uint8_t phase = game.phase, round = game.round_id;
    uint16_t snapshots = game.snapshots;
    uint16_t now = command_clock();
    if (len == 5 && payload[0] == 0x47 && game_receive(payload, len)) {
        link_state = LINK_WELCOME;
        phase_at = last_valid_at = last_snapshot_at = now;
    } else if (len == 3 && payload[0] == 0x48 && payload[1] == 1 &&
               (link_state == LINK_WAIT_WELCOME || link_state == LINK_REJOIN)) {
        link_stats.reject_reason = payload[2];
        link_state = LINK_REJECT;
    } else if ((link_state == LINK_WELCOME || link_state == LINK_STALE) &&
               game_receive(payload, len)) {
        last_valid_at = now;
        /* Results intentionally stop snapshots. Give a new round its own
           bootstrap window instead of timing it from the previous round. */
        if (game.phase != phase || game.round_id != round) last_snapshot_at = now;
        /* A stream of snapshots cannot finish a missing reliable bootstrap. */
        if (game.snapshots != snapshots && game.round_authorized && game.map_ready)
            last_snapshot_at = now;
        if (link_state == LINK_STALE &&
            ((game.snapshots != snapshots && game.round_authorized && game.map_ready) ||
             (game.phase && game.result_valid))) {
            game.link_paused = 0;
            link_state = LINK_WELCOME;
        }
    }
    link_stats.round_id = game.round_id;
    link_stats.round_phase = game.phase;
    link_stats.kill_limit = game.kill_limit;
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
    uint8_t received;
    char byte;
    if (link_state == LINK_STARTING || link_state == LINK_COMMAND_ERROR ||
        link_state == LINK_SERIAL_ERROR)
        return;
    for (pass = 0; pass < 2; ++pass) {
        budget = 96;
        received = 0;
        while (budget-- && rx_count < RX_RING_CAP) {
            result = ser_get(&byte);
            if (result == SER_ERR_NO_DATA)
                break;
            if (result != SER_ERR_OK) {
                ++link_stats.serial_api_errors;
                break;
            }
            rx_ring[rx_write++] = (uint8_t)byte;
            if (rx_write == RX_RING_CAP)
                rx_write = 0;
            ++rx_count;
            ++link_stats.rx_bytes;
            received = 1;
        }
        if (received) { last_rx_at = command_clock(); rx_seen = 1; }
        if (rx_count == RX_RING_CAP)
            ++link_stats.rx_overflows;
        parse_ring();
    }
    if (ser_status(&status) == SER_ERR_OK) {
        status &= SERIAL_ERROR_MASK;
        if (status & (uint8_t)~link_stats.serial_status)
            ++link_stats.serial_errors;
        link_stats.serial_status |= status;
    } else ++link_stats.serial_api_errors;
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
            if (--attempts == 0) {
                /* Abandoned COBS bytes must not merge with the next retry. */
                attempts = 512;
                while (ser_put(0) != SER_ERR_OK && --attempts) {}
                ++link_stats.tx_bytes;
                return 0;
            }
        }
        ++link_stats.tx_bytes;
    }
    /* Queue a complete frame without interleaving CRC/parsing work. Mikey's
       driver disables RX while transmitting; byte-wise parsing can empty
       the TX ring and split one COBS frame into separate wire turns. */
    netstream_poll();
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
    size_t host_len;
    size_t host_index;
    memset(&link_stats, 0, sizeof(link_stats));
    game_reset();
    frame_reset(&frame_rx);
    rx_read = rx_write = rx_count = 0;
    phase_at = last_valid_at = last_snapshot_at = command_clock();
    hello_at = phase_at - HELLO_RETRY_TICKS;
    rejoin_leave = 1;
    last_rx_at = 0;
    rx_seen = 0;
    quiet_counted = command_clock() - 1;
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
    if (!command_open()) {
        link_state = LINK_SERIAL_ERROR;
        return 0;
    }
    if (!enable_raw_tcp(host, port)) {
        link_state = LINK_COMMAND_ERROR;
        return 0;
    }
    /* Command-mode handoff time is separate from the HELLO deadline. */
    phase_at = last_valid_at = last_snapshot_at = command_clock();
    hello_at = phase_at - HELLO_RETRY_TICKS;
    link_state = LINK_WAIT_WELCOME;
    return 1;
}

/* Drop unsent/predicted actions at the freshness boundary. A new WELCOME
   resets all session sequence/reliable state but preserves configured identity. */
static void pause_game(void)
{
    game.link_paused = 1;
    game.pending_count = game.intent_count = game.fire_cue_frames = 0;
    game.input_sent = 0;
}

void netstream_retry(void)
{
    if (link_state != LINK_STALE && link_state != LINK_LOST &&
        link_state != LINK_TIMEOUT && link_state != LINK_REJECT &&
        link_state != LINK_TX_ERROR) return;
    pause_game();
    if (!game.leave_requested) {
        game.leave_seq = game.tx_seq++;
        game.leave_requested = 1;
    }
    phase_at = command_clock();
    hello_at = phase_at - REJOIN_SEND_TICKS;
    rejoin_leave = 1;
    link_state = LINK_REJOIN;
    ++link_stats.rejoins;
}

void netstream_service(void)
{
    uint8_t payload[3 + 2 * GAME_PENDING_INPUTS];
    uint8_t len, i, n;
    uint16_t now, age;
    netstream_poll();
    now = command_clock();
    if (link_state == LINK_WELCOME || link_state == LINK_STALE) {
        age = (uint16_t)(now - (game.phase ? last_valid_at : last_snapshot_at));
        if (age >= STALE_TICKS && link_state == LINK_WELCOME) {
            pause_game(); link_state = LINK_STALE;
        }
        if (age >= REJOIN_TICKS) netstream_retry();
    }
    if (link_state == LINK_WAIT_WELCOME &&
        (uint16_t)(now-phase_at) >= HELLO_DEADLINE_TICKS) link_state = LINK_TIMEOUT;
    if (link_state == LINK_REJOIN &&
        (uint16_t)(now-phase_at) >= REJOIN_DEADLINE_TICKS) link_state = LINK_LOST;
    for (i = 0; i < 2; ++i) {
        if (link_state != LINK_WELCOME && link_state != LINK_STALE &&
            link_state != LINK_WAIT_WELCOME && link_state != LINK_REJOIN) return;
        /* A delimiter is not the end of a half-duplex burst. Apply the same
           quiet/partial-frame gate to gameplay, HELLO and recovery controls. */
        now = command_clock();
        if (rx_seen && (uint16_t)(now - last_rx_at) < RX_QUIET_TICKS) {
            if (quiet_counted != now) {
                ++link_stats.quiet_ticks; quiet_counted = now;
            }
            return;
        }
        if (frame_rx.used || frame_rx.discarding) {
            if ((uint16_t)(now - last_rx_at) < RX_FRAME_WAIT_TICKS) return;
            frame_reset(&frame_rx);
            ++link_stats.bad_frames;
            link_stats.last_frame_error = FRAME_ERROR_TRUNCATED;
        }
        if (link_state == LINK_WAIT_WELCOME || link_state == LINK_REJOIN) {
            if ((uint16_t)(now-hello_at) <
                (link_state == LINK_REJOIN ? REJOIN_SEND_TICKS : HELLO_RETRY_TICKS)) return;
            hello_at = now;
            if (link_state == LINK_REJOIN && rejoin_leave) {
                payload[0] = 0x56; payload[1] = game.leave_seq;
                rejoin_leave = 0;
                if (!send_payload(payload,2)) link_state = LINK_TX_ERROR;
            } else {
                rejoin_leave = 1;
                if (!send_hello()) link_state = LINK_TX_ERROR;
            }
            return;
        }
        if (link_state == LINK_STALE && !game.ack_pending) return;
        if (link_state == LINK_WELCOME && !game.input_history &&
            (uint16_t)(now-phase_at) >= 60) {
            payload[0] = 0x59; payload[1] = 1; len = 2;
            phase_at = now;
        } else len = game_next_outbound(payload, sizeof(payload));
        if (!len) break;
        if (game.input_history && payload[0] == 0x41 && game.pending_count) {
            payload[0] = 0x58; payload[1] = game.round_id;
            payload[2] = game.pending_count; len = 3;
            for (n = 0; n < game.pending_count; ++n) {
                payload[len++] = game.pending_seq[n];
                payload[len++] = game.pending_joy[n];
            }
        }
        if (!send_payload(payload, len)) {
            pause_game(); link_state = LINK_TX_ERROR;
            break;
        }
    }
}

void netstream_tick(void)
{
    netstream_service();
    if (link_state == LINK_WELCOME) game_step();
}
