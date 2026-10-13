#include "protocol.h"
#include "sound.h"
#include "sound_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct sound_registers sound_channels[4];
uint8_t sound_stereo, sound_panning;
static uint16_t ticks;
static void update(uint16_t delta) { ticks+=delta; game_input_tick(ticks); sound_update(); }
static void silent(void)
{
    uint8_t i; for(i=0;i<4;++i) assert(sound_channels[i].volume==0);
}
static void setup(void)
{
    uint8_t i;
    game_reset(); sound_init(); ticks=100;
    game.have_welcome=game.round_authorized=game.snapshot_ready=game.map_ready=1;
    game.round_id=1; game.local_pid=0; game.seat_mask=15; game.name_echoed=1;
    for(i=0;i<4;++i) { game.players[i].x=3+i*4; game.players[i].y=5; game.players[i].joy=7; }
    update(0);
    assert(sound_stereo==0 && sound_panning==0);
    for(i=0;i<4;++i) assert(sound_channels[i].control==0x1b);
}
static void shot(uint8_t id,uint8_t active)
{
    uint8_t p[]={0x42,0,id,4,5,active,1};
    /* SHOT opcode is 0x42 in the game protocol. */
    assert(game_receive(p,sizeof(p)));
}
static void respawn(uint8_t id,uint8_t flags,uint8_t x,uint8_t y)
{
    uint8_t p[]={0x52,0,id,x,y,flags,1}; assert(game_receive(p,sizeof(p)));
}
static void reliable(uint16_t revision,const uint8_t *p,uint8_t len)
{
    uint8_t packet[64]={0x53,0,0,0}; packet[2]=revision; packet[3]=revision>>8;
    memcpy(packet+4,p,len); assert(game_receive(packet,len+4));
}
int main(void)
{
    uint8_t out[11], i, reload, brick[]={0x51,0,10,10,1}, start[]={0x55,2,10};
    uint8_t match[43]={0x54,1,0,15,0,10,10,7,4,2,0};
    uint8_t snapshot[]={0x40,0,1,3,5,7,5,11,5,15,5,7,7,7,7,0,0,0,0,0,1};
    setup(); silent();
    /* Successful predicted movement produces a quiet shuffle. Replaying a
       pending input on identical snapshots cannot replay its audio event. */
    game_sample_input(2,0,ticks); assert(game_next_outbound(out,11)==5);
    assert(game.sound_events[0]&GAME_SOUND_STEP); update(0);
    assert(sound_channels[0].volume==24 && sound_channels[0].feedback!=1);
    update(2); assert(sound_channels[0].volume==0);
    assert(game_receive(snapshot,sizeof(snapshot))); assert(!game.sound_events[0]);
    game.pending_count=0; game.intent_count=0;
    /* A wall suppresses footsteps along with prediction. */
    game.bricks[105/8]|=1u<<(105%8); game.players[0].x=4; game.players[0].y=5;
    game_set_joy(15); game_set_joy(7); game_next_outbound(out,11);
    assert(!game.sound_events[0]);
    update(20);
    /* A alone is silent. Pressing a direction with A held sends valid fire,
       but audio waits for actual projectile creation, never a bare trigger. */
    game_set_joy(0x1f); assert(game_next_outbound(out,11)==5); update(0);
    silent(); assert(!game.fire_cue_frames);
    game_set_joy(0x17); assert(game_next_outbound(out,11)==5); update(0);
    silent(); assert(game.fire_cue_frames);
    shot(0,1); update(0);
    assert(sound_channels[0].feedback==1 && sound_channels[0].volume==64);
    reload=sound_channels[0].reload; update(3);
    assert(sound_channels[0].reload>reload);
    shot(0,1); update(0); /* local network echo does not restart the zip */
    assert(sound_channels[0].reload>reload);
    update(5); assert(sound_channels[0].volume==0);
    shot(0,1); update(0); assert(sound_channels[0].volume==0);
    shot(0,0); shot(0,1); update(0); assert(sound_channels[0].volume==64);
    shot(1,1); update(0); assert(sound_channels[1].volume==32);
    update(10); silent();
    shot(1,0); update(0);
    assert(sound_channels[1].volume==28 && sound_channels[1].feedback!=1);
    update(2); reload=sound_channels[1].reload;
    shot(1,0); update(0); assert(sound_channels[1].reload==reload);
    update(14); silent();
    respawn(1,1,0,0); update(0);
    assert(sound_channels[1].volume==32 && sound_channels[1].feedback!=1);
    update(8); reload=sound_channels[1].reload;
    respawn(1,1,0,0); update(0); assert(sound_channels[1].reload==reload);
    /* A quiet step and shot cannot replace a death envelope. */
    game.sound_events[1]|=GAME_SOUND_STEP|GAME_SOUND_SHOT; update(0);
    assert(sound_channels[1].feedback!=1);
    update(24); silent();
    respawn(1,3,7,5); update(0); assert(sound_channels[1].feedback==1);
    reload=sound_channels[1].reload; update(2);
    assert(sound_channels[1].reload!=reload);
    respawn(1,3,7,5); update(0); assert(sound_channels[1].reload!=reload);
    update(20); silent();
    game.bricks[210/8]|=1u<<(210%8);
    assert(game_receive(brick,sizeof(brick))); update(0);
    assert(sound_channels[3].volume && sound_channels[3].feedback!=1);
    update(16); silent();
    assert(game_receive(brick,sizeof(brick))); update(0); silent();
    /* Round losers and winner smoke get the same death sound as gameplay;
       frozen duplicate results and final score display remain quiet. */
    for(i=0;i<4;++i) memcpy(match+11+8*i,"PLAYER  ",8);
    reliable(1,match,sizeof(match)); update(0);
    assert(!sound_channels[0].volume && sound_channels[1].volume);
    update(GAME_DEATH_TICKS); silent();
    reliable(2,match,sizeof(match)); update(0); silent();
    update(GAME_WINNER_DANCE_TICKS);
    assert(sound_channels[0].volume && !sound_channels[1].volume);
    update(GAME_DEATH_TICKS); silent();
    reliable(3,start,sizeof(start)); update(0); silent();
    /* Timers expire across wrap, and a delayed service never loops through
       all the missed envelope steps or leaves a held tone. */
    game.map_ready=game.snapshot_ready=1; ticks=65534; update(0);
    game.sound_events[0]=GAME_SOUND_SHOT; update(0);
    assert(sound_channels[0].volume); update(100); silent();
    puts("sound tests passed: movement/walls/replay, falling shot, local echo, remote gain, death/respawn duplicates, priorities, brick once, round smoke/results, clock wrap and late service");
    return 0;
}
