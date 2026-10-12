/* Share the frozen pre-integration movement, collision, art and CPU renderer.
   This file is linked only into the benchmark cartridge. */
#ifdef LYNX_BENCH_HOST
#include "suzy_host.h"
#else
#include <lynx.h>
#include <tgi.h>
#endif
#include "../src/playfield.h"
#include "bench.h"

#define playfield_draw cpu_playfield_draw
#define playfield_invalidate cpu_playfield_invalidate
#define playfield_invalidate_page cpu_playfield_invalidate_page
#define playfield_bottom_row cpu_playfield_bottom_row
#include "reference_playfield.c"
#undef playfield_draw
#undef playfield_invalidate
#undef playfield_invalidate_page
#undef playfield_bottom_row

uint8_t bench_mode;
uint8_t bench_batch_limit = 8;
static uint8_t dirty_cpu[2][GAME_BRICK_BYTES];
static uint8_t cpu_pending[2];
#ifdef LYNX_BENCH_HOST
static uint8_t *host_cpu_page;
#endif
static uint8_t cached_bricks[2][GAME_BRICK_BYTES];
static uint8_t cached_valid[2];
static uint8_t cached_round[2];
static uint8_t cached_ready[2];
static uint8_t dirty[GAME_BRICK_BYTES];
static SCB_REHV_PAL scbs[16];
static uint8_t scb_wizard[16];
static uint8_t queued;
static uint8_t tile_sprites[4][26];
static uint8_t wizard_sprites[4][26];
static const uint8_t shot_sprite[] = {2,0x9f,2,0x90,0};
static uint8_t sprites_ready;
static void (*serial_pump)(void);

static const uint8_t *cell_tile(const struct game_state *state,
                               uint8_t x, uint8_t y)
{
    if (!state->map_ready || !wall_at(state->bricks, x, y)) return floor_tile;
    if (x == 0 || x == 19 || y == 0 || y == 18) return border_tile;
    return (y & 1) ? wall_odd : wall_even;
}

static void make_sprites(void)
{
    uint8_t i, y, x, pen, source_x;
    const uint8_t *tiles[4];
    uint8_t *out;
    tiles[0] = floor_tile; tiles[1] = wall_even;
    tiles[2] = wall_odd; tiles[3] = border_tile;
    for (i = 0; i < 4; ++i) {
        out = tile_sprites[i];
        for (y = 0; y < 5; ++y) {
            *out++ = 5;
            memcpy(out, tiles[i] + y * 4, 4);
            out += 4;
        }
        *out = 0;
        memset(wizard_sprites[i], 0, 26);
        for (y = 0; y < 5; ++y) {
            out = wizard_sprites[i] + y * 5;
            *out++ = 5;
            for (x = 0; x < 8; ++x) {
                source_x = (i & 1) ? 7 - x : x;
                switch (wizard[y][source_x]) {
                case 'C': pen = 1; break;
                case 'P': pen = 2; break;
                case 'W': pen = 3; break;
                case 'S': pen = 4; break;
                default: pen = 0;
                }
                if ((i & 2) && x == 0 && y == 2) pen = 3;
                if (x & 1) out[x >> 1] |= pen;
                else out[x >> 1] = (uint8_t)(pen << 4);
            }
        }
    }
    for (i = 0; i < 16; ++i) {
        scbs[i].sprctl1 = LITERAL | REHV;
        scbs[i].sprcoll = NO_COLLIDE;
        scbs[i].hsize = scbs[i].vsize = 0x100;
        for (x = 0; x < 8; ++x)
            scbs[i].penpal[x] = (uint8_t)(x * 0x22 + 1);
    }
    sprites_ready = 1;
}

static void flush(void)
{
    unsigned long start, elapsed;
    if (!queued) return;
    if (serial_pump) serial_pump();
    scbs[queued - 1].next = 0;
    start = bench_now();
    tgi_sprite(scbs);
    elapsed = (bench_now() - start) & 0xffffffUL;
    if (bench_measuring && elapsed > bench_max_batch) bench_max_batch = elapsed;
    queued = 0;
    if (serial_pump) serial_pump();
}

