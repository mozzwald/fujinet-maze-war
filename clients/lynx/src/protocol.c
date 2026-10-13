#include "protocol.h"
#include <string.h>



#define PKT_SNAPSHOT 0x40
#define PKT_DELTA 0x41
#define PKT_SHOT 0x42
#define PKT_NAME 0x43
#define PKT_SEATS 0x44
#define PKT_RELIABLE_ACK 0x45
#define PKT_WELCOME 0x47
#define PKT_BRICK_FULL 0x50
#define PKT_BRICK_DELTA 0x51
#define PKT_RESPAWN 0x52
#define PKT_RELIABLE_EVENT 0x53
#define PKT_MATCH_END 0x54
#define PKT_ROUND_START 0x55
#define PKT_LEAVE_ROOM 0x56
#define PKT_LEAVE_ACK 0x57

struct game_state game;
uint16_t reconcile_trace[TRACE_WORDS];
static uint16_t pending_position[GAME_PENDING_INPUTS];
static uint8_t pending_cause[GAME_PENDING_INPUTS];
static uint16_t acknowledged_detail;
struct respawn_stamp { uint16_t at, cell; uint8_t seq, flags; };
static struct respawn_stamp respawn_stamp[GAME_PLAYERS];
static uint8_t respawn_stamped;


/* Disjoint per-function temporaries: foreground-only, assigned before use.
   Explicit globals let cc65 use true zero-page instructions; IRQs never use
   these and no owning function recursively reenters through a callback. */
#ifdef __CC65__
#pragma bss-name (push, "APPZP", "zp")
#endif
static uint8_t joy_ok_stick;
#ifdef __CC65__
#pragma zpsym ("joy_ok_stick")
#endif
static uint8_t name_ok_i;
#ifdef __CC65__
#pragma zpsym ("name_ok_i")
#endif
static uint8_t packet_valid_i;
#ifdef __CC65__
#pragma zpsym ("packet_valid_i")
#endif
static uint8_t round_newer_delta;
#ifdef __CC65__
#pragma zpsym ("round_newer_delta")
#endif
static struct game_motion *motion_append_m;
static uint8_t motion_append_n;
#ifdef __CC65__
#pragma zpsym ("motion_append_m")
#pragma zpsym ("motion_append_n")
#endif
static struct game_player *predict_input_local;
static uint8_t predict_input_stick;
static uint8_t predict_input_x;
static uint8_t predict_input_y;
static uint8_t predict_input_i;
static uint16_t predict_input_cell;
#ifdef __CC65__
#pragma zpsym ("predict_input_local")
#pragma zpsym ("predict_input_stick")
#pragma zpsym ("predict_input_x")
#pragma zpsym ("predict_input_y")
#pragma zpsym ("predict_input_i")
#pragma zpsym ("predict_input_cell")
#endif
static uint8_t replay_pending_i;
#ifdef __CC65__
#pragma zpsym ("replay_pending_i")
#endif
static uint8_t discard_applied_n;
static uint16_t discard_applied_sample;
static uint16_t discard_applied_expected;
#ifdef __CC65__
#pragma zpsym ("discard_applied_n")
#pragma zpsym ("discard_applied_sample")
#pragma zpsym ("discard_applied_expected")
#endif
static struct game_player *start_death_player;
static struct game_death *start_death_death;
#ifdef __CC65__
#pragma zpsym ("start_death_player")
#pragma zpsym ("start_death_death")
#endif
static uint8_t snapshot_apply_i;
static uint8_t snapshot_apply_old_x[GAME_PLAYERS];
static uint8_t snapshot_apply_old_y[GAME_PLAYERS];
static uint16_t snapshot_apply_expected;
#ifdef __CC65__
#pragma zpsym ("snapshot_apply_i")
#pragma zpsym ("snapshot_apply_old_x")
#pragma zpsym ("snapshot_apply_old_y")
#pragma zpsym ("snapshot_apply_expected")
#endif
static uint8_t apply_id;
static uint8_t apply_i;
static uint16_t apply_index;
static struct respawn_stamp *apply_stamp;
#ifdef __CC65__
#pragma zpsym ("apply_id")
#pragma zpsym ("apply_i")
#pragma zpsym ("apply_index")
#pragma zpsym ("apply_stamp")
#endif
static uint8_t game_set_name_i;
#ifdef __CC65__
#pragma zpsym ("game_set_name_i")
#endif
static uint16_t game_receive_revision;
static const uint8_t *game_receive_inner;
static uint8_t game_receive_inner_len;
#ifdef __CC65__
#pragma zpsym ("game_receive_revision")
#pragma zpsym ("game_receive_inner")
#pragma zpsym ("game_receive_inner_len")
#endif
static uint8_t game_input_tick_i;
#ifdef __CC65__
#pragma zpsym ("game_input_tick_i")
#endif
static uint16_t game_round_stage_elapsed;
#ifdef __CC65__
#pragma zpsym ("game_round_stage_elapsed")
#endif
static uint8_t game_sample_input_fresh;
static uint8_t game_sample_input_mask;
static uint8_t game_sample_input_joy;
#ifdef __CC65__
#pragma zpsym ("game_sample_input_fresh")
#pragma zpsym ("game_sample_input_mask")
#pragma zpsym ("game_sample_input_joy")
#endif
static uint16_t retry_pending_timeout;
static uint8_t retry_pending_tail;
#ifdef __CC65__
#pragma zpsym ("retry_pending_timeout")
#pragma zpsym ("retry_pending_tail")
#endif
static uint8_t game_next_outbound_joy;
static uint8_t game_next_outbound_old_x;
static uint8_t game_next_outbound_old_y;
static uint8_t game_next_outbound_explicit_intent;
static uint8_t game_next_outbound_repeats_pending;
static uint8_t game_next_outbound_i;
#ifdef __CC65__
#pragma zpsym ("game_next_outbound_joy")
#pragma zpsym ("game_next_outbound_old_x")
#pragma zpsym ("game_next_outbound_old_y")
#pragma zpsym ("game_next_outbound_explicit_intent")
#pragma zpsym ("game_next_outbound_repeats_pending")
#pragma zpsym ("game_next_outbound_i")
#endif
#ifdef __CC65__
#pragma bss-name (pop)
#endif

