#include "playfield.h"
#include "colors.h"
#include "phase_scratch.h"
#include <string.h>
#include <stddef.h>

typedef char view_prefix_must_match[(offsetof(struct game_state,names) == sizeof(struct game_view)) ? 1 : -1];
#ifdef LYNX_RENDER_HOST
#include "suzy_host.h"
#else
#include <lynx.h>
#include <tgi.h>
#endif
#include "playfield_internal.h"
#include "death_sprites.h"

/* Maze cache lives on the two display pages. Only changed cells and old
   overlay footprints are restored. CPU writes never touch video RAM. */
static uint8_t cached_bricks[2][GAME_BRICK_BYTES];
static uint8_t cached_valid[2];
static uint8_t cached_round[2];
static uint8_t cached_ready[2];
#define dirty phase_scratch.play.dirty
#ifdef LYNX_RENDER_HOST
static SCB_REHV_PAL scbs[8];
#else
extern SCB_REHV_PAL maze_scbs[8];
#define scbs maze_scbs
typedef char scb_size_must_match_assembly[(sizeof(SCB_REHV_PAL) == 23) ? 1 : -1];
#endif
static uint8_t scb_wizard[8];
static uint8_t queued;
/* Opaque tiles use counted packets, so padding never paints outside a cell.
   Transparent literals have a zero guard byte on every row for Suzy's
   end-of-byte last-pixel bug (Handy does not emulate this). */
#define WIZARD_ROW 6
#define WIZARD_BYTES (5 * WIZARD_ROW + 1)
#define DEATH_ROW 8
#define DEATH_BYTES (9 * DEATH_ROW + 1)
#include "maze_sprites.h"
static uint8_t sprites_ready;
#define frame_state phase_scratch.play.frame
static uint8_t clipped_wizards[GAME_PLAYERS][WIZARD_BYTES];
static uint8_t fire_sprites[GAME_PLAYERS][4];
static uint8_t clipped_deaths[GAME_PLAYERS][DEATH_BYTES];
static struct overlay_position old_deaths[2][GAME_PLAYERS];
static uint8_t have_round;
static uint8_t visual_round;
static void (*serial_pump)(void);

static uint8_t cell_tile(const struct game_view *state, uint8_t x, uint8_t y)
{
    if (!state->map_ready || !wall_at(state->bricks, x, y)) return 0;
    if (x == 0 || x == 19 || y == 0 || y == 18) return 3;
    return (y & 1) ? 2 : 1;
}

