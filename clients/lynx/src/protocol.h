#ifndef MAZE_LYNX_PROTOCOL_H
#define MAZE_LYNX_PROTOCOL_H

#include <stdint.h>

#define GAME_PLAYERS 4
#define GAME_NAME_LEN 8
#define GAME_BRICK_BYTES 48
#define GAME_PENDING_INPUTS 8

struct game_player {
    uint8_t x;
    uint8_t y;
    uint8_t joy;
    uint8_t score;
    uint8_t respawning;
};

struct game_shot {
    uint8_t x;
    uint8_t y;
    uint8_t active;
    uint8_t direction;
};

struct game_state {
    struct game_player players[GAME_PLAYERS];
    struct game_shot shots[GAME_PLAYERS];
    uint8_t names[GAME_PLAYERS][GAME_NAME_LEN];
    uint8_t local_name[GAME_NAME_LEN];
    uint8_t bricks[GAME_BRICK_BYTES];
    uint8_t pending_snapshot[21];
    uint16_t reliable_rev;
    uint16_t packets_applied;
    uint16_t packets_rejected;
    uint16_t snapshots;
    uint16_t ack_sent;
    uint8_t have_welcome;
    uint8_t round_id;
    uint8_t phase;
    uint8_t kill_limit;
    uint8_t local_pid;
    uint8_t seat_mask;
    uint8_t zombie_mask;
    uint8_t winner_pid;
    uint8_t round_authorized;
    uint8_t map_ready;
    uint8_t snapshot_ready;
    uint8_t pending_snapshot_valid;
    uint8_t ack_pending;
    uint8_t delta_due;
    uint8_t name_echoed;
    uint8_t name_age;
    uint8_t heartbeat_age;
    uint8_t tx_seq;
    uint8_t input_joy;
    uint8_t leave_seq;
    uint8_t leave_requested;
    uint8_t leave_acked;
    uint8_t pending_count;
    uint8_t pending_seq[GAME_PENDING_INPUTS];
    uint8_t pending_joy[GAME_PENDING_INPUTS];
    uint8_t fire_cue_frames;
};

extern struct game_state game;

void game_reset(void);
/* Returns 1 for structurally valid payload, 0 for rejected payload. */
uint8_t game_receive(const uint8_t *packet, uint8_t len);
uint8_t game_ready(void);
void game_step(void);
void game_set_joy(uint8_t joy);
/* Emits at most one ACK, NAME, or DELTA payload per call. */
uint8_t game_next_outbound(uint8_t *out, uint8_t cap);
/* Builds one game-level leave request. Does not stop FujiNet raw mode. */
uint8_t game_request_leave(uint8_t *out, uint8_t cap);

#endif
