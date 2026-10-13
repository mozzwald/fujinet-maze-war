#include "playfield.h"
#include <assert.h>
#include <stdio.h>
static uint8_t pages[2][PLAYFIELD_PAGE_BYTES];
static uint8_t snapshot[21]={0x40,0,0x81,5,5,4,10,0,0,0,0,15,7,15,15,0,0,0,0,0,1};
static uint8_t shot[]={0x42,0,1,4,12,1,1};
static void draw(uint16_t tick) {
    game_input_tick(tick);
    playfield_draw(pages[tick&1],tick&1,&game,0);
}
static void buffered_first_draw_cleanup(void) {
    uint8_t page,x,y;
    game_reset(); playfield_invalidate();
    game.map_ready=game.snapshot_ready=1; game.local_pid=0; game.seat_mask=2;
    game.players[1].x=6; game.players[1].y=5;
    game.motion[1].serial=3;
    for (x=0;x<3;++x) {
        game.motion[1].x[x]=4+x; game.motion[1].y[x]=5;
        game.motion[1].at[x]=6*x;
    }
    /* On hardware the initial maze draw can receive several observations
       before the first remote actor is rendered. Start inside a segment. */
    draw(12); draw(13);
    game.motion[1].serial=4; game.motion[1].x[3]=8;
    game.motion[1].y[3]=5; game.motion[1].at[3]=24; game.players[1].x=8;
    for (page=0;page<2;++page) {
        game_input_tick(24);
        playfield_draw(pages[page],page,&game,0);
        for (y=25;y<30;++y)
            for (x=40;x<50;++x) {
                uint8_t byte=pages[page][(uint16_t)y*80+x/2];
                assert((x&1 ? byte&15 : byte>>4)==1);
            }
    }
}
int main(void) {
    uint8_t x,y;
    game_reset(); playfield_invalidate();
    game.have_welcome=game.round_authorized=game.map_ready=1;
    game.local_pid=0; game.round_id=1; game.seat_mask=3;
    game_input_tick(65528); assert(game_receive(snapshot,21)); draw(65528);
    game_input_tick(65534); snapshot[5]=5; assert(game_receive(snapshot,21));
    assert(game.players[1].x==5); /* model updates immediately */
    draw(1); playfield_visual_position(1,&x,&y); assert(x==36 && y==50);
    draw(4); playfield_visual_position(1,&x,&y); assert(x==40);
    draw(100); playfield_visual_position(1,&x,&y); assert(x==40); /* no extrapolation */
    game.motion[1].serial=65535u;
    assert(game_receive(snapshot,21));
    assert(game.motion[1].serial==GAME_MOTION_POINTS);
    /* Even two observed endpoints cannot justify smoothing through a wall. */
    game.motion[1].serial=2;
    game.motion[1].x[0]=5; game.motion[1].y[0]=10; game.motion[1].at[0]=100;
    game.motion[1].x[1]=7; game.motion[1].y[1]=10; game.motion[1].at[1]=106;
    game.players[1].x=7;
    game.bricks[206>>3]|=1u<<(206&7);
    draw(109); playfield_visual_position(1,&x,&y); assert(x==40 && y==50);
    draw(112); playfield_visual_position(1,&x,&y); assert(x==56 && y==50);
    /* Death is immediate even with buffered live positions. */
    snapshot[5]=snapshot[6]=255; assert(game_receive(snapshot,21)); draw(113);
    assert(game.players[1].respawning && game.deaths[1].active);
    /* Shots interpolate observed travel; duplicate coordinates cannot shift
       its timestamp, and a clear erases both pages immediately on redraw. */
    game_input_tick(200); assert(game_receive(shot,7)); draw(200);
    assert(playfield_shot_position(0,1,&x,&y) && x==35 && y==62);
    game_input_tick(206); shot[3]=5; assert(game_receive(shot,7)); draw(206);
    assert(game.shots[1].x==5 && game.shots[1].at==206);
    assert(playfield_shot_position(0,1,&x,&y) && x==39);
    game_input_tick(207); assert(game_receive(shot,7)); draw(207);
    assert(game.shots[1].at==206);
    assert(playfield_shot_position(1,1,&x,&y) && x==40);
    draw(209); assert(playfield_shot_position(1,1,&x,&y) && x==43);
    draw(240); assert(playfield_shot_position(0,1,&x,&y) && x==43);
    shot[5]=0; assert(game_receive(shot,7)); draw(240); draw(241);
    assert(!playfield_shot_position(0,1,&x,&y) && !playfield_shot_position(1,1,&x,&y));
    /* A new shot/generation must never interpolate from the old projectile. */
    shot[3]=10; shot[5]=1; assert(game_receive(shot,7)); draw(242);
    assert(playfield_shot_position(0,1,&x,&y) && x==83);
    game.bricks[250>>3]|=1u<<(250&7); /* current projectile cell */
    draw(243); assert(!playfield_shot_position(1,1,&x,&y));
    /* Missing clear followed by a new same-facing shot behind its old cell
       must not animate backwards across the maze. */
    game.bricks[250>>3]=0;
    game_input_tick(248); shot[3]=9; assert(game_receive(shot,7)); draw(248);
    assert(playfield_shot_position(0,1,&x,&y) && x==75);
    puts("observation checks passed: buffered remote motion, clock wrap, model authority, immediate death/clear, shot timing/duplicate/restart and walls");
    buffered_first_draw_cleanup();
    puts("buffered first remote draw leaves no old sprite pixels on either page");
    return 0;
}