static void make_sprites(void)
{
    uint8_t i, x;
    for (i = 0; i < 8; ++i) {
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
    if (!queued) return;
    if (serial_pump) serial_pump();
    scbs[queued - 1].next = 0;
    tgi_sprite(scbs);
    queued = 0;
    if (serial_pump) serial_pump();
}

static void queue(const uint8_t *data, uint8_t x, uint8_t y,
                  uint8_t transparent, uint8_t wizard_color)
{
    SCB_REHV_PAL *scb = &scbs[queued];
    scb->data = (uint8_t *)data;
    scb->hpos = x; scb->vpos = y;
    scb->sprctl0 = BPP_4 | (transparent ? TYPE_NONCOLL : TYPE_BACKNONCOLL);
    scb->sprctl1 = REHV | (transparent ? LITERAL : 0);
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
    scb->next = queued < 7 ? (char *)&scbs[queued + 1] : 0;
    ++queued;
    if (queued == 8) flush();
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

static void queue_tile(const struct game_view *state, uint8_t x, uint8_t y)
{
    uint8_t id = cell_tile(state, x, y);
    queue(tile_sprites[id], x * 8, y * 5, 0, 0);
}

static uint8_t actor_clear(const struct game_view *state, uint8_t x, uint8_t y)
{
    return x < PLAYFIELD_W && y < PLAYFIELD_H &&
        (!state->map_ready || !wall_at(state->bricks, x / 8, y / 5));
}

static const uint8_t *wizard_image(const struct game_view *state, uint8_t slot,
                              uint8_t image, uint8_t x, uint8_t y)
{
    uint8_t row, col;
    uint8_t *data = clipped_wizards[slot];
    if (x <= PLAYFIELD_W - 8 && y <= PLAYFIELD_H - 5 &&
        (!state->map_ready ||
         (!wall_at(state->bricks, x / 8, y / 5) &&
          !wall_at(state->bricks, (x + 7) / 8, y / 5) &&
          !wall_at(state->bricks, x / 8, (y + 4) / 5) &&
          !wall_at(state->bricks, (x + 7) / 8, (y + 4) / 5))))
        return wizard_sprites[image];
    memcpy(data, wizard_sprites[image], WIZARD_BYTES);
    for (row = 0; row < 5; ++row)
        for (col = 0; col < 8; ++col)
            if (!actor_clear(state, x + col, y + row)) {
                if (col & 1) data[row * WIZARD_ROW + 1 + col / 2] &= 0xf0;
                else data[row * WIZARD_ROW + 1 + col / 2] &= 0x0f;
            }
    return data;
}

static void queue_fire(const struct game_view *state, uint8_t slot,
                        uint8_t x, uint8_t y)
{
    uint8_t facing = state->input_joy & 15;
    uint8_t px = x + 4;
    uint8_t py = y + 2;
    if (facing == 7) px = x + 7;
    else if (facing == 11) px = x;
    else if (facing == 14) py = y;
    else if (facing == 13) py = y + 4;
    fire_sprites[slot][0] = 3;
    fire_sprites[slot][1] = (actor_clear(state, px, py) ? ART_MUZZLE_LEFT : 0) |
        (actor_clear(state, px + 1, py) ? ART_MUZZLE_RIGHT : 0);
    fire_sprites[slot][2] = 0;
    fire_sprites[slot][3] = 0;
    queue(fire_sprites[slot], px, py, 1, 0);
}

static void draw_deaths(uint8_t page_index, const struct game_view *state)
{
    uint8_t i, x, y, row, col, image, cy, cx, previous_cx, clear;
    uint16_t elapsed;
    uint8_t *data;
    const struct game_death *death;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        old_deaths[page_index][i].valid = 0;
        death = &state->deaths[i];
        if (!death->active ||
            !((state->seat_mask | state->zombie_mask) & (1u << i)) ||
            death->x >= 20 || death->y >= 18) continue;
        elapsed = (uint16_t)(state->input_clock - death->started);
        if (elapsed >= GAME_DEATH_TICKS) continue;
        image = elapsed / GAME_DEATH_FRAME_TICKS;
        x = death->x ? death->x * 8 - 2 : 0;
        y = death->y ? death->y * 5 - 2 : 0;
        data = clipped_deaths[i];
        memcpy(data, death_sprites[image], DEATH_BYTES);
        for (row = 0; row < 9; ++row) {
            if (y + row >= PLAYFIELD_H) {
                memset(data + row * DEATH_ROW + 1, 0, 6);
                continue;
            }
            cy = (y + row) / 5;
            previous_cx = 255;
            clear = 0;
            for (col = 0; col < 12; ++col) {
                cx = (x + col) / 8;
                if (cx != previous_cx) {
                    clear = cx < 20 && (!state->map_ready ||
                        !wall_at(state->bricks, cx, cy));
                    previous_cx = cx;
                }
                if (!clear) {
                    if (col & 1) data[row * DEATH_ROW + 1 + col / 2] &= 0xf0;
                    else data[row * DEATH_ROW + 1 + col / 2] &= 0x0f;
                }
            }
        }
        queue(data, x, y, 1, 0);
        old_deaths[page_index][i].x = x;
        old_deaths[page_index][i].y = y;
        old_deaths[page_index][i].valid = 1;
    }
}

uint8_t playfield_draw(uint8_t *page, uint8_t page_index,
                       const struct game_state *source, void (*pump)(void))
{
    uint8_t i, bit, mask, x, y, facing, image, repaint, round_stage;
    uint16_t cell;
    const struct game_view *state;
    const struct game_player *player;
    struct overlay_position *position;
    const struct game_shot *shot;
    if (!page || !source || page_index > 1) return 0;
#ifdef LYNX_RENDER_HOST
    suzy_test_target(page);
#endif
    /* Poll callbacks apply live packets. Keep every cell and actor in this
       frame on one immutable revision while those callbacks run. */
    memcpy(&frame_state, source, sizeof(frame_state));
    state = &frame_state;
    round_stage = game_round_stage(source);
    if (round_stage) {
        /* Presentation belongs to this immutable frame, never occupancy. */
        memset(frame_state.deaths, 0, sizeof(frame_state.deaths));
        memset(frame_state.shots, 0, sizeof(frame_state.shots));
        for (i = 0; i < GAME_PLAYERS; ++i) {
            if ((i != state->winner_pid && round_stage == ROUND_LOSERS) ||
                (i == state->winner_pid && round_stage == ROUND_WINNER_SMOKE)) {
                if (!state->players[i].respawning) {
                    frame_state.deaths[i].x = state->players[i].x;
                    frame_state.deaths[i].y = state->players[i].y;
                    frame_state.deaths[i].active = 1;
                    frame_state.deaths[i].started = state->result_started +
                        (i == state->winner_pid ? GAME_DEATH_TICKS + GAME_WINNER_DANCE_TICKS : 0);
                }
            }
            if (i != state->winner_pid || round_stage >= ROUND_WINNER_SMOKE)
                frame_state.players[i].respawning = 1;
        }
    }
    serial_pump = pump;
    if (!have_round || visual_round != state->round_id) {
        memset(visual, 0, sizeof(visual));
        visual_round = state->round_id;
        have_round = 1;
    }
    repaint = !cached_valid[page_index] ||
        cached_round[page_index] != state->round_id ||
        cached_ready[page_index] != state->map_ready;
    if (!sprites_ready) make_sprites();
    if (repaint) {
        memset(dirty, 0xff, sizeof(dirty));
        memset(old_players[page_index], 0, sizeof(old_players[page_index]));
        memset(old_shots[page_index], 0, sizeof(old_shots[page_index]));
        memset(old_deaths[page_index], 0, sizeof(old_deaths[page_index]));
    } else {
        if (memcmp(cached_bricks[page_index], state->bricks, GAME_BRICK_BYTES)) {
            for (i = 0; i < GAME_BRICK_BYTES; ++i)
                dirty[i] = cached_bricks[page_index][i] ^ state->bricks[i];
        } else memset(dirty, 0, sizeof(dirty));
        for (i = 0; i < GAME_PLAYERS; ++i) {
            if (old_deaths[page_index][i].valid)
                mark_rect(old_deaths[page_index][i].x,
                          old_deaths[page_index][i].y, 12, 9);
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
        /* Clouds are scenery: live players and shots draw over them. */
        draw_deaths(page_index, state);
        for (i = 0; i < GAME_PLAYERS; ++i) {
            shot = &state->shots[i];
            old_shots[page_index][i].valid = 0;
            if (!shot->active || shot->x >= 20 || shot->y >= 18) continue;
            shot_visual(state,shot,&x,&y);
            /* New wall data must never paint a delayed projectile in brick. */
            if (!actor_clear(state,x,y) || !actor_clear(state,x+1,y) ||
                !actor_clear(state,x,y+1) || !actor_clear(state,x+1,y+1)) continue;
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
            if (round_stage == ROUND_DANCE && i == state->winner_pid)
                image = 4 + ((uint16_t)(state->input_clock - state->result_started -
                            GAME_DEATH_TICKS) / 8) % 4;
            queue(wizard_image(state, i, image, position->x, position->y),
                  position->x, position->y, 1, player_colors[i]);
            if (i == state->local_pid && state->fire_cue_frames)
                queue_fire(state, i, position->x, position->y);
            old_players[page_index][i] = *position;
        }
    } else {
        memset(visual, 0, sizeof(visual));
        memset(old_players[page_index], 0, sizeof(old_players[page_index]));
        memset(old_shots[page_index], 0, sizeof(old_shots[page_index]));
        memset(old_deaths[page_index], 0, sizeof(old_deaths[page_index]));
    }
    flush();
    if (serial_pump) serial_pump();
    return repaint;
}


void playfield_invalidate(void)
{
    memset(cached_valid, 0, sizeof(cached_valid));
    memset(visual, 0, sizeof(visual));
    memset(old_players, 0, sizeof(old_players));
    memset(old_shots, 0, sizeof(old_shots));
    memset(old_deaths, 0, sizeof(old_deaths));
    have_round = 0;
    queued = 0;
}

void playfield_invalidate_page(uint8_t page_index)
{
    if (page_index < 2) cached_valid[page_index] = 0;
}
#ifdef LYNX_RENDER_HOST
void playfield_visual_position(uint8_t slot, uint8_t *x, uint8_t *y)
{
    *x = visual[slot].x; *y = visual[slot].y;
}
uint8_t playfield_shot_position(uint8_t page, uint8_t slot, uint8_t *x, uint8_t *y)
{
    *x = old_shots[page][slot].x; *y = old_shots[page][slot].y;
    return old_shots[page][slot].valid;
}
#endif
