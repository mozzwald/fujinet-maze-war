#include "suzy_host.h"
#include "bench.h"
#include "../src/playfield.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAMES 60
static uint8_t pages[2][PLAYFIELD_PAGE_BYTES];
static uint8_t expected[FRAMES][PLAYFIELD_STRIDE * PLAYFIELD_H];
static uint8_t *target;
uint8_t bench_measuring;
unsigned long bench_max_batch;
unsigned long bench_now(void) { return 0; }

extern uint8_t cpu_playfield_draw(uint8_t *, uint8_t,
                                 const struct game_state *, void (*)(void));
extern const uint8_t *cpu_playfield_bottom_row(uint8_t);

void tgi_sprite(SCB_REHV_PAL *scb)
{
    uint8_t *data;
    unsigned x, y, n, i, pen, color;
    uint16_t offset;
    do {
        assert(scb->hsize == 0x100 && scb->vsize == 0x100);
        assert((scb->sprctl1 & (LITERAL | REHV)) == (LITERAL | REHV));
        data = scb->data;
        y = (unsigned)scb->vpos;
        while (*data) {
            n = (*data++ - 1) * 2;
            for (i = 0; i < n; ++i) {
                pen = (i & 1) ? (data[i >> 1] & 15) : (data[i >> 1] >> 4);
                if (i == n - 1 && !pen) break; /* literal trailing zero */
                color = (pen & 1) ? (scb->penpal[pen >> 1] & 15) :
                                     (scb->penpal[pen >> 1] >> 4);
                if ((scb->sprctl0 & 7) == TYPE_NONCOLL && !color) continue;
                x = (unsigned)scb->hpos + i;
                if (x >= 160 || y >= 102) continue;
                offset = (uint16_t)y * 80 + (x >> 1);
                if (x & 1) target[offset] = (target[offset] & 0xf0) | color;
                else target[offset] = (target[offset] & 15) | (color << 4);
            }
            data += n / 2;
            ++y;
        }
        scb = (SCB_REHV_PAL *)scb->next;
    } while (scb);
}

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
    if (frame >= 4 && frame < 28 && !(frame & 1))
        brick(state, 2 + (frame - 4) / 2, 6, 0);
    for (i = 0; i < 4; ++i) {
        state->players[i].x = 3 + (frame / 6 + i) % 10;
        state->shots[i].x = 2 + (frame / 2 + i) % 16;
        state->shots[i].y = 4 + i * 4;
        state->shots[i].active = frame % 12 < 8;
    }
}

int main(void)
{
    struct game_state state;
    uint8_t frame, page, mode, y;
    bench_mode = 0;
    setup(&state);
    playfield_invalidate();
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
    for (mode = 1; mode < 5; ++mode) {
        bench_mode = mode;
        bench_batch_limit = mode == 1 ? 8 : (uint8_t)(4u << (mode - 2));
        setup(&state);
        playfield_invalidate();
        memset(pages, 0, sizeof(pages));
        for (frame = 0; frame < FRAMES; ++frame) {
            page = frame & 1;
            target = pages[page];
            update(&state, frame);
            playfield_draw(target, page, &state, 0);
            if (mode == 1 && !page)
                for (y = 89; y < 95; ++y)
                    memcpy(target + (uint16_t)y * 80,
                           cpu_playfield_bottom_row(y), 80);
            if (memcmp(expected[frame], target, sizeof(expected[frame]))) {
                fprintf(stderr, "pixel mismatch: mode=%u frame=%u page=%u\n",
                        mode, frame, page);
                return 1;
            }
        }
    }
    puts("Cache pixel tests passed: 240 frames, CPU and Suzy, both pages, moving actors, "
         "shots, brick removals, feet, and border texture");
    return 0;
}
