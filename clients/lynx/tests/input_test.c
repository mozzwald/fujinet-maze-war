#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t out[11];
static uint8_t snap[21];
static void setup(void)
{
    game_reset();
    game.have_welcome = game.round_authorized = game.map_ready = game.snapshot_ready = 1;
    game.name_echoed = game.seat_mask = 1;
    game.local_pid = 0;
    game.round_id = 1;
    game.players[0].x = game.players[0].y = 5;
    game_sample_input(0, 0, 0);
    assert(game_next_outbound(out, sizeof(out)) == 5);
}

static void snapshot(uint8_t x, uint8_t y, uint8_t ack)
{
    memset(snap, 0, sizeof(snap));
    snap[0] = 0x40; snap[2] = 0x81;
    snap[3] = x; snap[4] = y;
    snap[11] = snap[12] = snap[13] = snap[14] = 15;
    snap[19] = ack; snap[20] = 1;
    assert(game_receive(snap, sizeof(snap)));
}

int main(void)
{
    uint8_t seq, i;
    setup();
    /* Press and release can both happen between outbound opportunities. */
    game_sample_input(2, 0, 1);
    game_sample_input(0, 0, 4);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    seq = out[1];
    assert(game.players[0].x == 6);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 15);
    assert(!game_next_outbound(out, sizeof(out)));
    snapshot(6, 5, seq);
    game_input_tick(50);
    assert(!game_next_outbound(out, sizeof(out)));
    assert(game.players[0].x == 6);

    /* Snapshots arriving just after the press must not create a second cell. */
    setup();
    game_sample_input(2, 0, 1);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    seq = out[1];
    snapshot(5, 5, (uint8_t)(seq - 1));
    assert(game.players[0].x == 6);
    for (i = 2; i < 16; ++i) {
        game_input_tick(i);
        assert(!game_next_outbound(out, sizeof(out)));
    }
    game_input_tick(16);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    assert(game.players[0].x == 7);
    /* No unbounded backlog when the acknowledgement stalls. */
    game_input_tick(100);
    assert(!game_next_outbound(out, sizeof(out)));
    assert(game.players[0].x == 7);
    snapshot(7, 5, out[1]);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    assert(!game_next_outbound(out, sizeof(out))); /* no catch-up burst */
    /* Stopping removes the repeat, even with the next repeat overdue. */
    game_sample_input(0, 0, 200);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 15);
    game_input_tick(201);
    assert(!game_next_outbound(out, sizeof(out)));

    /* A sampled corner turn follows the preceding discrete move, without
       requiring the old direction to be released first. */
    setup();
    game_sample_input(1, 0, 1);
    game_sample_input(3, 0, 2); /* Right newly pressed while Up remains held */
    game_sample_input(2, 0, 3);
    game_sample_input(0, 0, 4);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 14);
    assert(game.players[0].x == 5 && game.players[0].y == 4);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    assert(game.players[0].x == 6 && game.players[0].y == 4);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 15);
    assert(!game_next_outbound(out, sizeof(out)));

    /* Multiple intentional taps preserve their order, never folded away. */
    setup();
    for (i = 0; i < 3; ++i) {
        game_sample_input(2, 0, (uint16_t)(i * 2));
        game_sample_input(0, 0, (uint16_t)(i * 2 + 1));
    }
    for (i = 0; i < 3; ++i)
        assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    assert(game.players[0].x == 8);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 15);

    /* A full replay ring waits for ACKs without forgetting old predictions
       or dropping the newly sampled turn. */
    setup();
    game.pending_count = 0;
    for (i = 0; i < 4; ++i) {
        game_sample_input(2, 0, i * 2);
        assert(game_next_outbound(out, sizeof(out)) == 5);
        game_sample_input(0, 0, i * 2 + 1);
        assert(game_next_outbound(out, sizeof(out)) == 5);
    }
    seq = out[1];
    game_sample_input(1, 0, 9);
    assert(!game_next_outbound(out, sizeof(out)));
    assert(game.pending_count == 8 && game.intent_count == 1);
    snapshot(9, 5, seq);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 14);
    assert(game.players[0].x == 9 && game.players[0].y == 4);

    /* Repeat deadlines survive the 16-bit VBL clock wrapping. */
    setup();
    game_sample_input(2, 0, 65530u);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    game_input_tick(8);
    assert(!game_next_outbound(out, sizeof(out)));
    game_input_tick(9);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    /* Slow applied ACKs expand only the held-repeat allowance. Four steps
       still leave space for an explicit turn and neutral release. */
    setup(); seq=out[1];
    game_input_tick(18); snapshot(5,5,seq);
    assert(game.ack_delay==18 && game.repeat_window==4);
    game_sample_input(2,0,19);
    assert(game_next_outbound(out,sizeof(out))==5);
    for (i=0;i<3;++i) {
        game_input_tick(34+i*6);
        assert(game_next_outbound(out,sizeof(out))==5 && out[3]==7);
    }
    game_input_tick(52); assert(!game_next_outbound(out,sizeof(out)));
    game_sample_input(1,0,53);
    assert(game_next_outbound(out,sizeof(out))==5 && out[3]==14);
    game_sample_input(0,0,54);
    assert(game_next_outbound(out,sizeof(out))==5 && out[3]==15);
    assert(game.pending_count==6 && game.pending_highwater==6);
    game_input_tick(80); assert(game.repeat_window==2);
    assert(!game_next_outbound(out,sizeof(out)));
    puts("input tests passed: taps, snapshot races, bounded repeats, stop, turns, clock wrap");
    return 0;
}
