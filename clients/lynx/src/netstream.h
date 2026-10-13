#ifndef MAZE_LYNX_NETSTREAM_H
#define MAZE_LYNX_NETSTREAM_H

#include <stdint.h>

enum link_state {
    LINK_STARTING,
    LINK_WAIT_WELCOME,
    LINK_WELCOME,
    LINK_REJECT,
    LINK_TIMEOUT,
    LINK_SERIAL_ERROR,
    LINK_COMMAND_ERROR,
    LINK_TX_ERROR,
    LINK_STALE,
    LINK_REJOIN,
    LINK_LOST
};

struct link_stats {
    uint16_t rx_bytes;
    uint16_t tx_bytes;
    uint16_t valid_frames;
    uint16_t bad_frames;
    uint16_t crc_errors;
    uint8_t last_frame_error;
    uint16_t serial_errors; /* polls introducing previously unseen sticky bits */
    uint16_t serial_api_errors; /* failed get/status calls, not physical error rate */
    uint16_t rejoins;
    uint16_t rx_overflows;
    uint8_t serial_status;
    uint8_t hello_attempts;
    uint8_t round_id;
    uint8_t round_phase;
    uint8_t kill_limit;
    uint8_t reject_reason;
    uint16_t quiet_ticks;
};

extern struct link_stats link_stats;
extern uint8_t link_state;

/* Reuses command-mode serial and issues $70/$F0 with raw TCP flags. */
uint8_t netstream_start(const char *host, uint16_t port);
/* Drain serial and parse as many queued bytes as the bounded budget allows. */
void netstream_poll(void);
/* Send pending actions/ACKs between render batches, without advancing timers. */
void netstream_service(void);
/* Retry the same raw endpoint after a terminal error; never enter command mode. */
void netstream_retry(void);
/* Call once per displayed frame; timers use the independent 60 Hz clock. */
void netstream_tick(void);

#endif