static void queue(uint8_t *data, uint8_t x, uint8_t y,
                  uint8_t transparent, uint8_t wizard_color)
{
    SCB_REHV_PAL *scb = &scbs[queued];
    scb->data = data;
    scb->hpos = x; scb->vpos = y;
    scb->sprctl0 = BPP_4 | (transparent ? TYPE_NONCOLL : TYPE_BACKNONCOLL);
    if (wizard_color) {
        scb->penpal[0] = wizard_color; /* transparent 0; pen 1 is robe */
        scb->penpal[1] = 0x8f;        /* face, eye/local marker */
        scb->penpal[2] = 0x65;        /* shoes */
    } else if (scb_wizard[queued]) {
        scb->penpal[0] = 0x01;
        scb->penpal[1] = 0x23;
        scb->penpal[2] = 0x45;
    }
    scb_wizard[queued] = wizard_color;
    scb->next = (char *)&scbs[queued + 1];
    ++queued;
    if (queued == bench_batch_limit) flush();
}

static void mark_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t cx, cy, right, bottom;
    uint16_t cell;
    if (x >= 160 || y >= 95) return;
    right = (uint8_t)((x + w - 1) / 8);
    bottom = (uint8_t)((y + h - 1) / 5);
    if (right > 19) right = 19;
    if (bottom > 18) bottom = 18;
    for (cy = y / 5; cy <= bottom; ++cy)
        for (cx = x / 8; cx <= right; ++cx) {
            cell = (uint16_t)cy * 20 + cx;
            dirty[cell >> 3] |= (uint8_t)(1u << (cell & 7));
        }
}

static void queue_tile(const struct game_state *state, uint8_t x, uint8_t y)
{
    const uint8_t *tile = cell_tile(state, x, y);
    uint8_t id = tile == floor_tile ? 0 : tile == wall_even ? 1 :
                 tile == wall_odd ? 2 : 3;
    queue(tile_sprites[id], x * 8, y * 5, 0, 0);
}

static uint8_t suzy_draw(uint8_t page_index, const struct game_state *state,
                         void (*pump)(void))
{
    uint8_t i, bit, mask, x, y, facing, image;
    uint16_t cell;
    const struct game_player *player;
    struct overlay_position *position;
    const struct game_shot *shot;
    serial_pump = pump;
    if (!sprites_ready) make_sprites();
    if (!cached_valid[page_index] || cached_round[page_index] != state->round_id ||
        cached_ready[page_index] != state->map_ready) {
        memset(dirty, 0xff, sizeof(dirty));
        memset(old_players[page_index], 0, sizeof(old_players[page_index]));
        memset(old_shots[page_index], 0, sizeof(old_shots[page_index]));
    } else {
        if (memcmp(cached_bricks[page_index], state->bricks, GAME_BRICK_BYTES)) {
            for (i = 0; i < GAME_BRICK_BYTES; ++i)
                dirty[i] = cached_bricks[page_index][i] ^ state->bricks[i];
        } else memset(dirty, 0, sizeof(dirty));
        for (i = 0; i < GAME_PLAYERS; ++i) {
            if (old_players[page_index][i].valid)
                mark_rect(old_players[page_index][i].x,
                          old_players[page_index][i].y, 9, 5);
            if (old_shots[page_index][i].valid)
                mark_rect(old_shots[page_index][i].x,
                          old_shots[page_index][i].y, 2, 2);
        }
    }
    memcpy(cached_bricks[page_index], state->bricks, GAME_BRICK_BYTES);
    cached_valid[page_index] = 1;
    cached_round[page_index] = state->round_id;
    cached_ready[page_index] = state->map_ready;
    for (i = 0; i < GAME_BRICK_BYTES; ++i) {
        mask = dirty[i];
        if (!mask) continue;
        for (bit = 0; bit < 8; ++bit) {
            if (!(mask & (1u << bit))) continue;
            cell = (uint16_t)i * 8 + bit;
            if (cell >= 380) break;
            x = cell % 20; y = cell / 20;
            queue_tile(state, x, y);
        }
    }
    if (state->snapshot_ready) {
        for (i = 0; i < GAME_PLAYERS; ++i) {
            shot = &state->shots[i];
            old_shots[page_index][i].valid = 0;
            if (!shot->active || shot->x >= 20 || shot->y >= 18) continue;
            x = shot->x * 8 + 3; y = shot->y * 5 + 2;
            queue((uint8_t *)shot_sprite, x, y, 1, 0);
            old_shots[page_index][i].x = x;
            old_shots[page_index][i].y = y;
            old_shots[page_index][i].valid = 1;
        }
        for (i = 0; i < GAME_PLAYERS; ++i) {
            player = &state->players[i];
            old_players[page_index][i].valid = 0;
            if (!((state->seat_mask | state->zombie_mask) & (1u << i)) ||
                player->respawning || player->x >= 20 || player->y >= 18) {
                visual[i].valid = 0;
                continue;
            }
            move_visual(i, state);
            position = &visual[i];
            facing = player->joy & 15;
            image = (facing == 11 ? 1 : 0) | (i == state->local_pid ? 2 : 0);
            queue(wizard_sprites[image], position->x, position->y,
                  1, player_colors[i]);
            old_players[page_index][i] = *position;
        }
    }
    flush();
    return 0; /* Native Suzy painted every row; no TGI bottom-strip fallback. */
}

