#include "netstream.h"
#include "protocol.h"
#include "frame.h"
#include "serial_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t rx[FRAME_WIRE_MAX], tx[4096], rd, used, mid_frame, reads_during_frame;
static uint16_t sent, wire_clock;
static uint8_t reported_status, get_error, status_error;
static uint8_t fail, busy, inject, clock_offset;
uint16_t command_clock(void) { return wire_clock+clock_offset; }
uint8_t command_open(void) { return 1; }
uint8_t command_complete(uint8_t d,const uint8_t *p,uint16_t n)
{ assert(d==0x70 && p[0]==0xf0 && n>4); return 1; }
void command_enter_raw(void) {}
uint8_t ser_get(char *byte)
{
    if(mid_frame) ++reads_during_frame;
    if(get_error) return get_error;
    if(rd==used) return SER_ERR_NO_DATA;
    *byte=rx[rd++]; return SER_ERR_OK;
}
uint8_t ser_status(uint8_t *status) { *status=reported_status; return status_error; }
uint8_t ser_put(char byte)
{
    if(fail && sent==3 && byte) return SER_ERR_OVERFLOW;
    if(busy) { --busy; return SER_ERR_OVERFLOW; }
    tx[sent++]=(uint8_t)byte;
    mid_frame=byte!=0;
    if(!byte && inject) { rd=0; used=5; inject=0; }
    return SER_ERR_OK;
}
static void tick(uint16_t at) { wire_clock=at; game_input_tick(at); }
static void sample(uint8_t mask,uint8_t fire,uint16_t at)
{ wire_clock=at; game_sample_input(mask,fire,at); }
static void deliver(const uint8_t *p,uint8_t n)
{
    rd=0;used=frame_encode(p,n,rx,sizeof(rx));assert(used);
    netstream_poll();
}
static void setup(void)
{
    static const uint8_t welcome[]={0x47,1,1,0,5};
    rd=used=sent=mid_frame=reads_during_frame=fail=busy=inject=clock_offset=0;
    reported_status=get_error=status_error=0; wire_clock=0;
    assert(netstream_start("test",5025));
    used=frame_encode(welcome,sizeof(welcome),rx,sizeof(rx));
    netstream_poll(); assert(link_state==LINK_WELCOME);
    clock_offset=2; /* Welcome's receive burst has finished. */
    game.round_authorized=game.map_ready=game.snapshot_ready=game.name_echoed=1;
    game.local_pid=0; game.seat_mask=1; game.players[0].x=game.players[0].y=5;
}
static void diagnostics_checks(void)
{
    setup();
    reported_status=4;netstream_poll();assert(link_stats.serial_errors==1);
    reported_status=8;netstream_poll();assert(link_stats.serial_errors==2);
    netstream_poll();assert(link_stats.serial_errors==2); /* subset of accumulated 12 */
    reported_status=0;netstream_poll();reported_status=8;netstream_poll();
    assert(link_stats.serial_errors==2 && link_stats.serial_status==12);
    reported_status=0x9e;netstream_poll();assert(link_stats.serial_errors==3);
    get_error=SER_ERR_OVERFLOW;netstream_poll();assert(link_stats.serial_api_errors==2);
    get_error=0;status_error=SER_ERR_OVERFLOW;netstream_poll();
    assert(link_stats.serial_api_errors==3 && link_stats.serial_errors==3);
    assert(link_stats.rx_overflows==0 && link_stats.bad_frames==0);
}
static void control_only(uint16_t start)
{
    struct frame_rx p;uint8_t out[64],len;uint16_t i;
    frame_reset(&p);
    for(i=start;i<sent;++i) {
        int r=frame_feed(&p,tx[i],out,sizeof(out),&len);assert(r>=0);
        if(r) assert(len==2 && (out[0]==0x46 || out[0]==0x56));
    }
}
static void connection_checks(void)
{
    static const uint8_t welcome[]={0x47,1,2,0,5};
    static const uint8_t round[]={0x53,0,1,0,0x55,2,5};
    static const uint8_t seats[]={0x44,0,1};
    static const uint8_t noise[]={0x42,0,3,0,0,0,1};
    uint8_t snapshot[]={0x40,0,0x81,5,5,255,255,255,255,255,255,
                        15,15,15,15,0,0,0,0,0,1};
    uint8_t map[56]={0x53,0,2,0,0x50,0,1};
    uint16_t start;
    setup();sample(8,0,0);netstream_service();assert(game.pending_count==1);
    tick(178);netstream_service(); /* command_clock includes two extra ticks */
    assert(link_state==LINK_STALE && game.link_paused && !game_ready());
    assert(!game.pending_count && !game.intent_count);
    start=sent;sample(2,0,180);netstream_service();
    assert(sent==start && !game.intent_count); /* no new movement or retries */
    deliver(noise,sizeof(noise));assert(link_state==LINK_STALE); /* valid traffic != fresh snapshot */
    sample(0,0,181);deliver(snapshot,sizeof(snapshot));
    assert(link_state==LINK_WELCOME && game_ready() && !game.link_paused);
    assert(game.players[0].x==5 && !game.pending_count && !game.intent_count);

    /* Intermission metadata keeps the link alive; ROUND_START must give
       the new map/snapshot bootstrap its own freshness window. */
    setup();game.phase=1;game.result_valid=1;tick(1000);
    deliver(seats,sizeof(seats));netstream_service();assert(link_state==LINK_WELCOME);
    deliver(round,sizeof(round));tick(1001);netstream_service();
    assert(link_state==LINK_WELCOME && !game.map_ready && !game.link_paused);

    /* Snapshots without the reliable map cannot keep SYNCING forever. */
    setup();game.map_ready=0;tick(178);deliver(snapshot,sizeof(snapshot));
    netstream_service();assert(link_state==LINK_STALE && !game_ready());
    tick(598);deliver(snapshot,sizeof(snapshot));netstream_service();
    assert(link_state==LINK_REJOIN);

    setup();game_set_name("RECOVER");sample(2,0,0);netstream_service();start=sent;
    tick(598);netstream_service();
    assert(link_state==LINK_REJOIN && link_stats.rejoins==1 && !game_ready());
    assert(game.leave_requested && !game.pending_count && !game.intent_count);
    tick(628);netstream_service(); /* HELLO after retiring the previous socket */
    tick(658);netstream_service(); /* bounded same-sequence LEAVE retry */
    tick(688);netstream_service();control_only(start);
    deliver(snapshot,sizeof(snapshot));assert(link_state==LINK_REJOIN); /* old-session frames ignored */
    deliver(welcome,sizeof(welcome));
    assert(link_state==LINK_WELCOME && !game_ready() && !game.leave_requested);
    assert(!game.pending_count && !game.intent_count && !game.input_history);
    assert(!memcmp(game.local_name,"RECOVER ",8));
    map[55]=2;snapshot[20]=2;
    deliver(round,sizeof(round));deliver(map,sizeof(map));deliver(seats,sizeof(seats));
    deliver(snapshot,sizeof(snapshot));assert(game_ready() && game.local_pid==0);
    assert(game.input_joy==15); /* no stale action replays into the new seat */

    setup();tick(598);netstream_service();tick(2398);netstream_service();
    assert(link_state==LINK_LOST && !game_ready());start=sent;
    netstream_service();assert(sent==start);netstream_retry();
    assert(link_state==LINK_REJOIN && link_stats.rejoins==2);
    netstream_service();assert(sent>start);control_only(start);

    setup();wire_clock=65520;assert(netstream_start("test",5025));
    deliver(welcome,sizeof(welcome));game.round_authorized=game.map_ready=game.snapshot_ready=1;
    game.local_pid=0;tick(164);netstream_service(); /* 180 ticks across wrap */
    assert(link_state==LINK_STALE);tick(584);netstream_service();
    assert(link_state==LINK_REJOIN);tick(2384);netstream_service();assert(link_state==LINK_LOST);

    setup();wire_clock=0;assert(netstream_start("test",5025));
    tick(0);netstream_tick();assert(link_state==LINK_WAIT_WELCOME);
    tick(600);netstream_tick();assert(link_state==LINK_TIMEOUT);
    netstream_retry();assert(link_state==LINK_REJOIN); /* raw retry, no FujiNet command */
}

