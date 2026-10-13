#include "protocol.h"
#include "playfield.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t pages[2][PLAYFIELD_PAGE_BYTES], out[11];
static void draw(uint16_t tick, uint8_t *x, uint8_t *y)
{
    game_input_tick(tick);
    playfield_draw(pages[tick&1],tick&1,&game,0);
    playfield_visual_position(0,x,y);
}
static void corner(uint16_t start)
{
    uint8_t x,y;
    game_reset(); playfield_invalidate();
    game.have_welcome=game.round_authorized=game.snapshot_ready=game.map_ready=1;
    game.local_pid=0; game.round_id=1; game.seat_mask=game.name_echoed=1;
    game.players[0].x=game.players[0].y=5;
    /* Inside corner: diagonal/X-then-Y shortcuts would enter this wall. */
    game.bricks[85>>3]|=1u<<(85&7); /* cell 5,4 */
    draw(start,&x,&y); assert(x==40 && y==25);
    game_sample_input(2,0,start);
    assert(game_next_outbound(out,sizeof(out))==5);
    game_sample_input(1,0,start);
    assert(game_next_outbound(out,sizeof(out))==5);
    game_sample_input(0,0,start);
    assert(game.players[0].x==6 && game.players[0].y==4);
}
int main(void)
{
    uint8_t x,y,fast[13][2],i;
    uint8_t snapshot[21]={0x40,0,0x81,6,5,0,0,0,0,0,0,15,15,15,15,0,0,0,0,0,1};
    corner(100);
    for(i=0;i<=12;++i) {
        draw(100+i,&x,&y); fast[i][0]=x; fast[i][1]=y;
        assert((y==25 && x>=40 && x<=48) || (x==48 && y>=20 && y<=25));
        draw(100+i,&x,&y); /* repeated render at same clock cannot move */
        assert(x==fast[i][0] && y==fast[i][1]);
    }
    /* Two queued legs catch up to the inside corner, then animate only the
       latest legal leg. Never interpolate diagonally or replay both legs. */
    assert(fast[1][0]==48 && fast[1][1]==24);
    assert(fast[3][0]==48 && fast[3][1]==20);
    assert(fast[4][0]==48 && fast[4][1]==20);
    assert(fast[6][0]==48 && fast[6][1]==20);
    assert(x==48 && y==20);
    corner(100);
    for(i=0;i<=12;i+=3) {
        draw(100+i,&x,&y);
        assert(x==fast[i][0] && y==fast[i][1]); /* 20 vs 60 renders/sec */
    }
    /* A cumulative ACK/replay that retains the predicted endpoint must
       preserve the in-flight last leg, rather than restart its deadline. */
    corner(100); draw(102,&x,&y);
    snapshot[19]=0; assert(game_receive(snapshot,sizeof(snapshot)));
    assert(game.motion[0].serial==2);
    draw(103,&x,&y); assert(x==48 && y==20);
    draw(112,&x,&y); assert(x==48 && y==20);
    /* An authoritative correction immediately rejoins without stale motion. */
    snapshot[3]=5; snapshot[4]=5; snapshot[19]=1;
    assert(game_receive(snapshot,sizeof(snapshot)));
    draw(113,&x,&y); assert(x==40 && y==25);
    assert(game.local_corrections==1);
    /* Smooth a one-cell correction without delaying the actual model. The
       next identical snapshot must not restart the three-tick deadline. */
    snapshot[3]=6;
    assert(game_receive(snapshot,sizeof(snapshot)));
    assert(game.players[0].x==6);
    draw(113,&x,&y); assert(x==40 && y==25);
    draw(114,&x,&y); assert(x==42 && y==25);
    assert(game_receive(snapshot,sizeof(snapshot)));
    draw(115,&x,&y); assert(x==45 && y==25);
    draw(116,&x,&y); assert(x==48 && y==25);
    corner(65530u);
    draw(65533u,&x,&y); assert(x==48 && y==20);
    draw(0,&x,&y); assert(x==48 && y==20);
    corner(100); draw(101,&x,&y);
    game_sample_input(0,1,101); /* bare A must not snap an unfinished walk */
    assert(game_next_outbound(out,sizeof(out))==5);
    draw(101,&x,&y); assert(x==48 && y==24);
    game_sample_input(2,1,101);
    assert(game_next_outbound(out,sizeof(out))==5);
    draw(101,&x,&y); assert(x==48 && y==20); /* muzzle rejoin */
    /* Held repeat still takes six ticks: faster taps must not speed up
       continuous movement or introduce another transmitted input. */
    corner(100); draw(106,&x,&y);
    game_sample_input(2,0,115);
    assert(game_next_outbound(out,sizeof(out))==5); /* explicit turn: 3 ticks */
    draw(118,&x,&y); assert(x==56 && y==20);
    snapshot[3]=7; snapshot[4]=4; snapshot[19]=out[1];
    assert(game_receive(snapshot,sizeof(snapshot)));
    game_input_tick(130); assert(game_next_outbound(out,sizeof(out))==5);
    draw(130,&x,&y); assert(x==56 && y==20);
    draw(133,&x,&y); assert(x==60 && y==20);
    draw(136,&x,&y); assert(x==64 && y==20);
    /* A burst of six retained cells is not a six-cell playback queue after
       release. Catch up to the final legal leg and finish at its timestamp.
       This also covers remote snapshots that expand several cells at once. */
    game_reset(); playfield_invalidate();
    game.local_pid=0; game.seat_mask=1; game.snapshot_ready=game.map_ready=1;
    game.players[0].x=1; game.players[0].y=10;
    draw(100,&x,&y);
    for(i=0;i<6;++i) {
        unsigned n=i%GAME_MOTION_POINTS;
        game.motion[0].x[n]=2+i; game.motion[0].y[n]=10;
        game.motion[0].at[n]=130; game.motion[0].ticks[n]=6;
    }
    game.motion[0].serial=6; game.players[0].x=7;
    draw(130,&x,&y); assert(x==48 && y==50);
    draw(133,&x,&y); assert(x==52 && y==50);
    draw(136,&x,&y); assert(x==56 && y==50);
    draw(200,&x,&y); assert(x==56 && y==50);
    /* A remote destination arriving midway through interpolation must not
       first jump to the old destination. Ease the whole clear gap instead. */
    game_reset(); playfield_invalidate();
    game.local_pid=1; game.seat_mask=1; game.snapshot_ready=game.map_ready=1;
    game.players[0].x=game.players[0].y=5;
    draw(100,&x,&y); assert(x==40 && y==25);
    game.motion[0].serial=1; game.motion[0].x[0]=6; game.motion[0].y[0]=5;
    game.motion[0].at[0]=100; game.motion[0].ticks[0]=6;
    game.players[0].x=6;
    draw(100,&x,&y); assert(x==40);
    draw(103,&x,&y); assert(x==44);
    game.motion[0].serial=2; game.motion[0].x[1]=7; game.motion[0].y[1]=5;
    game.motion[0].at[1]=103; game.motion[0].ticks[1]=6;
    game.players[0].x=7;
    draw(103,&x,&y); assert(x==44 && y==25); /* no boundary jump */
    draw(106,&x,&y); assert(x==50 && y==25);
    draw(109,&x,&y); assert(x==56 && y==25);
    /* A two-cell local correction has a fixed 50 ms deadline, not two cell
       intervals. An intervening wall must prevent this smoothing path. */
    game_reset(); playfield_invalidate();
    game.local_pid=0; game.seat_mask=1; game.snapshot_ready=game.map_ready=1;
    game.players[0].x=game.players[0].y=5;
    draw(100,&x,&y);
    ++game.motion[0].epoch; game.players[0].x=7;
    draw(100,&x,&y); assert(x==40);
    draw(101,&x,&y); assert(x==45);
    draw(103,&x,&y); assert(x==56);
    game.bricks[106>>3]|=1u<<(106&7); /* wall 6,5 between clear ends */
    ++game.motion[0].epoch; game.players[0].x=5;
    draw(104,&x,&y); assert(x==40 && y==25); /* rejoin, no wall pixels */
    puts("motion tests passed: bounded local leg, remote jitter retargeting, safe corners/wall gaps, ACK retention, bounded correction easing, fire and wrap");
    return 0;
}
