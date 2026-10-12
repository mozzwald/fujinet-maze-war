#include "playfield.h"
#include "suzy_host.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t framebuffer[PLAYFIELD_PAGE_BYTES + 8];
static uint8_t framebuffer1[PLAYFIELD_PAGE_BYTES + 8];
static uint8_t wall_background[PLAYFIELD_STRIDE * PLAYFIELD_H];
static uint8_t blocked_frame[PLAYFIELD_H * PLAYFIELD_STRIDE];

static uint8_t pen(uint8_t x, uint8_t y)
{
    uint8_t byte = framebuffer[4 + (uint16_t)y * PLAYFIELD_STRIDE + (x >> 1)];
    return (x & 1) ? (byte & 15) : (byte >> 4);
}

static void wall(struct game_state *state, uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * 20 + x;
    state->bricks[index >> 3] |= (uint8_t)(1u << (index & 7));
}

static void assert_walls_unchanged(const struct game_state *state)
{
    uint8_t x;
    uint8_t y;
    uint16_t cell;
    uint16_t offset;
    uint8_t expected;
    for (y = 0; y < PLAYFIELD_H; ++y) {
        for (x = 0; x < PLAYFIELD_W; ++x) {
            cell = (uint16_t)(y / 5) * 20 + x / 8;
            if (!(state->bricks[cell >> 3] & (1u << (cell & 7)))) continue;
            offset = (uint16_t)y * PLAYFIELD_STRIDE + (x >> 1);
            expected = wall_background[offset];
            expected = (x & 1) ? (expected & 15) : (expected >> 4);
            assert(pen(x, y) == expected);
        }
    }
}