int main(void)
{
    uint8_t out[64], len, i;
    static const uint8_t clear[]={0x42,0,3,0,0,0,1};
    struct frame_rx parser;
    setup(); busy=3;
    sample(2,0,0); netstream_service();
    assert(link_state==LINK_WELCOME && sent==9 && !mid_frame);
    assert(!reads_during_frame); /* parsing cannot split frame enqueue */
    frame_reset(&parser);
    for(i=0;i<sent-1;++i) assert(!frame_feed(&parser,tx[i],out,sizeof(out),&len));
    assert(frame_feed(&parser,0,out,sizeof(out),&len)==1);
    assert(len==5 && out[0]==0x41 && out[3]==7 && out[4]==1);
    setup(); fail=1;
    sample(2,0,0); netstream_service();
    assert(link_state==LINK_TX_ERROR && sent==4 && tx[3]==0);
    assert(!reads_during_frame); /* bounded failure terminates fragment */
    setup(); rd=0;
    len=frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    sample(2,0,0); netstream_service();
    assert(!sent && !game.pending_count); /* RX frame still arriving */
    used=len; netstream_service();
    assert(!sent && !game.pending_count); /* complete is not quiet */
    tick(2); netstream_service();
    assert(sent==9 && game.pending_count==1 && !reads_during_frame);
    setup(); rd=0;
    frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    sample(2,0,0); netstream_service(); assert(!sent);
    assert(link_stats.quiet_ticks==1);
    netstream_service(); assert(link_stats.quiet_ticks==1); /* same VBL */
    tick(1); netstream_service(); assert(!sent);
    assert(link_stats.quiet_ticks==2);
    tick(2); netstream_service();
    assert(sent==9 && link_stats.last_frame_error==FRAME_ERROR_TRUNCATED);
    setup();
    len=frame_encode(clear,sizeof(clear),rx,sizeof(rx)); rd=used=len;
    sample(2,0,0); game.ack_pending=1; inject=1;
    netstream_service(); /* Partial RX appears after the reliable ACK. */
    assert(sent==8 && !game.pending_count && game.intent_count==1);
    used=len; netstream_service();
    assert(sent==8 && !game.pending_count);
    tick(2); netstream_service();
    assert(sent==17 && game.pending_count==1);
    setup(); rd=0; clock_offset=2;
    frame_encode(clear,sizeof(clear),rx,sizeof(rx)); used=5;
    sample(2,0,0); netstream_service();
    assert(!sent); /* RX clock can advance beyond the sampled input clock. */
    tick(1); netstream_service(); assert(!sent);
    tick(2); netstream_service(); assert(sent==9);
    /* Two complete frames in one burst still require quiet after the last
       byte. Incoming traffic across a VBL resets the handoff deadline. */
    setup(); rd=0;
    used=frame_encode(clear,sizeof(clear),rx,sizeof(rx));
    sample(2,0,0); netstream_service(); assert(!sent);
    tick(1); rd=0; netstream_service(); assert(!sent);
    tick(2); netstream_service(); assert(!sent);
    tick(3); netstream_service(); assert(sent==9);
    setup(); rd=0;
    used=frame_encode(clear,sizeof(clear),rx,sizeof(rx));
    sample(2,0,0); netstream_service(); assert(!sent);
    link_stats.rx_bytes=0; /* A wrapped diagnostic counter is not bus quiet. */
    netstream_service(); assert(!sent);
    tick(2); netstream_service(); assert(sent==9);
    /* Negotiation retries without changing the legacy DELTA path. A single
       history CRC then covers all pending inputs, oldest first. */
    setup(); tick(60); netstream_service();
    assert(sent==15 && !game.input_history); /* offer(6) + neutral(9) */
    static const uint8_t mode[]={0x59,1};
    rd=0; used=frame_encode(mode,sizeof(mode),rx,sizeof(rx));
    netstream_poll(); assert(game.input_history);
    sent=0; sample(2,0,62); netstream_service();
    sample(1,0,63); netstream_service();
    frame_reset(&parser); uint8_t histories=0;
    for(i=0;i<sent;++i) {
        int result=frame_feed(&parser,tx[i],out,sizeof(out),&len);
        assert(result>=0);
        if(result) {
            assert(out[0]==0x58 && out[1]==1);
            assert(out[2]==++histories+1 && len==3+2*out[2]);
            assert(out[4]==15 && out[6]==7);
            if(histories==2) assert(out[8]==14);
        }
    }
    assert(histories==2 && game.pending_count==3 && !reads_during_frame);
    diagnostics_checks();connection_checks();
    puts("netstream tests passed: sticky/API diagnostics, freshness pause/resume, rejoin, deadlines/wrap, contiguous enqueue, complete/partial burst quiet handoff, timeout, bounded busy retry, valid CRC and fragment delimiter");
    return 0;
}
