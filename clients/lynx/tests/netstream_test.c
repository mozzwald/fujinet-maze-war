#include "netstream.h"
#include "protocol.h"
#include "frame.h"
#include "serial_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t rx[FRAME_WIRE_MAX], tx[128], rd, used, sent, mid_frame, reads_during_frame;
static uint8_t fail, busy, inject, clock_offset;
uint16_t command_clock(void) { return game.input_clock+clock_offset; }
uint8_t command_open(void) { return 1; }
uint8_t command_complete(uint8_t d,const uint8_t *p,uint16_t n)
{ assert(d==0x70 && p[0]==0xf0 && n>4); return 1; }
void command_enter_raw(void) {}
uint8_t ser_get(char *byte)
{
    if(mid_frame) ++reads_during_frame;
    if(rd==used) return SER_ERR_NO_DATA;
    *byte=rx[rd++]; return SER_ERR_OK;
}
uint8_t ser_status(uint8_t *status) { *status=0; return SER_ERR_OK; }
uint8_t ser_put(char byte)
{
    if(fail && sent==3 && byte) return SER_ERR_OVERFLOW;
    if(busy) { --busy; return SER_ERR_OVERFLOW; }
    tx[sent++]=(uint8_t)byte;
    mid_frame=byte!=0;
    if(!byte && inject) { rd=0; used=5; inject=0; }
    return SER_ERR_OK;
}
static void setup(void)
{
    static const uint8_t welcome[]={0x47,1,1,0,5};
    rd=used=sent=mid_frame=reads_during_frame=fail=busy=inject=clock_offset=0;
    assert(netstream_start("test",5025));
    used=frame_encode(welcome,sizeof(welcome),rx,sizeof(rx));
    netstream_poll(); assert(link_state==LINK_WELCOME);
    clock_offset=2; /* Welcome's receive burst has finished. */
    game.round_authorized=game.map_ready=game.snapshot_ready=game.name_echoed=1;
    game.local_pid=0; game.seat_mask=1; game.players[0].x=game.players[0].y=5;
}
int main(void)
{
    uint8_t out[64], len, i;
    static const uint8_t clear[]={0x42,0,3,0,0,0,1};
    struct frame_rx parser;
    setup(); busy=3;
    game_sample_input(2,0,0); netstream_service();
    assert(link_state==LINK_WELCOME && sent==9 && !mid_frame);
    assert(!reads_during_frame); /* parsing cannot split frame enqueue */
    frame_reset(&parser);
    for(i=0;i<sent-1;++i) assert(!frame_feed(&parser,tx[i],out,sizeof(out),&len));
    assert(frame_feed(&parser,0,out,sizeof(out),&len)==1);
    assert(len==5 && out[0]==0x41 && out[3]==7 && out[4]==1);
    setup(); fail=1;
    game_sample_input(2,0,0); netstream_service();
    assert(link_state==LINK_TX_ERROR && sent==4 && tx[3]==0);
    assert(!reads_during_frame); /* bounded failure terminates fragment */
    setup(); rd=0;
    len=frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    game_sample_input(2,0,0); netstream_service();
    assert(!sent && !game.pending_count); /* RX frame still arriving */
    used=len; netstream_service();
    assert(!sent && !game.pending_count); /* complete is not quiet */
    game_input_tick(2); netstream_service();
    assert(sent==9 && game.pending_count==1 && !reads_during_frame);
    setup(); rd=0;
    frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    game_sample_input(2,0,0); netstream_service(); assert(!sent);
    game_input_tick(1); netstream_service(); assert(!sent);
    game_input_tick(2); netstream_service();
    assert(sent==9 && link_stats.last_frame_error==FRAME_ERROR_TRUNCATED);
    setup();
    len=frame_encode(clear,sizeof(clear),rx,sizeof(rx)); rd=used=len;
    game_sample_input(2,0,0); game.ack_pending=1; inject=1;
    netstream_service(); /* Partial RX appears after the reliable ACK. */
    assert(sent==8 && !game.pending_count && game.intent_count==1);
    used=len; netstream_service();
    assert(sent==8 && !game.pending_count);
    game_input_tick(2); netstream_service();
    assert(sent==17 && game.pending_count==1);
    setup(); rd=0; clock_offset=2;
    frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    game_sample_input(2,0,0); netstream_service();
    assert(!sent); /* RX clock can advance beyond the sampled input clock. */
    game_input_tick(1); netstream_service(); assert(!sent);
    game_input_tick(2); netstream_service(); assert(sent==9);
    /* Two complete frames in one burst still require quiet after the last
       byte. Incoming traffic across a VBL resets the handoff deadline. */
    setup(); rd=0;
    used=frame_encode(clear,sizeof(clear),rx,sizeof(rx));
    game_sample_input(2,0,0); netstream_service(); assert(!sent);
    game_input_tick(1); rd=0; netstream_service(); assert(!sent);
    game_input_tick(2); netstream_service(); assert(!sent);
    game_input_tick(3); netstream_service(); assert(sent==9);
    setup(); rd=0;
    used=frame_encode(clear,sizeof(clear),rx,sizeof(rx));
    game_sample_input(2,0,0); netstream_service(); assert(!sent);
    link_stats.rx_bytes=0; /* A wrapped diagnostic counter is not bus quiet. */
    netstream_service(); assert(!sent);
    game_input_tick(2); netstream_service(); assert(sent==9);
    puts("netstream tests passed: contiguous enqueue, complete/partial burst quiet handoff, timeout, bounded busy retry, valid CRC and fragment delimiter");
    return 0;
}
