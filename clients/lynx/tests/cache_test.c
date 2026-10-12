#include "suzy_host.h"
#include "../src/playfield.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAMES 60
static uint8_t pages[2][PLAYFIELD_PAGE_BYTES];
static uint8_t expected[FRAMES][PLAYFIELD_STRIDE * PLAYFIELD_H];

extern void cpu_playfield_invalidate(void);
extern uint8_t cpu_playfield_draw(uint8_t *, uint8_t,
                                 const struct game_state *, void (*)(void));
extern const uint8_t *cpu_playfield_bottom_row(uint8_t);

static void brick(struct game_state *state, uint8_t x, uint8_t y, uint8_t set)
{
    uint16_t cell = (uint16_t)y * 20 + x;
    if (set) state->bricks[cell >> 3] |= (uint8_t)(1u << (cell & 7));
    else state->bricks[cell >> 3] &= (uint8_t)~(1u << (cell & 7));
}

static void setup(struct game_state *state)
{
    uint8_t i, x, y;
    memset(state, 0, sizeof(*state));
    state->map_ready = state->snapshot_ready = 1;
    state->seat_mask = 15;
    state->input_joy = 15;
    for (y = 0; y < 19; ++y)
        for (x = 0; x < 20; ++x)
            if (!x || x == 19 || !y || y == 18 || y == 6 || y == 10)
                brick(state, x, y, 1);
    for (i = 0; i < 4; ++i) {
        state->players[i].x = 3 + i;
        state->players[i].y = i == 3 ? 17 : 3 + i * 4;
        state->players[i].joy = i & 1 ? 11 : 7;
    }
}

static void update(struct game_state *state, uint8_t frame)
{
    uint8_t i;
    state->fire_cue_frames = frame % 9 == 0;
    if (frame >= 4 && frame < 28 && !(frame & 1))
        brick(state, 2 + (frame - 4) / 2, 6, 0);
    for (i = 0; i < 4; ++i) {
        state->players[i].x = 3 + (frame / 6 + i) % 10;
        state->shots[i].x = 2 + (frame / 2 + i) % 16;
        state->shots[i].y = 4 + i * 4;
        state->shots[i].active = frame % 12 < 8;
    }
}

static struct game_state *live_state;
static uint8_t changed_during_draw;
static void mutate_map(void)
{
    if (changed_during_draw) return;
    brick(live_state, 2, 6, 0);
    changed_during_draw = 1;
}

static void transition_tests(void)
{
    struct game_state state;
    uint8_t y;
    setup(&state);
    live_state = &state;
    cpu_playfield_invalidate();
    memset(pages, 0, sizeof(pages));
    cpu_playfield_draw(pages[0], 0, &state, 0);
    for (y = 89; y < 95; ++y)
        memcpy(pages[0] + (uint16_t)y * 80, cpu_playfield_bottom_row(y), 80);
    memcpy(expected[0], pages[0], sizeof(expected[0]));
    playfield_invalidate();
    playfield_draw(pages[0], 0, &state, mutate_map);
    assert(changed_during_draw);
    assert(memcmp(expected[0], pages[0], sizeof(expected[0])) == 0);
    /* The packet applied during drawing belongs to the next frame. */
    cpu_playfield_invalidate();
    cpu_playfield_draw(pages[1], 1, &state, 0);
    memcpy(expected[0], pages[1], sizeof(expected[0]));
    playfield_draw(pages[1], 1, &state, 0);
    assert(memcmp(expected[0], pages[1], sizeof(expected[0])) == 0);
    /* Clearing a diagnostics page must force its full maze to return. */
    memset(pages[1], 0xa5, sizeof(pages[1]));
    playfield_invalidate_page(1);
    playfield_draw(pages[1], 1, &state, 0);
    assert(memcmp(expected[0], pages[1], sizeof(expected[0])) == 0);
    /* A new round may reuse the same map but must reset visual positions. */
    ++state.round_id;
    state.players[0].x = 15;
    cpu_playfield_invalidate();
    cpu_playfield_draw(pages[1], 1, &state, 0);
    memcpy(expected[0], pages[1], sizeof(expected[0]));
    playfield_draw(pages[1], 1, &state, 0);
    assert(memcmp(expected[0], pages[1], sizeof(expected[0])) == 0);
    state.snapshot_ready = 0;
    cpu_playfield_draw(pages[1], 1, &state, 0);
    memcpy(expected[0], pages[1], sizeof(expected[0]));
    playfield_draw(pages[1], 1, &state, 0);
    assert(memcmp(expected[0], pages[1], sizeof(expected[0])) == 0);
}

int main(void)
{
    struct game_state state;
    uint8_t frame, page, y;
    setup(&state);
    cpu_playfield_invalidate();
    memset(pages, 0, sizeof(pages));
    for (frame = 0; frame < FRAMES; ++frame) {
        page = frame & 1;
        update(&state, frame);
        cpu_playfield_draw(pages[page], page, &state, 0);
        if (!page)
            for (y = 89; y < 95; ++y)
                memcpy(pages[page] + (uint16_t)y * 80,
                       cpu_playfield_bottom_row(y), 80);
        memcpy(expected[frame], pages[page], sizeof(expected[frame]));
    }
    setup(&state);
    playfield_invalidate();
    memset(pages, 0, sizeof(pages));
    for (frame = 0; frame < FRAMES; ++frame) {
        page = frame & 1;
        update(&state, frame);
        playfield_draw(pages[page], page, &state, 0);
        assert(memcmp(expected[frame], pages[page], sizeof(expected[frame])) == 0);
    }
    transition_tests();
    puts("Production Suzy cache matches CPU: 60 frames, serial mutation, page reset, new round, lost snapshot");
    return 0;
}
