#include "protocol.h"
#include "playfield.h"
#include "suzy_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t pages[2][PLAYFIELD_PAGE_BYTES];
static uint8_t walls[PLAYFIELD_STRIDE * PLAYFIELD_H];
static uint8_t stages[4][PLAYFIELD_STRIDE * PLAYFIELD_H];
static uint8_t expected[PLAYFIELD_PAGE_BYTES];
extern void cpu_playfield_invalidate(void);
extern uint8_t cpu_playfield_draw(uint8_t *, uint8_t,
                                 const struct game_state *, void (*)(void));

static void wall(uint8_t x, uint8_t y)
{
    uint16_t cell = (uint16_t)y * 20 + x;
    game.bricks[cell >> 3] |= 1u << (cell & 7);
}

static uint8_t pen(uint8_t page, uint8_t x, uint8_t y)
{
    uint8_t byte = pages[page][(uint16_t)y * 80 + x / 2];
    return x & 1 ? byte & 15 : byte >> 4;
}

static void draw(uint8_t page)
{
    uint16_t cell, offset;
    uint8_t x, y, before, after;
    playfield_draw(pages[page], page, &game, 0);
    for (y = 0; y < 95; ++y)
        for (x = 0; x < 160; ++x) {
            cell = (uint16_t)(y / 5) * 20 + x / 8;
            if (!(game.bricks[cell >> 3] & (1u << (cell & 7)))) continue;
            offset = (uint16_t)y * 80 + x / 2;
            before = walls[offset]; after = pages[page][offset];
            if (x & 1) assert((before & 15) == (after & 15));
            else assert((before >> 4) == (after >> 4));
        }
    for (offset = 95 * 80; offset < PLAYFIELD_PAGE_BYTES; ++offset)
        assert(pages[page][offset] == 0xa5);
}

static void setup(void)
{
    uint8_t x, y;
    game_reset();
    game.have_welcome = game.round_authorized = game.map_ready = game.snapshot_ready = 1;
    game.local_pid = 0; game.round_id = 1; game.name_echoed = 1;
    game.players[0].x = 5; game.players[0].y = 5;
    game.players[1].x = 6; game.players[1].y = 5;
    game.players[0].joy = game.players[1].joy = 7;
    for (y = 0; y < 19; ++y)
        for (x = 0; x < 20; ++x)
            if (x == 0 || x == 19 || y == 0 || y == 18 || y == 4 || y == 6)
                wall(x, y);
    playfield_invalidate();
    memset(pages, 0xa5, sizeof(pages));
    playfield_draw(pages[0], 0, &game, 0);
    memcpy(walls, pages[0], sizeof(walls));
    game.seat_mask = 3;
}

static void respawn(uint8_t id, uint8_t flags, uint8_t x, uint8_t y)
{
    uint8_t p[7] = {0x52, 0, id, x, y, flags, 1};
    assert(game_receive(p, sizeof(p)));
}

