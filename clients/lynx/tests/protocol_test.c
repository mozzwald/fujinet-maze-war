#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void reliable(uint16_t rev, const uint8_t *inner, uint8_t inner_len)
{
    uint8_t packet[60];
    packet[0] = 0x53;
    packet[1] = 0;
    packet[2] = (uint8_t)rev;
    packet[3] = (uint8_t)(rev >> 8);
    memcpy(packet + 4, inner, inner_len);
    assert(game_receive(packet, (uint8_t)(inner_len + 4)) == 1);
}

static void ack(uint16_t rev)
{
    uint8_t packet[11];
    uint8_t len = game_next_outbound(packet, sizeof(packet));
    assert(len == 4 && packet[0] == 0x45);
    assert(packet[2] == (uint8_t)rev && packet[3] == (uint8_t)(rev >> 8));
}

static void prediction_test(void)
{
    static const uint8_t welcome[] = {0x47, 1, 1, 0, 5};
    static const uint8_t start[] = {0x55, 1, 5};
    static const uint8_t seats[] = {0x44, 0, 1};
    uint8_t brick[52] = {0x50, 0, 1};
    uint8_t snap[21] = {0x40, 0, 1, 5, 5, 255, 255, 255, 255,
                        255, 255, 15, 15, 15, 15, 0, 0, 0, 0, 0, 1};
    uint8_t out[11];
    uint8_t neutral_seq;
    uint8_t right_seq;

    game_reset();
    assert(game_receive(welcome, sizeof(welcome)));
    assert(game_receive(seats, sizeof(seats)));
    reliable(1, start, sizeof(start));
    ack(1);
    brick[51] = 1;
    reliable(2, brick, sizeof(brick));
    ack(2);
    assert(game_receive(snap, sizeof(snap)));
    assert(game_next_outbound(out, sizeof(out)) == 11); /* NAME */
    assert(game_next_outbound(out, sizeof(out)) == 5);  /* neutral */
    neutral_seq = out[1];
    game_set_joy(7);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    right_seq = out[1];
    assert(out[3] == 7 && game.players[0].x == 6);
    assert(game_next_outbound(out, sizeof(out)) == 0);

    /* Server has applied neutral; replay right over the old position. */
    snap[2] = 0x81;
    snap[19] = neutral_seq;
    assert(game_receive(snap, sizeof(snap)));
    assert(game.players[0].x == 6 && game.pending_count == 1);
    snap[3] = 6;
    snap[19] = right_seq;
    assert(game_receive(snap, sizeof(snap)));
    assert(game.players[0].x == 6 && game.pending_count == 0);

    /* Directional firing gives an immediate cue but does not move. */
    game_set_joy(0x17);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    assert(game.fire_cue_frames == 3);
    assert(game.players[0].x == 6);

    /* Wall collision must suppress local prediction. */
    game_set_joy(15);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    game.bricks[(5 * 20 + 7) >> 3] |= 1u << ((5 * 20 + 7) & 7);
    game_set_joy(7);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    assert(game.players[0].x == 6);
}

int main(void)
{
    static const uint8_t welcome[] = {0x47, 1, 1, 0, 5};
    static const uint8_t start1[] = {0x55, 1, 5};
    static const uint8_t start2[] = {0x55, 2, 5};
    static const uint8_t old_brick[] = {0x51, 1, 4, 4, 1};
    static const uint8_t seats[] = {0x44, 0, 1};
    static const uint8_t bad_shot[] = {0x42, 0, 4, 2, 2, 1, 1};
    uint8_t snapshot[21] = {0x40, 9, 1, 2, 2, 3, 3, 4, 4, 5, 5,
                            15, 15, 15, 15, 0, 0, 0, 0, 0, 1};
    uint8_t brick[52] = {0x50, 0, 1};
    uint8_t wrapper[60];
    uint8_t out[11];
    uint8_t len;
    uint8_t name[11] = {0x43, 0, 0, 'L','Y','N','X',' ',' ',' ',' '};
    uint8_t leave_ack[2];

    game_reset();
    assert(!game_ready());
    assert(game_receive(snapshot, sizeof(snapshot)) == 0);
    assert(game_receive(welcome, sizeof(welcome)) == 1);
    assert(game_receive(seats, sizeof(seats)) == 1);
    assert(game.seat_mask == 1);
    reliable(1, start1, sizeof(start1));
    ack(1);
    assert(game.round_authorized && !game_ready());
    brick[51] = 1;
    brick[3] = 0x10;
    brick[13] = 0x10; /* (4,4) remains set after an old-round delta */
    reliable(2, brick, sizeof(brick));
    ack(2);
    assert(game.map_ready && !game_ready());
    assert(game_receive(snapshot, sizeof(snapshot)) == 1);
    assert(game_ready() && game.local_pid == 0 && game.snapshots == 1);
    len = game_next_outbound(out, sizeof(out));
    assert(len == 11 && out[0] == 0x43);
    assert(game_receive(name, sizeof(name)) == 1 && game.name_echoed);
    len = game_next_outbound(out, sizeof(out));
    assert(len == 5 && out[0] == 0x41 && out[2] == 0 &&
           out[3] == 15 && out[4] == 1);
    assert(game_receive(bad_shot, sizeof(bad_shot)) == 0);

    /* A gap cannot mutate state and re-ACKs the highest applied revision. */
    wrapper[0] = 0x53; wrapper[1] = 0;
    wrapper[2] = 4; wrapper[3] = 0;
    memcpy(wrapper + 4, start2, sizeof(start2));
    assert(game_receive(wrapper, 7) == 0);
    assert(game.round_id == 1 && game.reliable_rev == 2);
    ack(2);

    reliable(3, name, sizeof(name));
    ack(3);
    reliable(4, start2, sizeof(start2));
    ack(4);
    assert(game.round_id == 2 && !game_ready());
    /* Old-round events advance the stream without changing the new map. */
    reliable(5, old_brick, sizeof(old_brick));
    ack(5);
    assert(game.bricks[10] == 0x10);
    brick[51] = 2;
    assert(game_receive(brick, sizeof(brick)) == 1);
    assert(!game.map_ready); /* direct repair does not authorize a round */
    reliable(6, brick, sizeof(brick));
    ack(6);
    assert(game.map_ready && !game.snapshot_ready);
    snapshot[20] = 2;
    assert(game_receive(snapshot, sizeof(snapshot)) == 1 && game_ready());

    /* Game-level leave ACK is tracked without claiming FujiNet teardown. */
    assert(game_request_leave(out, sizeof(out)) == 2);
    assert(out[0] == 0x56);
    leave_ack[0] = 0x57; leave_ack[1] = out[1];
    assert(game_receive(leave_ack, sizeof(leave_ack)) == 1);
    assert(game.leave_acked);
    prediction_test();
    game_set_name("ACE LYNX");
    game_reset();
    assert(!memcmp(game.local_name, "ACE LYNX", GAME_NAME_LEN));
    assert(game_receive(welcome, sizeof(welcome)) == 1);
    assert(game_next_outbound(out, sizeof(out)) == 11);
    assert(out[0] == 0x43 && !memcmp(out + 3, "ACE LYNX", GAME_NAME_LEN));
    puts("protocol tests passed");
    return 0;
}