int main(void)
{
    struct game_state state;
    uint8_t i;
    uint16_t offset;
    memset(&state, 0, sizeof(state));
    memset(framebuffer, 0xa5, sizeof(framebuffer));
    state.map_ready = 1;
    state.snapshot_ready = 1;
    wall(&state, 0, 0);
    wall(&state, 1, 1);
    state.seat_mask = 1;
    state.local_pid = 0;
    state.input_joy = 15;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.players[0].joy = 7;
    state.shots[0].active = 1;
    state.shots[0].x = 6;
    state.shots[0].y = 6;
    playfield_invalidate();
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(0, 0) == 13 || pen(0, 0) == 14);
    assert(pen(8, 5) == 9);
    assert(pen(39, 25) == 1);
    assert(pen(44, 27) == 15); /* right-facing eye */
    assert(pen(42, 25) == 10); /* slot 0 green, matching Atari PLR0COL */
    assert(pen(51, 32) == 9);  /* shot spark */
    assert(pen(0, 95) == 0xa && pen(159, 101) == 5);
    for (i = 0; i < 4; ++i) {
        assert(framebuffer[i] == 0xa5);
        assert(framebuffer[PLAYFIELD_PAGE_BYTES + 4 + i] == 0xa5);
    }
    state.players[0].joy = 11;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(43, 27) == 15); /* mirrored left-facing eye */
    state.seat_mask = 15;
    for (i = 0; i < 4; ++i) {
        state.players[i].x = (uint8_t)(5 + i * 2);
        state.players[i].y = 5;
        state.players[i].joy = 7;
    }
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(42, 25) == 10); /* Atari $C8: green */
    assert(pen(58, 25) == 14); /* Atari $86: blue */
    assert(pen(74, 25) == 12); /* Atari $58: purple */
    assert(pen(90, 25) == 8);  /* Atari $28: orange */
    state.players[0].x = 6;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(42, 25) == 1);  /* previous sprite was restored */
    assert(pen(45, 25) == 10); /* only three pixels this frame */
    playfield_draw(framebuffer + 4, 0, &state, 0);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(50, 25) == 10); /* reached the next cell smoothly */
    /* Held movement advances between server snapshots without moving the
       authoritative cell. A wall stops the visual prediction. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.players[0].joy = 7;
    state.input_joy = 7;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(44, 25) == 10);
    assert(state.players[0].x == 5);
    wall(&state, 6, 5);
    for (i = 0; i < 8; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(49, 25) == 9); /* wall did not admit the sprite */

    /* A neighboring wall must stop the entire sprite at the cell edge,
       rather than letting its center advance while the leading pixels clip. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.players[0].joy = 7;
    state.input_joy = 7;
    wall(&state, 6, 5);
    wall(&state, 5, 6);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    memcpy(blocked_frame, framebuffer + 4, sizeof(blocked_frame));
    for (i = 0; i < 5; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(memcmp(blocked_frame, framebuffer + 4, sizeof(blocked_frame)) == 0);
    state.input_joy = 13;
    for (i = 0; i < 5; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(memcmp(blocked_frame, framebuffer + 4, sizeof(blocked_frame)) == 0);

    /* Turning in an L-shaped open route finishes the horizontal step first,
       then moves vertically without crossing the blocked diagonal cell. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.players[0].joy = 7;
    state.input_joy = 15;
    wall(&state, 5, 6);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    state.players[0].x = 6;
    state.input_joy = 7;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    state.players[0].y = 6;
    state.input_joy = 13;
    for (i = 0; i < 8; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(50, 30) == 10);
    assert(pen(42, 30) == 9);

    /* Neither the sprite nor firing cue may replace a wall pixel during a
       transition between 8x5 cells. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.players[0].joy = 7;
    state.input_joy = 15;
    wall(&state, 5, 4);
    wall(&state, 5, 6);
    wall(&state, 6, 5);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    memcpy(wall_background, framebuffer + 4, sizeof(wall_background));
    state.seat_mask = 1;
    state.input_joy = 7;
    for (i = 0; i < 5; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert_walls_unchanged(&state);
    state.input_joy = 0x17;
    state.fire_cue_frames = 1;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert_walls_unchanged(&state);
    /* A delayed authoritative correction across a wall must rejoin the
       server cell, rather than freezing the visual sprite on the old side. */
    state.input_joy = 15;
    state.fire_cue_frames = 0;
    state.players[0].x = 7;
    for (i = 0; i < 8; ++i)
        playfield_draw(framebuffer + 4, 0, &state, 0);
    assert_walls_unchanged(&state);
    assert(pen(58, 25) == 10);
    assert(pen(42, 25) == 1);

    /* The same correction must happen while Right remains held. The old
       prediction path returned before considering a newer server position. */
    playfield_invalidate();
    state.players[0].x = 5;
    state.input_joy = 7;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    state.players[0].x = 7;
    state.shots[0].active = 1;
    state.shots[0].x = 8;
    state.shots[0].y = 5;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(58, 25) == 10);
    assert(pen(67, 27) == 9);
    assert_walls_unchanged(&state);

    /* An authoritative sideways move must also win over a held Up input;
       otherwise a freely moving prediction can diverge indefinitely. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.input_joy = 14;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    state.players[0].x = 6;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(50, 25) == 10);
    assert(pen(42, 25) == 1);

    /* Firing from a predicted position must bring the sprite back to the
       authoritative shot origin before the firing frame is drawn. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.local_pid = 0;
    state.players[0].x = 5;
    state.players[0].y = 5;
    state.input_joy = 7;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(44, 25) == 10);
    state.input_joy = 0x17;
    playfield_draw(framebuffer + 4, 0, &state, 0);
    assert(pen(42, 25) == 10);
    assert(pen(44, 25) == 1);

    /* Suzy paints the last row, feet and textured border on both pages;
       the seven HUD rows remain untouched. */
    playfield_invalidate();
    memset(&state, 0, sizeof(state));
    memset(framebuffer, 0xa5, sizeof(framebuffer));
    memset(framebuffer1, 0xa5, sizeof(framebuffer1));
    state.map_ready = state.snapshot_ready = 1;
    state.seat_mask = 1;
    state.input_joy = 15;
    state.players[0].x = 5;
    state.players[0].y = 17;
    wall(&state, 5, 18);
    assert(playfield_draw(framebuffer + 4, 0, &state, 0));
    assert(pen(41, 89) == 10);
    assert(pen(40, 90) == 13);
    assert(pen(43, 90) == 14);
    for (offset = PLAYFIELD_H * PLAYFIELD_STRIDE;
         offset < PLAYFIELD_PAGE_BYTES; ++offset)
        assert(framebuffer[4 + offset] == 0xa5);
    assert(playfield_draw(framebuffer1 + 4, 1, &state, 0));
    assert((framebuffer1[4 + 89 * PLAYFIELD_STRIDE + (41 >> 1)] & 15) == 10);
    assert((framebuffer1[4 + 90 * PLAYFIELD_STRIDE + (43 >> 1)] & 15) == 14);
    assert(suzy_test_max_batch() == 8);
    puts("playfield tests passed (native Suzy, maximum batch eight)");
    return 0;
}
