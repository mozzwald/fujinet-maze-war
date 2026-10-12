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

static uint8_t coordinate_ok(uint8_t x, uint8_t y)
{
    return (x < 20 && y < 19) || (x == 255 && y == 255);
}

static uint8_t joy_ok(uint8_t joy)
{
    uint8_t stick = joy & 15;
    return (joy & 0xe0) == 0 &&
           (stick == 7 || stick == 13 || stick == 11 ||
            stick == 14 || stick == 15);
}

static uint8_t name_ok(const uint8_t *name)
{
    uint8_t i;
    for (i = 0; i < GAME_NAME_LEN; ++i) {
        uint8_t c = name[i];
        if (c && !(c == ' ' || c == '-' || c == '.' ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

static uint8_t packet_valid(const uint8_t *p, uint8_t len, uint8_t reliable)
{
    uint8_t i;
    switch (p[0]) {
    case PKT_SNAPSHOT:
        if (reliable || len != 21 || !(p[2] & 1)) return 0;
        for (i = 0; i < GAME_PLAYERS; ++i)
            if (!coordinate_ok(p[3 + 2 * i], p[4 + 2 * i]) ||
                !joy_ok(p[11 + i])) return 0;
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
        for (i = 0; i < GAME_PLAYERS; ++i)
            if (!name_ok(p + 11 + 8 * i)) return 0;
        return 1;
    case PKT_ROUND_START:
        return reliable && len == 3 && p[2] > 0 && p[2] <= 10;
    case PKT_LEAVE_ACK:
        return !reliable && len == 2;
    default:
        return 0;
    }
}

static uint8_t round_newer(uint8_t candidate, uint8_t current)
{
    uint8_t delta = (uint8_t)(candidate - current);
    return delta != 0 && delta < 128;
}

static void predict_input(uint8_t joy)
{
    struct game_player *local;
    uint8_t stick = joy & 15;
    uint8_t x;
    uint8_t y;
    uint8_t i;
    uint16_t cell;
    if (!game_ready() || game.local_pid >= GAME_PLAYERS) return;
    local = &game.players[game.local_pid];
    local->joy = joy;
    if (local->respawning || local->x >= 20 || local->y >= 19 ||
        (joy & 0x10) || stick == 15) return;
    x = local->x;
    y = local->y;
    if (stick == 14) --y;
    else if (stick == 7) ++x;
    else if (stick == 13) ++y;
    else if (stick == 11) --x;
    if (x >= 20 || y >= 19) return;
    cell = (uint16_t)y * 20 + x;
    if (game.bricks[cell >> 3] & (1u << (cell & 7))) return;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        if (i != game.local_pid &&
            ((game.seat_mask | game.zombie_mask) & (1u << i)) &&
            !game.players[i].respawning &&
            game.players[i].x == x && game.players[i].y == y) return;
    }
    local->x = x;
    local->y = y;
}

static void replay_pending(void)
{
    uint8_t i;
    for (i = 0; i < game.pending_count; ++i)
        predict_input(game.pending_joy[i]);
}

static void discard_applied(uint8_t ack_seq)
{
    uint8_t n = 0;
    while (n < game.pending_count &&
           (uint8_t)(ack_seq - game.pending_seq[n]) < 128)
        ++n;
    if (!n) return;
    game.pending_count -= n;
    memmove(game.pending_seq, game.pending_seq + n, game.pending_count);
    memmove(game.pending_joy, game.pending_joy + n, game.pending_count);
}

static void start_death(uint8_t id)
{
    struct game_player *player = &game.players[id];
    struct game_death *death = &game.deaths[id];
    /* Pending respawn packets carry 0,0, not the death cell. Capture the
       last live location before hiding the actor. Echoes never restart it. */
    if (player->respawning) return;
    if (game.snapshot_ready && player->x < 20 && player->y < 18 &&
        ((game.seat_mask | game.zombie_mask) & (1u << id))) {
        death->x = player->x;
        death->y = player->y;
        death->started = game.input_clock;
        death->active = 1;
        game.sound_events[id] |= GAME_SOUND_DEATH;
    }
    player->respawning = 1;
    player->x = player->y = 255;
    if (id == game.local_pid) {
        game.pending_count = 0;
        game.intent_count = 0;
        game.fire_cue_frames = 0;
    }
}

static void snapshot_apply(const uint8_t *p)
{
    uint8_t i;
    game.local_pid = (p[2] >> 1) & 3;
    game.zombie_mask = (p[2] >> 3) & 15;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        if (p[3 + 2 * i] == 255) {
            start_death(i);
        } else if (!game.players[i].respawning) {
            /* After a death, only explicit final placement can restore
               occupancy. A stale live snapshot must not revive the body. */
            if (!game.snapshot_ready)
                game.sound_events[i] |= GAME_SOUND_SPAWN;
            else if (i != game.local_pid &&
                     (game.players[i].x != p[3 + 2*i] || game.players[i].y != p[4 + 2*i]))
                game.sound_events[i] |= GAME_SOUND_STEP;
            game.players[i].x = p[3 + 2 * i];
            game.players[i].y = p[4 + 2 * i];
        }
        game.players[i].joy = p[11 + i];
        game.players[i].score = p[15 + i];
    }
    if (p[2] & 0x80) discard_applied(p[19]);
    replay_pending();
    if (memcmp(game.names[game.local_pid], game.local_name, GAME_NAME_LEN) == 0)
        game.name_echoed = 1;
    game.snapshot_ready = 1;
    game.heartbeat_age = 0;
    ++game.snapshots;
}

static void apply(const uint8_t *p, uint8_t len, uint8_t reliable)
{
    uint8_t id;
    uint8_t i;
    uint16_t index;
    (void)len;
    switch (p[0]) {
    case PKT_ROUND_START:
        id = p[1];
        if (id == game.round_id || round_newer(id, game.round_id)) {
            if (id != game.round_id || !game.round_authorized) {
                memset(game.shots, 0, sizeof(game.shots));
                memset(game.deaths, 0, sizeof(game.deaths));
                /* A new round can use snapshots for its initial placement. */
                for (i = 0; i < GAME_PLAYERS; ++i)
                    game.players[i].respawning = 0;
                game.map_ready = 0;
                game.snapshot_ready = 0;
                game.pending_count = 0;
                game.fire_cue_frames = 0;
                game.intent_count = 0;
                game.input_sent = 0;
                memset(game.sound_events, 0, sizeof(game.sound_events));
                game.sound_brick = 0;
            }
            game.round_id = id;
            game.kill_limit = p[2];
            game.phase = 0;
            game.result_valid = 0;
            game.round_authorized = 1;
            if (game.pending_snapshot_valid &&
                game.pending_snapshot[20] == id) {
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
        for (id = 0; id < GAME_PLAYERS; ++id)
            game.players[id].score = p[6 + id];
        memcpy(game.names, p + 11, sizeof(game.names));
        memset(game.shots, 0, sizeof(game.shots));
        memset(game.deaths, 0, sizeof(game.deaths));
        break;
    case PKT_BRICK_FULL:
        if (p[51] == game.round_id) {
            memcpy(game.bricks, p + 3, GAME_BRICK_BYTES);
            if (reliable) game.map_ready = 1;
        }
        break;
    case PKT_BRICK_DELTA:
        if (p[4] == game.round_id && game.phase == 0) {
            index = (uint16_t)p[3] * 20 + p[2];
            if (game.bricks[index >> 3] & (1u << (index & 7))) game.sound_brick = 1;
            game.bricks[index >> 3] &= (uint8_t)~(1u << (index & 7));
        }
        break;
    case PKT_RESPAWN:
        if (p[6] != game.round_id || game.phase != 0) break;
        id = p[2];
        if ((p[5] & 1) && !(p[5] & 2)) {
            start_death(id);
        } else if (p[5] & 2) {
            if (game.players[id].respawning || game.players[id].x != p[3] ||
                game.players[id].y != p[4]) game.sound_events[id] |= GAME_SOUND_SPAWN;
            if (id == game.local_pid) {
                game.pending_count = 0;
                game.intent_count = 0;
                game.fire_cue_frames = 0;
            }
            game.deaths[id].active = 0;
            game.players[id].respawning = 0;
            game.players[id].x = p[3];
            game.players[id].y = p[4];
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
        id = p[2];
        if ((p[5] & 1) && !game.shots[id].active)
            game.sound_events[id] |= GAME_SOUND_SHOT;
        if (!(p[5] & 1) && game.shots[id].active)
            game.sound_events[id] |= GAME_SOUND_IMPACT;
        game.shots[id].active = p[5] & 1;
        if (game.shots[id].active) {
            game.shots[id].x = p[3];
            game.shots[id].y = p[4];
            game.shots[id].direction = (p[5] >> 1) & 3;
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
    uint8_t i;
    memset(configured_name, ' ', GAME_NAME_LEN);
    for (i = 0; i < GAME_NAME_LEN && name[i]; ++i)
        configured_name[i] = name[i];
    memcpy(game.local_name, configured_name, GAME_NAME_LEN);
}

void game_reset(void)
{
    memset(&game, 0, sizeof(game));
    game.local_pid = 255;
    game.input_joy = game.sent_joy = 15;
    memcpy(game.local_name, configured_name, GAME_NAME_LEN);
    game.name_age = 60;
}

uint8_t game_receive(const uint8_t *packet, uint8_t len)
{
    uint16_t revision;
    const uint8_t *inner;
    uint8_t inner_len;
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
        revision = (uint16_t)packet[2] | ((uint16_t)packet[3] << 8);
        inner = packet + 4;
        inner_len = len - 4;
        if (revision != (uint16_t)(game.reliable_rev + 1) ||
            !packet_valid(inner, inner_len, 1)) {
            ++game.packets_rejected;
            game.ack_pending = 1;
            return 0;
        }
        game.reliable_rev = revision;
        game.ack_pending = 1;
        apply(inner, inner_len, 1);
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
    return game.have_welcome && game.phase == 0 &&
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
    uint8_t i;
    game.input_clock = ticks;
    for (i = 0; i < GAME_PLAYERS; ++i)
        if (game.deaths[i].active &&
            (uint16_t)(ticks - game.deaths[i].started) >= GAME_DEATH_TICKS)
            game.deaths[i].active = 0;
}

uint8_t game_round_stage(const struct game_state *state)
{
    uint16_t elapsed;
    if (!state->phase || !state->result_valid) return ROUND_NONE;
    /* A late join has no board positions to animate. */
    if (!state->snapshot_ready || !state->map_ready) return ROUND_RESULTS;
    elapsed = (uint16_t)(state->input_clock - state->result_started);
    if (elapsed < GAME_DEATH_TICKS) return ROUND_LOSERS;
    if (elapsed < GAME_DEATH_TICKS + GAME_WINNER_DANCE_TICKS) return ROUND_DANCE;
    if (elapsed < 2 * GAME_DEATH_TICKS + GAME_WINNER_DANCE_TICKS)
        return ROUND_WINNER_SMOKE;
    return ROUND_RESULTS;
}

void game_sample_input(uint8_t directions, uint8_t fire, uint16_t ticks)
{
    uint8_t fresh = directions & (uint8_t)~game.direction_mask;
    uint8_t mask;
    uint8_t joy = game.input_joy & 15;
    game_input_tick(ticks);
    game.direction_mask = directions;
    mask = joy == 14 ? 1 : joy == 7 ? 2 : joy == 13 ? 4 : joy == 11 ? 8 : 0;
    if (fresh || !(directions & mask)) {
        mask = fresh ? fresh : directions;
        joy = mask & 1 ? 14 : mask & 2 ? 7 : mask & 4 ? 13 : mask & 8 ? 11 : 15;
    }
    if (fire) joy |= 0x10;
    game_set_joy(joy);
}

void game_set_joy(uint8_t joy)
{
    if (!joy_ok(joy)) return;
    if (joy != game.input_joy) {
        if ((joy & 0x10) && !(game.input_joy & 0x10) &&
            (joy & 15) != 15 && game_ready())
            game.fire_cue_frames = 3;
        /* Preserve sampled presses across a release during a render. Each
           intent is one discrete action, not a level-triggered repeat. */
        if (game_ready() && game.local_pid < GAME_PLAYERS &&
            !game.players[game.local_pid].respawning && joy != 15 &&
            game.intent_count < GAME_INPUT_INTENTS)
            game.intents[game.intent_count++] = joy;
        game.input_joy = joy;
        game.delta_due = 1;
    }
    if (game_ready() && game.local_pid < GAME_PLAYERS)
        game.players[game.local_pid].joy = joy;
}

uint8_t game_next_outbound(uint8_t *out, uint8_t cap)
{
    uint8_t joy;
    uint8_t old_x, old_y;
    uint8_t explicit_intent = 0;
    uint8_t repeats_pending = 0;
    uint8_t i;
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
    joy = game_ready() && !game.players[game.local_pid].respawning ?
        game.input_joy : 15;
    if (game_ready() && game.intent_count) {
        joy = game.intents[0];
        explicit_intent = 1;
    } else if (game.input_sent && joy == game.sent_joy &&
               !game.delta_due) {
        if ((joy & 15) != 15) {
            /* A snapshot never grants another move. Late service sends one
               repeat and starts a new interval, with no catch-up burst. */
            if ((uint16_t)(game.input_clock - game.repeat_at) >= 0x8000u)
                return 0;
            for (i = 0; i < game.pending_count; ++i)
                if ((game.pending_joy[i] & 15) != 15 &&
                    !(game.pending_joy[i] & 0x10)) ++repeats_pending;
            if (!(joy & 0x10) && repeats_pending >= GAME_REPEAT_PENDING)
                return 0;
        } else if ((uint16_t)(game.input_clock - game.last_input_at) < 60)
            return 0;
    }
    if (game_ready() && game.pending_count == GAME_PENDING_INPUTS) return 0;
    out[0] = PKT_DELTA;
    out[1] = game.tx_seq++;
    out[2] = game.local_pid;
    out[3] = joy;
    out[4] = game.round_id;
    if (explicit_intent) {
        --game.intent_count;
        memmove(game.intents, game.intents + 1, game.intent_count);
    }
    if (game_ready()) {
        /* The replay ring also includes neutral and firing inputs. */
        game.pending_seq[game.pending_count] = out[1];
        game.pending_joy[game.pending_count++] = joy;
        old_x = game.players[game.local_pid].x;
        old_y = game.players[game.local_pid].y;
        predict_input(joy);
        if (old_x != game.players[game.local_pid].x || old_y != game.players[game.local_pid].y)
            game.sound_events[game.local_pid] |= GAME_SOUND_STEP;
        if ((joy & 0x10) && !(game.sent_joy & 0x10))
            game.sound_events[game.local_pid] |= GAME_SOUND_FIRE;
    }
    game.last_input_at = game.input_clock;
    game.sent_joy = joy;
    game.input_sent = 1;
    if ((joy & 15) != 15)
        game.repeat_at = game.input_clock +
            (explicit_intent ? GAME_REPEAT_DELAY : GAME_REPEAT_PERIOD);
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
