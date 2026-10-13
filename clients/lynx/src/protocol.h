#ifndef MAZE_LYNX_PROTOCOL_H
#define MAZE_LYNX_PROTOCOL_H

#include <stdint.h>

#define GAME_PLAYERS 4
#define GAME_NAME_LEN 8
#define GAME_BRICK_BYTES 48
#define GAME_PENDING_INPUTS 8
#define GAME_INPUT_INTENTS 4
#define GAME_REPEAT_DELAY 15 /* 250 ms at the configured 60 Hz display */
#define GAME_REPEAT_PERIOD 6 /* 10 steps/second */
#define GAME_REPEAT_PENDING 2
#define GAME_REPEAT_MAX 4 /* Leave server queue room for turns and release. */
#ifndef GAME_MOTION_POINTS
#define GAME_MOTION_POINTS 4 /* 400 ms observation history; local uses last two. */
#endif
typedef char motion_ring_must_be_power_of_two[(GAME_MOTION_POINTS >= 2 &&
    GAME_MOTION_POINTS <= 8 && !(GAME_MOTION_POINTS & (GAME_MOTION_POINTS-1))) ? 1 : -1];
#define GAME_MOVE_TICKS 6
#define GAME_PRESS_TICKS 3 /* Fast initial tap/turn; held steps stay 100 ms. */
#define GAME_REMOTE_DELAY 6 /* One observed 10 Hz interval; never extrapolate. */
#define GAME_SHOT_DELAY 3 /* Shorter delay; clears/hits remain immediate. */
#define GAME_RETRY_MIN 30 /* 500 ms; at most two retry frames/second. */
#define GAME_RETRY_MAX 120 /* Cap recovery timeout at two seconds. */
struct game_motion {
    uint16_t serial;
    uint16_t at[GAME_MOTION_POINTS];
    uint8_t ticks[GAME_MOTION_POINTS];
    uint8_t epoch;
    uint8_t x[GAME_MOTION_POINTS], y[GAME_MOTION_POINTS];
};
#define GAME_DEATH_FRAME_TICKS 8
#define GAME_DEATH_TICKS (4 * GAME_DEATH_FRAME_TICKS)
#define GAME_WINNER_DANCE_TICKS 300 /* Atari's five-second celebration */
enum { ROUND_NONE, ROUND_LOSERS, ROUND_DANCE, ROUND_WINNER_SMOKE, ROUND_RESULTS };
/* Latched presentation events; the sound service consumes them between batches. */
#define GAME_SOUND_STEP 1
#define GAME_SOUND_SHOT 2
#define GAME_SOUND_DEATH 4
#define GAME_SOUND_SPAWN 8
#define GAME_SOUND_IMPACT 32

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
    uint8_t previous_x, previous_y, segment;
    uint16_t at, previous_at;
};

/* Presentation only: this location never participates in occupancy. */
struct game_death {
    uint16_t started;
    uint8_t x, y, active;
};

/* Shared prefix lets rendering snapshot only the fields it reads. */
#define GAME_VIEW_FIELDS \
    struct game_player players[GAME_PLAYERS]; \
    struct game_shot shots[GAME_PLAYERS]; \
    struct game_death deaths[GAME_PLAYERS]; \
    uint8_t bricks[GAME_BRICK_BYTES]; \
    struct game_motion motion[GAME_PLAYERS]; \
    uint8_t seat_mask; \
    uint8_t zombie_mask; \
    uint8_t local_pid; \
    uint8_t round_id; \
    uint8_t phase; \
    uint8_t map_ready; \
    uint8_t snapshot_ready; \
    uint8_t input_joy; \
    uint8_t facing_joy[GAME_PLAYERS]; /* presentation: last non-neutral direction */ \
    uint8_t fire_cue_frames; \
    uint8_t winner_pid; \
    uint8_t result_valid; \
    uint16_t input_clock; \
    uint16_t result_started;

struct game_view {
    GAME_VIEW_FIELDS
};

struct game_state {
    GAME_VIEW_FIELDS
    uint8_t names[GAME_PLAYERS][GAME_NAME_LEN];
    uint8_t local_name[GAME_NAME_LEN];
    uint8_t pending_snapshot[21];
    uint16_t reliable_rev;
    uint16_t packets_applied;
    uint16_t packets_rejected;
    uint16_t snapshots;
    uint16_t ack_sent;
    uint8_t have_welcome;
    uint8_t link_paused;
    uint8_t kill_limit;
    uint8_t result_zombie_history;
    uint8_t sound_events[GAME_PLAYERS];
    uint8_t sound_brick;
    uint8_t round_authorized;
    uint8_t pending_snapshot_valid;
    uint8_t ack_pending;
    uint8_t delta_due;
    uint8_t name_echoed;
    uint8_t name_age;
    uint8_t heartbeat_age;
    uint8_t tx_seq;
    uint8_t leave_seq;
    uint8_t leave_requested;
    uint8_t leave_acked;
    uint8_t pending_count;
    uint8_t pending_seq[GAME_PENDING_INPUTS];
    uint8_t pending_joy[GAME_PENDING_INPUTS];
    uint16_t pending_at[GAME_PENDING_INPUTS];
    uint16_t ack_delay, ack_last, ack_samples, repeat_stalls, stall_at;
    uint16_t intent_dropped;
    uint16_t retry_at, input_retries;
    uint16_t local_corrections;
    uint8_t repeat_window, pending_highwater;
    uint16_t repeat_at;
    uint16_t last_input_at;
    uint8_t sent_joy;
    uint8_t input_sent;
    uint8_t intent_count;
    uint8_t intents[GAME_INPUT_INTENTS];
    uint8_t direction_mask;
    uint8_t input_history;
};
#undef GAME_VIEW_FIELDS

extern struct game_state game;
/* Last model disagreement; packed positions are $YYXX, ACK is $pending/seq. */
enum { TRACE_BEFORE, TRACE_SERVER, TRACE_EXPECTED, TRACE_REPLAYED, TRACE_ACK,
       TRACE_RESPAWN_IGNORED, TRACE_DETAIL, TRACE_WORDS };
extern uint16_t reconcile_trace[TRACE_WORDS];

void game_reset(void);
void game_set_name(const char *name);
/* Returns 1 for structurally valid payload, 0 for rejected payload. */
uint8_t game_receive(const uint8_t *packet, uint8_t len);
uint8_t game_ready(void);
void game_step(void);
/* Feed the 60 Hz VBL clock before sampling buttons; elapsed render time counts. */
void game_input_tick(uint16_t ticks);
uint8_t game_round_stage(const struct game_state *state);
/* Direction bits: Up=1, Right=2, Down=4, Left=8; newly pressed axis wins. */
void game_sample_input(uint8_t directions, uint8_t fire, uint16_t ticks);
void game_set_joy(uint8_t joy);
/* Emits at most one ACK, NAME, or DELTA payload per call. */
uint8_t game_next_outbound(uint8_t *out, uint8_t cap);
/* Builds one game-level leave request. Does not stop FujiNet raw mode. */
uint8_t game_request_leave(uint8_t *out, uint8_t cap);

#endif
