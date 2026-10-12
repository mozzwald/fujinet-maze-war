#include "protocol.h"
#include "playfield.h"
#include "suzy_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pages[2][PLAYFIELD_PAGE_BYTES];
static uint8_t poses[4][20];
static uint8_t result[43] = {0x54,1,0,15,8,10,10,7,4,2,8};
static void reliable(uint16_t revision, const uint8_t *payload, uint8_t len)
{
    uint8_t packet[64] = {0x53,0,0,0};
    packet[2] = revision; packet[3] = revision >> 8;
    memcpy(packet+4,payload,len);
    assert(game_receive(packet,len+4));
}
static void draw(void)
{
    uint8_t page;
    for (page=0; page<2; ++page) {
        playfield_draw(pages[page],page,&game,0);
        assert(!memcmp(pages[page]+95*80,"\245\245\245\245",4));
    }
    assert(!memcmp(pages[0],pages[1],95*80));
}
static void region(uint8_t *out, uint8_t cell)
{
    uint8_t y;
    for (y=0; y<5; ++y) memcpy(out+4*y,pages[0]+(25+y)*80+cell*4,4);
}
int main(void)
{
    uint8_t i, pose, current[20], start[] = {0x55,2,10};
    uint8_t snapshot[] = {0x40,0,1,1,1,1,1,1,1,1,1,15,15,15,15,0,0,0,0,0,1};
    game_reset();
    game.have_welcome = game.round_authorized = game.snapshot_ready = game.map_ready = 1;
    game.round_id=1; game.seat_mask=15; game.local_pid=0;
    for(i=0;i<4;++i) {
        game.players[i].x=3+i*4; game.players[i].y=5;
        game.players[i].joy=7;
        memcpy(result+11+8*i,"PLAYER1 ",8); result[17+8*i]='1'+i;
    }
    memset(pages,0xa5,sizeof(pages)); playfield_invalidate();
    game_input_tick(65520);
    reliable(1,result,sizeof(result));
    assert(game.result_started==65520 && game.result_zombie_history==8);
    assert(game.phase && !game_ready() && game_round_stage(&game)==ROUND_LOSERS);
    draw(); region(current,7);
    assert(memcmp(current,"\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21\21",20));
    for(pose=0;pose<4;++pose) {
        game_input_tick((uint16_t)(65520UL+GAME_DEATH_TICKS+pose*8));
        assert(game_round_stage(&game)==ROUND_DANCE);
        draw(); region(poses[pose],3);
        region(current,7);
        for(i=0;i<20;++i) assert(current[i]==0x11); /* loser gone */
        if(pose) assert(memcmp(poses[pose-1],poses[pose],20));
    }
    /* Frozen results survive a fresh revision and late old-round state. */
    result[6]=1; result[11]='X'; reliable(2,result,sizeof(result));
    assert(game.result_started==65520 && game.players[0].score==10 && game.names[0][0]=='P');
    assert(game_receive(snapshot,sizeof(snapshot)));
    assert(game.players[0].x==3 && game.players[0].score==10);
    game_input_tick((uint16_t)(65520UL+GAME_DEATH_TICKS+GAME_WINNER_DANCE_TICKS));
    assert(game_round_stage(&game)==ROUND_WINNER_SMOKE); draw();
    region(current,3); assert(memcmp(current,poses[3],20));
    game_input_tick((uint16_t)(65520UL+2*GAME_DEATH_TICKS+GAME_WINNER_DANCE_TICKS));
    assert(game_round_stage(&game)==ROUND_RESULTS); draw();
    region(current,3); for(i=0;i<20;++i) assert(current[i]==0x11);
    reliable(3,start,sizeof(start));
    assert(!game.result_valid && !game.phase && game_round_stage(&game)==ROUND_NONE);
    assert(!game_ready());
    /* Returning from the results restores both pages. */
    game.map_ready=game.snapshot_ready=1; draw();
    region(current,3); assert(memcmp(current,poses[0],20));
    /* A new round can also interrupt the dance before its timer expires. */
    result[1]=2; reliable(4,result,sizeof(result));
    game_input_tick(game.input_clock+GAME_DEATH_TICKS);
    assert(game_round_stage(&game)==ROUND_DANCE); draw();
    start[1]=3; reliable(5,start,sizeof(start));
    game.map_ready=game.snapshot_ready=1; draw();
    assert(!game.result_valid && game_round_stage(&game)==ROUND_NONE);
    region(current,7); for(i=0;i<20 && current[i]==0x11;++i) {}
    assert(i<20); /* loser is alive again; no lingering presentation mask */
    /* An intermission join can show scores without valid maze positions. */
    game.phase=1; game.result_valid=1; game.snapshot_ready=0;
    assert(game_round_stage(&game)==ROUND_RESULTS);
    assert(suzy_test_max_batch()<=8);
    puts("round tests passed: loser smoke, four dance poses, winner smoke, frozen scores, duplicate results, clock wrap, next round, late join, both pages");
    return 0;
}