int main(void)
{
    uint8_t frame, page, out[11];
    uint8_t snap[21] = {0x40, 0, 1, 5,5,255,255,255,255,255,255,
                        15,15,15,15,0,0,0,0,0,1};
    setup();
    game_input_tick(100);
    respawn(1, 1, 0, 0);
    assert(game.deaths[1].active && game.deaths[1].x == 6 && game.deaths[1].y == 5);
    assert(game.players[1].respawning && game.players[1].x == 255);
    for (frame = 0; frame < 4; ++frame) {
        game_input_tick(100 + frame * 8);
        respawn(1, 1, 0, 0); /* repeated direct/reliable/echo indication */
        assert(game.deaths[1].started == 100);
        for (page = 0; page < 2; ++page) draw(page);
        memcpy(stages[frame], pages[0], sizeof(stages[frame]));
        if (frame) assert(memcmp(stages[frame - 1], stages[frame], sizeof(stages[frame])));
    }
    /* Protocol prediction and visual interpolation both ignore the corpse.
       Keep the smoke active while the green wizard enters the same cell. */
    game_input_tick(124);
    game_sample_input(2, 0, 124);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 7);
    assert(game.players[0].x == 6 && game.deaths[1].active);
    for (frame = 0; frame < 4; ++frame) {
        draw(0); draw(1);
    }
    game_input_tick(game.input_clock + GAME_MOVE_TICKS);
    draw(0); draw(1);
    assert(pen(0, 50, 25) == 10 && pen(1, 50, 25) == 10);
    game_sample_input(0, 0, 130);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    game_sample_input(2, 0, 131);
    assert(game_next_outbound(out, sizeof(out)) == 5);
    assert(game.players[0].x == 7);
    for (frame = 0; frame < 4; ++frame) { draw(0); draw(1); }
    game_input_tick(137);
    assert(!game.deaths[1].active && game.players[1].respawning);
    cpu_playfield_invalidate();
    cpu_playfield_draw(expected, 1, &game, 0);
    for (page = 0; page < 2; ++page) {
        draw(page);
        assert(!memcmp(expected, pages[page], PLAYFIELD_STRIDE * PLAYFIELD_H));
    }
    respawn(1, 1, 0, 0);
    assert(!game.deaths[1].active); /* expired echoes cannot restart smoke */
    respawn(1, 3, 10, 5);
    assert(!game.players[1].respawning && game.players[1].x == 10);
    respawn(1, 1, 0, 0); /* a genuinely new death can animate again */
    assert(game.deaths[1].active && game.deaths[1].x == 10);

    /* Snapshot-only disappearance starts smoke if the pending event is late.
       An older live snapshot cannot reintroduce a solid body. */
    setup();
    game_input_tick(65530u);
    assert(game_receive(snap, sizeof(snap)));
    assert(game.deaths[1].active && game.deaths[1].x == 6);
    snap[5] = 6; snap[6] = 5;
    assert(game_receive(snap, sizeof(snap)));
    assert(game.players[1].respawning && game.players[1].x == 255);
    game_input_tick(26);
    assert(!game.deaths[1].active); /* wrap-safe expiry */
    respawn(1, 3, 8, 5);
    assert(!game.players[1].respawning);

    /* Death cancels local movement debt; buttons pressed while dead are not
       deferred actions that execute after spawning. */
    respawn(0, 1, 0, 0);
    assert(!game.pending_count && !game.intent_count);
    game_sample_input(2, 0, 27);
    assert(!game.intent_count);
    assert(game_next_outbound(out, sizeof(out)) == 5 && out[3] == 15);
    /* Round changes clear every effect and allow initial placement again. */
    {
        uint8_t round[] = {0x53,0,1,0,0x55,2,10};
        assert(game_receive(round, sizeof(round)));
        assert(!game.deaths[0].active && !game.deaths[1].active);
        assert(!game.players[0].respawning && !game.players[1].respawning);
    }
    /* Edge and bottom-row puffs cannot paint walls or the HUD. */
    for (frame = 0; frame < 3; ++frame) {
        setup();
        game.players[1].x = frame == 0 ? 1 : frame == 1 ? 18 : 6;
        game.players[1].y = frame == 0 ? 1 : 17;
        game_input_tick(100);
        respawn(1, 1, 0, 0);
        for (page = 0; page < 2; ++page) draw(page);
        game_input_tick(132);
        cpu_playfield_invalidate();
        cpu_playfield_draw(expected, 1, &game, 0);
        for (page = 0; page < 2; ++page) {
            draw(page);
            assert(!memcmp(expected, pages[page], PLAYFIELD_STRIDE * PLAYFIELD_H));
        }
    }
    /* Joining a room with somebody already dead has no known death cell. */
    setup();
    game.snapshot_ready = 0;
    respawn(1, 1, 0, 0);
    assert(game.players[1].respawning && !game.deaths[1].active);
    assert(suzy_test_max_batch() == 8);
    puts("death tests passed: four stages, nonblocking cloud, player layering, wall/HUD clipping, both-page cleanup, duplicate events, respawn, snapshot fallback, clock wrap, round reset");
    return 0;
}