static uint16_t local_position(void)
{
    return game.players[game.local_pid].x | ((uint16_t)game.players[game.local_pid].y << 8);
}

static uint8_t coordinate_ok(uint8_t x, uint8_t y)
{
    return (x < 20 && y < 19) || (x == 255 && y == 255);
}

static uint8_t joy_ok(uint8_t joy)
{
    joy_ok_stick = joy & 15;
    return (joy & 0xe0) == 0 &&
           (joy_ok_stick == 7 || joy_ok_stick == 13 || joy_ok_stick == 11 ||
            joy_ok_stick == 14 || joy_ok_stick == 15);
}

static uint8_t name_ok(const uint8_t *name)
{
    for (name_ok_i = 0; name_ok_i < GAME_NAME_LEN; ++name_ok_i) {
        uint8_t c = name[name_ok_i];
        if (c && !(c == ' ' || c == '-' || c == '.' ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

static uint8_t packet_valid(const uint8_t *p, uint8_t len, uint8_t reliable)
{
    switch (p[0]) {
    case PKT_SNAPSHOT:
        if (reliable || len != 21 || !(p[2] & 1)) return 0;
        for (packet_valid_i = 0; packet_valid_i < GAME_PLAYERS; ++packet_valid_i)
            if (!coordinate_ok(p[3 + 2 * packet_valid_i], p[4 + 2 * packet_valid_i]) ||
                !joy_ok(p[11 + packet_valid_i])) return 0;
        return 1;
    case PKT_SHOT:
        return !reliable && len == 7 && p[2] < 4 && !(p[5] & 0xf8) &&
               (!(p[5] & 1) || coordinate_ok(p[3], p[4]));
    case PKT_NAME:
        return len == 11 && p[2] < 4 && name_ok(p + 3);
    case PKT_SEATS:
        return !reliable && len == 3 && !(p[2] & 0xf0);
    case PKT_BRICK_FULL:
        return len == 52 && p[2] == 1;
    case PKT_BRICK_DELTA:
        return len == 5 && p[2] > 0 && p[2] < 19 &&
               p[3] > 0 && p[3] < 18;
    case PKT_RESPAWN:
        return len == 7 && p[2] < 4 && !(p[5] & 0xfc) &&
               (!(p[5] & 2) || (p[3] < 20 && p[4] < 19));
    case PKT_MATCH_END:
        if (!reliable || len != 43 || p[2] >= 4 || p[5] == 0 ||
            p[5] > 10 || (p[3] & 0xf0) || (p[4] & 0xf0) ||
            (p[10] & 0xf0)) return 0;
        for (packet_valid_i = 0; packet_valid_i < GAME_PLAYERS; ++packet_valid_i)
            if (!name_ok(p + 11 + 8 * packet_valid_i)) return 0;
        return 1;
    case PKT_ROUND_START:
        return reliable && len == 3 && p[2] > 0 && p[2] <= 10;
    case PKT_LEAVE_ACK:
        return !reliable && len == 2;
    case 0x59: /* Opt-in input-history support; old servers ignore the offer. */
        return !reliable && len == 2 && p[1] == 1;
    default:
        return 0;
    }
}

static uint8_t round_newer(uint8_t candidate, uint8_t current)
{
    round_newer_delta = (uint8_t)(candidate - current);
    return round_newer_delta != 0 && round_newer_delta < 128;
}

static void motion_reset(uint8_t id)
{
    ++game.motion[id].epoch;
    game.motion[id].serial = 0;
}
static void motion_append(uint8_t id, uint8_t x, uint8_t y, uint8_t ticks)
{
    motion_append_m = &game.motion[id];
    motion_append_n = motion_append_m->serial % GAME_MOTION_POINTS;
    motion_append_m->x[motion_append_n] = x; motion_append_m->y[motion_append_n] = y; motion_append_m->at[motion_append_n] = game.input_clock;
    motion_append_m->ticks[motion_append_n] = ticks;
    ++motion_append_m->serial;
    /* Power-of-two rings retain their indexing when the counter wraps. */
    if (!motion_append_m->serial) motion_append_m->serial = GAME_MOTION_POINTS;
}
static uint8_t predict_input(uint8_t joy)
{
    predict_input_stick = joy & 15;
    if (!game_ready() || game.local_pid >= GAME_PLAYERS) return 1;
    predict_input_local = &game.players[game.local_pid];
    predict_input_local->joy = joy;
    if (predict_input_local->respawning || predict_input_local->x >= 20 || predict_input_local->y >= 19) return 2;
    if (joy & 0x10) return 3;
    if (predict_input_stick == 15) return 4;
    predict_input_x = predict_input_local->x;
    predict_input_y = predict_input_local->y;
    if (predict_input_stick == 14) --predict_input_y;
    else if (predict_input_stick == 7) ++predict_input_x;
    else if (predict_input_stick == 13) ++predict_input_y;
    else if (predict_input_stick == 11) --predict_input_x;
    if (predict_input_x >= 20 || predict_input_y >= 19) return 5;
    predict_input_cell = (uint16_t)predict_input_y * 20 + predict_input_x;
    if (game.bricks[predict_input_cell >> 3] & (1u << (predict_input_cell & 7))) return 6;
    for (predict_input_i = 0; predict_input_i < GAME_PLAYERS; ++predict_input_i) {
        if (predict_input_i != game.local_pid &&
            ((game.seat_mask | game.zombie_mask) & (1u << predict_input_i)) &&
            !game.players[predict_input_i].respawning &&
            game.players[predict_input_i].x == predict_input_x && game.players[predict_input_i].y == predict_input_y) return 7;
    }
    predict_input_local->x = predict_input_x;
    predict_input_local->y = predict_input_y;
    return 0;
}

static void replay_pending(void)
{
    for (replay_pending_i = 0; replay_pending_i < game.pending_count; ++replay_pending_i) {
        pending_cause[replay_pending_i] = predict_input(game.pending_joy[replay_pending_i]);
        pending_position[replay_pending_i] = local_position();
    }
}

static uint16_t discard_applied(uint8_t ack_seq)
{
    discard_applied_n = 0;
    while (discard_applied_n < game.pending_count &&
           (uint8_t)(ack_seq - game.pending_seq[discard_applied_n]) < 128)
        ++discard_applied_n;
    acknowledged_detail = 65535u;
    if (!discard_applied_n) return 65535u;
    acknowledged_detail = game.pending_joy[discard_applied_n-1] | ((uint16_t)pending_cause[discard_applied_n-1] << 8);
    discard_applied_expected = game.pending_seq[discard_applied_n-1] == ack_seq ? pending_position[discard_applied_n-1] : 65535u;
    /* Sample the newest applied input, avoiding an old input's queue time
       when a snapshot acknowledges several entries at once. This measures
       the whole feedback loop, including server tick phase, not just RTT. */
    discard_applied_sample = (uint16_t)(game.input_clock - game.pending_at[discard_applied_n-1]);
    if (discard_applied_sample < 120) {
        if (!game.ack_samples || discard_applied_sample > game.ack_delay) game.ack_delay = discard_applied_sample;
        else game.ack_delay = (game.ack_delay * 7 + discard_applied_sample) / 8;
        if (game.ack_samples != 65535u) ++game.ack_samples;
        game.ack_last = game.input_clock;
        game.repeat_window = game.ack_delay / GAME_REPEAT_PERIOD + 1;
        if (game.repeat_window < GAME_REPEAT_PENDING) game.repeat_window = GAME_REPEAT_PENDING;
        if (game.repeat_window > GAME_REPEAT_MAX) game.repeat_window = GAME_REPEAT_MAX;
    }
    game.pending_count -= discard_applied_n;
    memmove(game.pending_seq, game.pending_seq + discard_applied_n, game.pending_count);
    memmove(game.pending_joy, game.pending_joy + discard_applied_n, game.pending_count);
    memmove(game.pending_at, game.pending_at + discard_applied_n, game.pending_count * sizeof(uint16_t));
    memmove(pending_position, pending_position+discard_applied_n, game.pending_count * sizeof(uint16_t));
    memmove(pending_cause, pending_cause+discard_applied_n, game.pending_count);
    return discard_applied_expected;
}

static void start_death(uint8_t id)
{
    start_death_player = &game.players[id];
    start_death_death = &game.deaths[id];
    /* Pending respawn packets carry 0,0, not the death cell. Capture the
       last live location before hiding the actor. Echoes never restart it. */
    if (start_death_player->respawning) return;
    if (game.snapshot_ready && start_death_player->x < 20 && start_death_player->y < 18 &&
        ((game.seat_mask | game.zombie_mask) & (1u << id))) {
        start_death_death->x = start_death_player->x;
        start_death_death->y = start_death_player->y;
        start_death_death->started = game.input_clock;
        start_death_death->active = 1;
        game.sound_events[id] |= GAME_SOUND_DEATH;
    }
    start_death_player->respawning = 1;
    motion_reset(id);
    start_death_player->x = start_death_player->y = 255;
    if (id == game.local_pid) {
        game.pending_count = 0;
        game.intent_count = 0;
        game.fire_cue_frames = 0;
    }
}

static void snapshot_apply(const uint8_t *p)
{
    snapshot_apply_expected = 65535u;
    for (snapshot_apply_i=0; snapshot_apply_i<GAME_PLAYERS; ++snapshot_apply_i) {
        snapshot_apply_old_x[snapshot_apply_i] = game.players[snapshot_apply_i].x; snapshot_apply_old_y[snapshot_apply_i] = game.players[snapshot_apply_i].y;
    }
    game.local_pid = (p[2] >> 1) & 3;
    game.zombie_mask = (p[2] >> 3) & 15;
    for (snapshot_apply_i = 0; snapshot_apply_i < GAME_PLAYERS; ++snapshot_apply_i) {
        if (p[3 + 2 * snapshot_apply_i] == 255) {
            start_death(snapshot_apply_i);
        } else if (!game.players[snapshot_apply_i].respawning) {
            /* After a death, only explicit final placement can restore
               occupancy. A stale live snapshot must not revive the body. */
            if (!game.snapshot_ready)
                game.sound_events[snapshot_apply_i] |= GAME_SOUND_SPAWN;
            else if (snapshot_apply_i != game.local_pid &&
                     (game.players[snapshot_apply_i].x != p[3 + 2*snapshot_apply_i] || game.players[snapshot_apply_i].y != p[4 + 2*snapshot_apply_i]))
                game.sound_events[snapshot_apply_i] |= GAME_SOUND_STEP;
            game.players[snapshot_apply_i].x = p[3 + 2 * snapshot_apply_i];
            game.players[snapshot_apply_i].y = p[4 + 2 * snapshot_apply_i];
        }
        game.players[snapshot_apply_i].joy = p[11 + snapshot_apply_i];
        /* Remote neutral snapshots stop movement without changing the view.
           Local facing belongs to sampled input, never delayed snapshots. */
        if (snapshot_apply_i != game.local_pid && (p[11 + snapshot_apply_i] & 15) != 15)
            game.facing_joy[snapshot_apply_i] = p[11 + snapshot_apply_i] & 15;
        game.players[snapshot_apply_i].score = p[15 + snapshot_apply_i];
    }
    if (p[2] & 0x80) snapshot_apply_expected = discard_applied(p[19]);
    replay_pending();
    for (snapshot_apply_i=0; snapshot_apply_i<GAME_PLAYERS; ++snapshot_apply_i) {
        if (snapshot_apply_i != game.local_pid) {
            /* Retain actual observations, including stationary snapshots. */
            if (!game.snapshot_ready || game.players[snapshot_apply_i].respawning ||
                snapshot_apply_old_x[snapshot_apply_i] >= 20 || snapshot_apply_old_y[snapshot_apply_i] >= 19) motion_reset(snapshot_apply_i);
            if (!game.players[snapshot_apply_i].respawning)
                motion_append(snapshot_apply_i,game.players[snapshot_apply_i].x,game.players[snapshot_apply_i].y,GAME_MOVE_TICKS);
            continue;
        }
        if (snapshot_apply_old_x[snapshot_apply_i] == game.players[snapshot_apply_i].x && snapshot_apply_old_y[snapshot_apply_i] == game.players[snapshot_apply_i].y) continue;
        if (game.snapshot_ready && snapshot_apply_old_x[snapshot_apply_i] < 20 && snapshot_apply_old_y[snapshot_apply_i] < 19 &&
            !game.players[snapshot_apply_i].respawning) {
            ++game.local_corrections;
            reconcile_trace[TRACE_BEFORE] = snapshot_apply_old_x[snapshot_apply_i] | ((uint16_t)snapshot_apply_old_y[snapshot_apply_i] << 8);
            reconcile_trace[TRACE_SERVER] = p[3+2*snapshot_apply_i] | ((uint16_t)p[4+2*snapshot_apply_i] << 8);
            reconcile_trace[TRACE_EXPECTED] = snapshot_apply_expected;
            reconcile_trace[TRACE_DETAIL] = snapshot_apply_expected == 65535u ? 65535u : acknowledged_detail;
            reconcile_trace[TRACE_REPLAYED] = local_position();
            reconcile_trace[TRACE_ACK] = p[19] | ((uint16_t)game.pending_count << 8);
        }
        motion_reset(snapshot_apply_i); /* local correction never invents a corner */
    }
    if (memcmp(game.names[game.local_pid], game.local_name, GAME_NAME_LEN) == 0)
        game.name_echoed = 1;
    game.snapshot_ready = 1;
    game.heartbeat_age = 0;
    ++game.snapshots;
}

static void apply(const uint8_t *p, uint8_t len, uint8_t reliable)
{
    (void)len;
    switch (p[0]) {
    case PKT_ROUND_START:
        apply_id = p[1];
        if (apply_id == game.round_id || round_newer(apply_id, game.round_id)) {
            if (apply_id != game.round_id || !game.round_authorized) {
                memset(game.shots, 0, sizeof(game.shots));
                memset(game.deaths, 0, sizeof(game.deaths));
                memset(game.facing_joy, 0, sizeof(game.facing_joy));
                /* A new round can use snapshots for its initial placement. */
                for (apply_i = 0; apply_i < GAME_PLAYERS; ++apply_i)
                    game.players[apply_i].respawning = 0;
                game.map_ready = 0;
                /* Unknown cells stay blocked until the initial full map.
                   Deltas received first must survive that map too. */
                memset(game.bricks, 255, sizeof(game.bricks));
                game.snapshot_ready = 0;
                game.pending_count = 0;
                respawn_stamped = 0;
                game.fire_cue_frames = 0;
                game.intent_count = 0;
                game.input_sent = 0;
                for (apply_i=0; apply_i<GAME_PLAYERS; ++apply_i) motion_reset(apply_i);
                game.repeat_window = GAME_REPEAT_PENDING;
                game.ack_samples = game.ack_delay = 0;
                memset(game.sound_events, 0, sizeof(game.sound_events));
                game.sound_brick = 0;
            }
            game.round_id = apply_id;
            game.kill_limit = p[2];
            game.phase = 0;
            game.result_valid = 0;
            game.round_authorized = 1;
            if (game.pending_snapshot_valid &&
                game.pending_snapshot[20] == apply_id) {
                snapshot_apply(game.pending_snapshot);
            }
            game.pending_snapshot_valid = 0;
        }
        break;
    case PKT_MATCH_END:
        /* A later reliable revision must not restart or replace the result. */
        if (p[1] != game.round_id || game.result_valid) break;
        game.result_valid = 1;
        game.result_started = game.input_clock;
        game.result_zombie_history = p[10];
        game.phase = 1;
        game.pending_count = 0;
        game.fire_cue_frames = 0;
        game.intent_count = 0;
        game.winner_pid = p[2];
        game.seat_mask = p[3] & (uint8_t)~p[4];
        game.zombie_mask = p[4];
        game.kill_limit = p[5];
        for (apply_id = 0; apply_id < GAME_PLAYERS; ++apply_id)
            game.players[apply_id].score = p[6 + apply_id];
        memcpy(game.names, p + 11, sizeof(game.names));
        memset(game.shots, 0, sizeof(game.shots));
        memset(game.deaths, 0, sizeof(game.deaths));
        break;
    case PKT_BRICK_FULL:
        if (p[51] == game.round_id) {
            /* Within a round bricks only disappear. A delayed reliable map
               must not resurrect a wall cleared by a newer direct event. */
            for (apply_i = 0; apply_i < GAME_BRICK_BYTES; ++apply_i) game.bricks[apply_i] &= p[3+apply_i];
            if (reliable) game.map_ready = 1;
        }
        break;
    case 0x59:
        game.input_history = 1;
        break;
    case PKT_BRICK_DELTA:
        if (p[4] == game.round_id && game.phase == 0) {
            apply_index = (uint16_t)p[3] * 20 + p[2];
            if (game.bricks[apply_index >> 3] & (1u << (apply_index & 7))) game.sound_brick = 1;
            game.bricks[apply_index >> 3] &= (uint8_t)~(1u << (apply_index & 7));
        }
        break;
    case PKT_RESPAWN:
        if (p[6] != game.round_id || game.phase != 0) break;
        apply_id = p[2];
        apply_stamp = &respawn_stamp[apply_id];
        apply_index = p[3] | ((uint16_t)p[4] << 8);
        /* Direct, reliable and re-stamped repair copies are one transition.
           Do not teleport a player who already walked away, or let a delayed
           reliable hide undo a newer final placement. Limit sequence ordering
           to a short repair horizon: global 8-bit sequence wraps frequently. */
        if ((respawn_stamped & (1u << apply_id)) &&
            (uint16_t)(game.input_clock - apply_stamp->at) < 60 &&
            (((uint8_t)(p[1]-apply_stamp->seq) >= 128) ||
             (apply_stamp->flags == p[5] && apply_stamp->cell == apply_index))) {
            ++reconcile_trace[TRACE_RESPAWN_IGNORED];
            if ((uint8_t)(p[1]-apply_stamp->seq) < 128) apply_stamp->seq = p[1];
            break;
        }
        apply_stamp->at = game.input_clock; apply_stamp->cell = apply_index;
        apply_stamp->seq = p[1]; apply_stamp->flags = p[5];
        respawn_stamped |= 1u << apply_id;
        if ((p[5] & 1) && !(p[5] & 2)) {
            start_death(apply_id);
        } else if (p[5] & 2) {
            if (game.players[apply_id].respawning || game.players[apply_id].x != p[3] ||
                game.players[apply_id].y != p[4]) motion_reset(apply_id);
            if (game.players[apply_id].respawning || game.players[apply_id].x != p[3] ||
                game.players[apply_id].y != p[4]) game.sound_events[apply_id] |= GAME_SOUND_SPAWN;
            if (apply_id == game.local_pid) {
                game.pending_count = 0;
                game.intent_count = 0;
                game.fire_cue_frames = 0;
            }
            game.deaths[apply_id].active = 0;
            game.players[apply_id].respawning = 0;
            game.players[apply_id].x = p[3];
            game.players[apply_id].y = p[4];
        }
        break;
    case PKT_NAME:
        if (game.phase == 0) {
            memcpy(game.names[p[2]], p + 3, GAME_NAME_LEN);
            if (p[2] == game.local_pid &&
                memcmp(p + 3, game.local_name, GAME_NAME_LEN) == 0)
                game.name_echoed = 1;
        }
        break;
    case PKT_SEATS:
        if (game.phase == 0) game.seat_mask = p[2];
        break;
    case PKT_SHOT:
        if (p[6] != game.round_id || game.phase != 0) break;
        apply_id = p[2];
        if (!(p[5] & 1)) game.shots[apply_id].segment = 0;
        else if (!game.shots[apply_id].active ||
                 game.shots[apply_id].direction != ((p[5] >> 1) & 3)) {
            game.shots[apply_id].segment = 0;
            game.shots[apply_id].at = game.input_clock;
        } else if (game.shots[apply_id].x != p[3] || game.shots[apply_id].y != p[4]) {
            game.shots[apply_id].previous_x = game.shots[apply_id].x;
            game.shots[apply_id].previous_y = game.shots[apply_id].y;
            game.shots[apply_id].previous_at = game.shots[apply_id].at;
            game.shots[apply_id].at = game.input_clock;
            game.shots[apply_id].segment = 1;
        }
        if ((p[5] & 1) && !game.shots[apply_id].active)
            game.sound_events[apply_id] |= GAME_SOUND_SHOT;
        if (!(p[5] & 1) && game.shots[apply_id].active)
            game.sound_events[apply_id] |= GAME_SOUND_IMPACT;
        game.shots[apply_id].active = p[5] & 1;
        if (game.shots[apply_id].active) {
            game.shots[apply_id].x = p[3];
            game.shots[apply_id].y = p[4];
            game.shots[apply_id].direction = (p[5] >> 1) & 3;
        }
        break;
    case PKT_SNAPSHOT:
        if (p[20] == game.round_id && game.phase == 0) {
            snapshot_apply(p);
        } else if (round_newer(p[20], game.round_id)) {
            memcpy(game.pending_snapshot, p, sizeof(game.pending_snapshot));
            game.pending_snapshot_valid = 1;
        }
        break;
    case PKT_LEAVE_ACK:
        if (game.leave_requested && p[1] == game.leave_seq)
            game.leave_acked = 1;
        break;
    }
}

/* Identity survives both connection initialization and WELCOME resets. */
static char configured_name[GAME_NAME_LEN] = "LYNX    ";

void game_set_name(const char *name)
{
    memset(configured_name, ' ', GAME_NAME_LEN);
    for (game_set_name_i = 0; game_set_name_i < GAME_NAME_LEN && name[game_set_name_i]; ++game_set_name_i)
        configured_name[game_set_name_i] = name[game_set_name_i];
    memcpy(game.local_name, configured_name, GAME_NAME_LEN);
}

void game_reset(void)
{
    memset(&game, 0, sizeof(game));
    memset(&reconcile_trace, 0, sizeof(reconcile_trace));
    respawn_stamped = 0;
    game.repeat_window = GAME_REPEAT_PENDING;
    game.local_pid = 255;
    game.input_joy = game.sent_joy = 15;
    memcpy(game.local_name, configured_name, GAME_NAME_LEN);
    game.name_age = 60;
}

uint8_t game_receive(const uint8_t *packet, uint8_t len)
{
    if (!packet || !len) return 0;
    if (packet[0] == PKT_WELCOME) {
        if (len != 5 || packet[1] != 1 || packet[3] > 1 ||
            packet[4] == 0 || packet[4] > 10) {
            ++game.packets_rejected;
            return 0;
        }
        game_reset();
        game.have_welcome = 1;
        game.round_id = packet[2];
        game.phase = packet[3];
        game.kill_limit = packet[4];
        return 1;
    }
    if (!game.have_welcome) return 0;
    if (packet[0] == PKT_RELIABLE_EVENT) {
        if (len < 7) {
            ++game.packets_rejected;
            game.ack_pending = 1;
            return 0;
        }
        game_receive_revision = (uint16_t)packet[2] | ((uint16_t)packet[3] << 8);
        game_receive_inner = packet + 4;
        game_receive_inner_len = len - 4;
        if (game_receive_revision != (uint16_t)(game.reliable_rev + 1) ||
            !packet_valid(game_receive_inner, game_receive_inner_len, 1)) {
            ++game.packets_rejected;
            game.ack_pending = 1;
            return 0;
        }
        game.reliable_rev = game_receive_revision;
        game.ack_pending = 1;
        apply(game_receive_inner, game_receive_inner_len, 1);
        ++game.packets_applied;
        return 1;
    }
    if (!packet_valid(packet, len, 0)) {
        ++game.packets_rejected;
        return 0;
    }
    apply(packet, len, 0);
    ++game.packets_applied;
    return 1;
}

uint8_t game_ready(void)
{
    return game.have_welcome && !game.link_paused && game.phase == 0 &&
           game.round_authorized && game.map_ready && game.snapshot_ready;
}

void game_step(void)
{
    if (!game.have_welcome) return;
    if (game.name_age != 255) ++game.name_age;
    if (game.heartbeat_age != 255) ++game.heartbeat_age;
}

void game_input_tick(uint16_t ticks)
{
    game.input_clock = ticks;
    if (game.ack_samples && (uint16_t)(ticks - game.ack_last) >= 60)
        game.repeat_window = GAME_REPEAT_PENDING;
    for (game_input_tick_i = 0; game_input_tick_i < GAME_PLAYERS; ++game_input_tick_i)
        if (game.deaths[game_input_tick_i].active &&
            (uint16_t)(ticks - game.deaths[game_input_tick_i].started) >= GAME_DEATH_TICKS)
            game.deaths[game_input_tick_i].active = 0;
}

uint8_t game_round_stage(const struct game_state *state)
{
    if (!state->phase || !state->result_valid) return ROUND_NONE;
    /* A late join has no board positions to animate. */
    if (!state->snapshot_ready || !state->map_ready) return ROUND_RESULTS;
    game_round_stage_elapsed = (uint16_t)(state->input_clock - state->result_started);
    if (game_round_stage_elapsed < GAME_DEATH_TICKS) return ROUND_LOSERS;
    if (game_round_stage_elapsed < GAME_DEATH_TICKS + GAME_WINNER_DANCE_TICKS) return ROUND_DANCE;
    if (game_round_stage_elapsed < 2 * GAME_DEATH_TICKS + GAME_WINNER_DANCE_TICKS)
        return ROUND_WINNER_SMOKE;
    return ROUND_RESULTS;
}

void game_sample_input(uint8_t directions, uint8_t fire, uint16_t ticks)
{
    game_sample_input_fresh = directions & (uint8_t)~game.direction_mask;
    game_sample_input_joy = game.input_joy & 15;
    game_input_tick(ticks);
    game.direction_mask = directions;
    game_sample_input_mask = game_sample_input_joy == 14 ? 1 : game_sample_input_joy == 7 ? 2 : game_sample_input_joy == 13 ? 4 : game_sample_input_joy == 11 ? 8 : 0;
    if (game_sample_input_fresh || !(directions & game_sample_input_mask)) {
        game_sample_input_mask = game_sample_input_fresh ? game_sample_input_fresh : directions;
        game_sample_input_joy = game_sample_input_mask & 1 ? 14 : game_sample_input_mask & 2 ? 7 : game_sample_input_mask & 4 ? 13 : game_sample_input_mask & 8 ? 11 : 15;
    }
    if (fire) game_sample_input_joy |= 0x10;
    game_set_joy(game_sample_input_joy);
}

void game_set_joy(uint8_t joy)
{
    if (!joy_ok(joy)) return;
    if (joy != game.input_joy) {
        /* Preserve sampled presses across a release during a render. Each
           intent is one discrete action, not a level-triggered repeat. */
        if (game_ready() && game.local_pid < GAME_PLAYERS &&
            !game.players[game.local_pid].respawning && joy != 15 &&
            game.intent_count < GAME_INPUT_INTENTS)
            game.intents[game.intent_count++] = joy;
        else if (game_ready() && joy != 15 && game.intent_count == GAME_INPUT_INTENTS)
            ++game.intent_dropped;
        game.input_joy = joy;
        game.delta_due = 1;
    }
    if (game_ready() && game.local_pid < GAME_PLAYERS) {
        game.players[game.local_pid].joy = joy;
        if ((joy & 15) != 15) game.facing_joy[game.local_pid] = joy & 15;
    }
}

static uint8_t retry_pending(uint8_t *out)
{
    retry_pending_timeout = game.ack_delay * 2 + 12;
    if (!game_ready() || !game.pending_count ||
        game.players[game.local_pid].respawning) return 0;
    if (retry_pending_timeout < GAME_RETRY_MIN) retry_pending_timeout = GAME_RETRY_MIN;
    if (retry_pending_timeout > GAME_RETRY_MAX) retry_pending_timeout = GAME_RETRY_MAX;
    retry_pending_tail = game.pending_count - 1;
    if ((uint16_t)(game.input_clock - game.pending_at[retry_pending_tail]) < retry_pending_timeout ||
        (uint16_t)(game.input_clock - game.retry_at) < retry_pending_timeout) return 0;
    /* Only the newest pending sequence can still be admitted by the legacy
       server's freshness filter. Retries never predict another action or
       consume a sequence, and never overtake a fresh turn/release. */
    out[0] = PKT_DELTA;
    out[1] = game.pending_seq[retry_pending_tail];
    out[2] = game.local_pid;
    out[3] = game.pending_joy[retry_pending_tail];
    out[4] = game.round_id;
    game.retry_at = game.input_clock;
    ++game.input_retries;
    return 5;
}

uint8_t game_next_outbound(uint8_t *out, uint8_t cap)
{
    game_next_outbound_explicit_intent = 0;
    game_next_outbound_repeats_pending = 0;
    if (!out || cap < 11 || !game.have_welcome) return 0;
    if (game.ack_pending) {
        out[0] = PKT_RELIABLE_ACK;
        out[1] = game.tx_seq++;
        out[2] = (uint8_t)game.reliable_rev;
        out[3] = (uint8_t)(game.reliable_rev >> 8);
        game.ack_pending = 0;
        ++game.ack_sent;
        return 4;
    }
    if (!game.name_echoed && game.name_age >= 60) {
        out[0] = PKT_NAME;
        out[1] = game.tx_seq++;
        out[2] = game.local_pid < 4 ? game.local_pid : 0;
        memcpy(out + 3, game.local_name, GAME_NAME_LEN);
        game.name_age = 0;
        return 11;
    }
    if (game.local_pid >= 4 || game.leave_requested) return 0;
    game_next_outbound_joy = game_ready() && !game.players[game.local_pid].respawning ?
        game.input_joy : 15;
    if (game_ready() && game.intent_count) {
        game_next_outbound_joy = game.intents[0];
        game_next_outbound_explicit_intent = 1;
    } else if (game.input_sent && game_next_outbound_joy == game.sent_joy &&
               !game.delta_due) {
        if ((game_next_outbound_joy & 15) != 15) {
            /* A snapshot never grants another move. Late service sends one
               repeat and starts a new interval, with no catch-up burst. */
            if ((uint16_t)(game.input_clock - game.repeat_at) >= 0x8000u)
                return retry_pending(out);
            for (game_next_outbound_i = 0; game_next_outbound_i < game.pending_count; ++game_next_outbound_i)
                if ((game.pending_joy[game_next_outbound_i] & 15) != 15 &&
                    !(game.pending_joy[game_next_outbound_i] & 0x10)) ++game_next_outbound_repeats_pending;
            if (!(game_next_outbound_joy & 0x10) && (game_next_outbound_repeats_pending >= game.repeat_window ||
                                 game.pending_count >= GAME_REPEAT_MAX)) {
                if (game.stall_at != game.input_clock) {
                    ++game.repeat_stalls;
                    game.stall_at = game.input_clock;
                }
                return retry_pending(out);
            }
        } else if ((uint16_t)(game.input_clock - game.last_input_at) < 60)
            return retry_pending(out);
    }
    if (game_ready() && game.pending_count == GAME_PENDING_INPUTS)
        return retry_pending(out);
    out[0] = PKT_DELTA;
    out[1] = game.tx_seq++;
    out[2] = game.local_pid;
    out[3] = game_next_outbound_joy;
    out[4] = game.round_id;
    if (game_next_outbound_explicit_intent) {
        --game.intent_count;
        memmove(game.intents, game.intents + 1, game.intent_count);
    }
    if (game_ready()) {
        /* The replay ring also includes neutral and firing inputs. */
        game.pending_seq[game.pending_count] = out[1];
        game.pending_at[game.pending_count] = game.input_clock;
        game.pending_joy[game.pending_count++] = game_next_outbound_joy;
        if (game.pending_count > game.pending_highwater) game.pending_highwater = game.pending_count;
        game_next_outbound_old_x = game.players[game.local_pid].x;
        game_next_outbound_old_y = game.players[game.local_pid].y;
        pending_cause[game.pending_count-1] = predict_input(game_next_outbound_joy);
        pending_position[game.pending_count-1] = local_position();
        if (game_next_outbound_old_x != game.players[game.local_pid].x || game_next_outbound_old_y != game.players[game.local_pid].y) {
            motion_append(game.local_pid,game.players[game.local_pid].x,game.players[game.local_pid].y,
                          game_next_outbound_explicit_intent ? GAME_PRESS_TICKS : GAME_MOVE_TICKS);
            game.sound_events[game.local_pid] |= GAME_SOUND_STEP;
        }
        if ((game_next_outbound_joy & 0x10) && (game_next_outbound_joy & 15) != 15 && game_next_outbound_joy != game.sent_joy)
            game.fire_cue_frames = 3;
    }
    game.last_input_at = game.input_clock;
    game.retry_at = game.input_clock;
    game.sent_joy = game_next_outbound_joy;
    game.input_sent = 1;
    if ((game_next_outbound_joy & 15) != 15)
        game.repeat_at = game.input_clock +
            (game_next_outbound_explicit_intent ? GAME_REPEAT_DELAY : GAME_REPEAT_PERIOD);
    game.delta_due = 0;
    game.heartbeat_age = 0;
    return 5;
}

uint8_t game_request_leave(uint8_t *out, uint8_t cap)
{
    if (!out || cap < 2 || !game.have_welcome || game.leave_requested)
        return 0;
    game.leave_seq = game.tx_seq++;
    game.leave_requested = 1;
    game.leave_acked = 0;
    out[0] = PKT_LEAVE_ROOM;
    out[1] = game.leave_seq;
    return 2;
}