/* Control experiment: retain the existing CPU renderer and bitmap cache,
   but patch changed tiles instead of invalidating the entire maze. */
static void cpu_patch(uint8_t page_index, const struct game_state *state)
{
    uint8_t i, bit, row, x, y, diff;
    uint16_t cell;
    uint8_t *dst;
    const uint8_t *tile;
    if (!background_valid || background_round != state->round_id ||
        background_map_ready != state->map_ready) return;
    if (memcmp(background_bricks, state->bricks, GAME_BRICK_BYTES)) {
    for (i = 0; i < GAME_BRICK_BYTES; ++i) {
        diff = background_bricks[i] ^ state->bricks[i];
        if (!diff) continue;
        dirty_cpu[0][i] |= diff;
        dirty_cpu[1][i] |= diff;
        cpu_pending[0] = cpu_pending[1] = 1;
        background_bricks[i] = state->bricks[i];
        for (bit = 0; bit < 8; ++bit) {
            if (!(diff & (1u << bit))) continue;
            cell = (uint16_t)i * 8 + bit;
            if (cell >= 380) break;
            x = cell % 20; y = cell / 20;
            tile = cell_tile(state, x, y);
            dst = background + (uint16_t)y * 400 + x * 4;
            for (row = 0; row < 5; ++row) {
                memcpy(dst, tile, 4);
                dst += 80; tile += 4;
            }
        }
    }
    }
    if (!frame_valid[page_index]) {
        memset(dirty_cpu[page_index], 0, GAME_BRICK_BYTES);
        cpu_pending[page_index] = 0;
        return;
    }
    if (!cpu_pending[page_index]) return;
    for (i = 0; i < GAME_BRICK_BYTES; ++i) {
        diff = dirty_cpu[page_index][i];
        if (!diff) continue;
        for (bit = 0; bit < 8; ++bit) {
            if (!(diff & (1u << bit))) continue;
            cell = (uint16_t)i * 8 + bit;
            if (cell >= 380) break;
#ifdef LYNX_BENCH_HOST
            restore_rect(host_cpu_page, page_index,
#else
            restore_rect((uint8_t *)(uintptr_t)(page_index ? 0xc038 : 0xe018), page_index,
#endif
                         (cell % 20) * 8, (cell / 20) * 5, 8, 5);
        }
        dirty_cpu[page_index][i] = 0;
    }
    cpu_pending[page_index] = 0;
}

void bench_render_reset(void)
{
    memset(cached_valid, 0, sizeof(cached_valid));
    memset(dirty_cpu, 0, sizeof(dirty_cpu));
    memset(cpu_pending, 0, sizeof(cpu_pending));
    queued = 0;
    cpu_playfield_invalidate();
}

void playfield_invalidate(void) { bench_render_reset(); }
void playfield_invalidate_page(uint8_t page_index)
{
    cpu_playfield_invalidate_page(page_index);
    if (page_index < 2) cached_valid[page_index] = 0;
}

const uint8_t *playfield_bottom_row(uint8_t y)
{
    return bench_mode >= 2 ? 0 : cpu_playfield_bottom_row(y);
}

uint8_t playfield_draw(uint8_t *page, uint8_t page_index,
                       const struct game_state *state, void (*pump)(void))
{
    if (bench_mode >= 2) return suzy_draw(page_index, state, pump);
    if (bench_mode == 1) {
#ifdef LYNX_BENCH_HOST
        host_cpu_page = page;
#endif
        cpu_patch(page_index, state);
    }
    return cpu_playfield_draw(page, page_index, state, pump);
}
