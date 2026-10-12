#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t out[11];
static void setup(uint16_t tick)
{
    game_reset();
    game.have_welcome=game.round_authorized=game.map_ready=game.snapshot_ready=1;
    game.name_echoed=game.seat_mask=1;
    game.local_pid=0; game.round_id=1;
    game.players[0].x=game.players[0].y=5;
    game_sample_input(2,0,tick);
    assert(game_next_outbound(out,sizeof(out))==5);
}
static void ack(uint8_t seq)
{
    uint8_t snap[21]={0x40,0,0x81,7,5,0,0,0,0,0,0,15,15,15,15,0,0,0,0,0,1};
    snap[19]=seq;
    assert(game_receive(snap,sizeof(snap)));
}
int main(void)
{
    uint8_t tail, tx, count;
    uint16_t motion;
    setup(0);
    game_input_tick(15);
    assert(game_next_outbound(out,sizeof(out))==5);
    tail=out[1]; tx=game.tx_seq; count=game.pending_count;
    motion=game.motion[0].serial;
    game.sound_events[0]=0;
    game_input_tick(44); assert(!game_next_outbound(out,sizeof(out)));
    game_input_tick(45);
    assert(game_next_outbound(out,sizeof(out))==5 && out[1]==tail && out[3]==7);
    assert(game.tx_seq==tx && game.pending_count==count);
    assert(game.motion[0].serial==motion && game.players[0].x==7);
    assert(game.sound_events[0]==0 && game.input_retries==1);
    assert(!game_next_outbound(out,sizeof(out))); /* no retry burst */
    game_input_tick(74); assert(!game_next_outbound(out,sizeof(out)));
    game_input_tick(75);
    assert(game_next_outbound(out,sizeof(out))==5 && out[1]==tail);
    ack(tail); /* recovery removes pending tail and unlocks held movement */
    assert(game_next_outbound(out,sizeof(out))==5 && out[1]==tx);
    assert(game.players[0].x==8);

    /* Due retries never overtake an explicit turn or release. */
    setup(0); game_input_tick(15); assert(game_next_outbound(out,sizeof(out))==5);
    game_sample_input(1,0,100);
    assert(game_next_outbound(out,sizeof(out))==5 && out[3]==14);
    game_sample_input(0,0,100);
    assert(game_next_outbound(out,sizeof(out))==5 && out[3]==15);
    assert(game.input_retries==0);
    /* A full replay ring can still retransmit without adding an entry. */
    game.pending_count=GAME_PENDING_INPUTS;
    game.pending_seq[7]=game.tx_seq-1; game.pending_joy[7]=15;
    game.pending_at[7]=100; game_input_tick(130);
    assert(game_next_outbound(out,sizeof(out))==5 && out[1]==game.tx_seq-1);
    assert(game.pending_count==GAME_PENDING_INPUTS);

    /* Slow measured ACKs extend retry pacing, capped at two seconds. */
    setup(0); game.ack_delay=100; game.ack_samples=1;
    game_input_tick(15); assert(game_next_outbound(out,sizeof(out))==5);
    game_input_tick(134); assert(!game_next_outbound(out,sizeof(out)));
    game_input_tick(135); assert(game_next_outbound(out,sizeof(out))==5);
    assert(game.input_retries==1);
    game.phase=1; game_input_tick(255);
    assert(game_next_outbound(out,sizeof(out))==5 && out[3]==15);
    assert(game.input_retries==1); /* intermission never retries gameplay */

    setup(65520u); game_input_tick(65535u);
    assert(game_next_outbound(out,sizeof(out))==5); tail=out[1];
    game_input_tick(28); assert(!game_next_outbound(out,sizeof(out)));
    game_input_tick(29); assert(game_next_outbound(out,sizeof(out))==5 && out[1]==tail);
    game.leave_requested=1; game_input_tick(59);
    assert(!game_next_outbound(out,sizeof(out)));
    puts("retry tests passed: lost tail, bounded rate, no duplicate prediction/sound, turns/release priority, full ring, timeout cap, phase/leave and clock wrap");
    return 0;
}
