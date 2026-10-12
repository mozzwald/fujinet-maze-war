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
    assert(fast[3][0]==44 && fast[3][1]==25);
    assert(fast[6][0]==48 && fast[6][1]==25);
    assert(fast[9][0]==48 && fast[9][1]==23);
    assert(x==48 && y==20);
    corner(100);
    for(i=0;i<=12;i+=3) {
        draw(100+i,&x,&y);
        assert(x==fast[i][0] && y==fast[i][1]); /* 20 vs 60 renders/sec */
    }
    /* A cumulative ACK/replay that retains the predicted endpoint must
       preserve the in-flight corner, rather than restart or add its path. */
    corner(100); draw(103,&x,&y);
    snapshot[19]=0; assert(game_receive(snapshot,sizeof(snapshot)));
    assert(game.motion[0].serial==2);
    draw(106,&x,&y); assert(x==48 && y==25);
    draw(112,&x,&y); assert(x==48 && y==20);
    /* An authoritative correction immediately rejoins without stale motion. */
    snapshot[3]=5; snapshot[4]=5; snapshot[19]=1;
    assert(game_receive(snapshot,sizeof(snapshot)));
    draw(113,&x,&y); assert(x==40 && y==25);
    corner(65530u);
    draw(0,&x,&y); assert(x==48 && y==25);
    draw(6,&x,&y); assert(x==48 && y==20);
    corner(100); draw(103,&x,&y);
    game_sample_input(2,1,103);
    assert(game_next_outbound(out,sizeof(out))==5);
    draw(103,&x,&y); assert(x==48 && y==20); /* muzzle rejoin */
    puts("motion tests passed: elapsed time, 20/60 Hz equivalence, repeated frames, retained corner, ACK replay, correction, fire and wrap");
    return 0;
}
