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

static void snapshot_apply(const uint8_t *p)
{
    uint8_t i;
    game.local_pid = (p[2] >> 1) & 3;
    game.zombie_mask = (p[2] >> 3) & 15;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        game.players[i].x = p[3 + 2 * i];
        game.players[i].y = p[4 + 2 * i];
        game.players[i].respawning = p[3 + 2 * i] == 255;
        game.players[i].joy = p[11 + i];
        game.players[i].score = p[15 + i];
    }
    if (p[2] & 0x80) discard_applied(p[19]);
    replay_pending();
    if (memcmp(game.names[game.local_pid], game.local_name, GAME_NAME_LEN) == 0)
        game.name_echoed = 1;
    game.snapshot_ready = 1;
    game.delta_due = 1;
    game.heartbeat_age = 0;
    ++game.snapshots;
}

static void apply(const uint8_t *p, uint8_t len, uint8_t reliable)
{
    uint8_t id;
    uint16_t index;
    (void)len;
    switch (p[0]) {
    case PKT_ROUND_START:
        id = p[1];
        if (id == game.round_id || round_newer(id, game.round_id)) {
            if (id != game.round_id || !game.round_authorized) {
                memset(game.shots, 0, sizeof(game.shots));
                game.map_ready = 0;
                game.snapshot_ready = 0;
                game.pending_count = 0;
                game.fire_cue_frames = 0;
            }
            game.round_id = id;
            game.kill_limit = p[2];
            game.phase = 0;
            game.round_authorized = 1;
            if (game.pending_snapshot_valid &&
                game.pending_snapshot[20] == id) {
                snapshot_apply(game.pending_snapshot);
            }
            game.pending_snapshot_valid = 0;
        }
        break;
    case PKT_MATCH_END:
        if (p[1] != game.round_id) break;
        game.phase = 1;
        game.pending_count = 0;
        game.fire_cue_frames = 0;
        game.winner_pid = p[2];
        game.seat_mask = p[3] & (uint8_t)~p[4];
        game.zombie_mask = p[4];
        game.kill_limit = p[5];
        for (id = 0; id < GAME_PLAYERS; ++id)
            game.players[id].score = p[6 + id];
        memcpy(game.names, p + 11, sizeof(game.names));
        memset(game.shots, 0, sizeof(game.shots));
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
            game.bricks[index >> 3] &= (uint8_t)~(1u << (index & 7));
        }
        break;
    case PKT_RESPAWN:
        if (p[6] != game.round_id || game.phase != 0) break;
        id = p[2];
        if (id == game.local_pid) {
            game.pending_count = 0;
            game.fire_cue_frames = 0;
        }
        game.players[id].respawning = (p[5] & 1) && !(p[5] & 2);
        if (game.players[id].respawning) {
            game.players[id].x = 255;
            game.players[id].y = 255;
        } else if (p[5] & 2) {
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

void game_reset(void)
{
    memset(&game, 0, sizeof(game));
    game.local_pid = 255;
    game.input_joy = 15;
    memcpy(game.local_name, "LYNX    ", GAME_NAME_LEN);
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

void game_set_joy(uint8_t joy)
{
    if (!joy_ok(joy)) return;
    if (joy != game.input_joy) {
        if ((joy & 0x10) && !(game.input_joy & 0x10) &&
            (joy & 15) != 15 && game_ready())
            game.fire_cue_frames = 3;
        game.input_joy = joy;
        game.delta_due = 1;
    }
    if (game_ready() && game.local_pid < GAME_PLAYERS)
        game.players[game.local_pid].joy = joy;
}

uint8_t game_next_outbound(uint8_t *out, uint8_t cap)
{
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
    if (game.local_pid < 4 && !game.leave_requested &&
        (game.delta_due || game.heartbeat_age >= 60)) {
        out[0] = PKT_DELTA;
        out[1] = game.tx_seq++;
        out[2] = game.local_pid;
        out[3] = game_ready() ? game.input_joy : 15;
        out[4] = game.round_id;
        if (game_ready()) {
            /* Keep sending even if a long link stall fills the replay window.
               The next snapshot restores the authoritative base. */
            if (game.pending_count == GAME_PENDING_INPUTS)
                game.pending_count = 0;
            game.pending_seq[game.pending_count] = out[1];
            game.pending_joy[game.pending_count++] = out[3];
            predict_input(out[3]);
        }
        game.delta_due = 0;
        game.heartbeat_age = 0;
        return 5;
    }
    return 0;
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
