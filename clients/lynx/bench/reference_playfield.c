/* Frozen CPU renderer from game commit 312356d. */
#include "reference_playfield.h"
#include "colors.h"
#include <string.h>

#define BACKGROUND_BYTES (PLAYFIELD_STRIDE * PLAYFIELD_H)

struct overlay_position {
    uint8_t x;
    uint8_t y;
    uint8_t valid;
};

static uint8_t background[BACKGROUND_BYTES];
static uint8_t bottom_row[PLAYFIELD_STRIDE];
static uint8_t active_page0;
static uint8_t background_bricks[GAME_BRICK_BYTES];
static uint8_t background_valid;
static uint8_t background_round;
static uint8_t background_map_ready;
static struct overlay_position visual[GAME_PLAYERS];
static struct overlay_position authoritative[GAME_PLAYERS];
static struct overlay_position old_players[2][GAME_PLAYERS];
static struct overlay_position old_shots[2][GAME_PLAYERS];
static uint8_t frame_valid[2];

/* The full 20x19 maze uses 8x5 cells. The remaining seven rows are HUD. */
static const uint8_t floor_tile[20] = {
    0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x16, 0x11,
    0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11
};
static const uint8_t wall_even[20] = {
    0x99, 0x99, 0x99, 0x99,
    0x77, 0x17, 0x77, 0x17,
    0x78, 0x81, 0x78, 0x87,
    0x77, 0x17, 0x77, 0x17,
    0x66, 0x66, 0x66, 0x66
};
static const uint8_t wall_odd[20] = {
    0x99, 0x99, 0x99, 0x99,
    0x77, 0x71, 0x77, 0x71,
    0x77, 0x87, 0x78, 0x81,
    0x77, 0x71, 0x77, 0x71,
    0x66, 0x66, 0x66, 0x66
};
static const uint8_t border_tile[20] = {
    0xDD, 0xDE, 0xDD, 0xDE,
    0xDE, 0xDD, 0xDE, 0xDD,
    0xDD, 0xED, 0xDD, 0xED,
    0xDE, 0xDD, 0xDE, 0xDD,
    0xDD, 0xDD, 0xDD, 0xDD
};

/* The five-pixel silhouette fits a single open maze row. */
static const char *const wizard[5] = {
    "..CC....",
    ".CCCCCC.",
    "..PPWC..",
    "..CCCCC.",
    ".CSSSSC."
};

static uint8_t wall_at(const uint8_t *bricks, uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * 20 + x;
    return (bricks[index >> 3] >> (index & 7)) & 1;
}

static void pixel(uint8_t *page, uint8_t x, uint8_t y, uint8_t color)
{
    uint16_t index = (uint16_t)y * PLAYFIELD_STRIDE + (x >> 1);
    if (x & 1)
        page[index] = (uint8_t)((page[index] & 0xf0) | color);
    else
        page[index] = (uint8_t)((page[index] & 0x0f) | (color << 4));
}

static void actor_pixel(uint8_t *page, uint8_t x, uint8_t y, uint8_t color)
{
    /* During pixel interpolation, the maze still owns every wall pixel. */
    if (x >= PLAYFIELD_W || y >= PLAYFIELD_H ||
        (background_map_ready &&
         wall_at(background_bricks, x / 8, y / 5))) return;
    if (active_page0 && y == PLAYFIELD_PAGE0_CPU_ROWS) {
        pixel(bottom_row, x, 0, color);
        return;
    }
    if (active_page0 && y > PLAYFIELD_PAGE0_CPU_ROWS) return;
    pixel(page, x, y, color);
}

static void wizard_draw(uint8_t *page, uint8_t px, uint8_t py,
                        uint8_t color, uint8_t facing)
{
    uint8_t row;
    uint8_t col;
    uint8_t tile_x;
    uint8_t screen_y;
    uint8_t pen;
    for (row = 0; row < 5; ++row) {
        screen_y = (uint8_t)(py + row);
        if (screen_y >= PLAYFIELD_H) continue;
        for (col = 0; col < 8; ++col) {
            tile_x = facing == 11 ? (uint8_t)(7 - col) : col;
            switch (wizard[row][tile_x]) {
            case 'C': pen = color; break;
            case 'P': pen = 8; break;
            case 'W': pen = 15; break;
            case 'S': pen = 6; break;
            default: continue;
            }
            actor_pixel(page, (uint8_t)(px + col), screen_y, pen);
        }
    }
    /* White shoulder mark distinguishes the locally controlled wizard. */
}

static void shot_draw(uint8_t *page, uint8_t x, uint8_t y)
{
    uint8_t px = (uint8_t)(x * 8 + 3);
    uint8_t py = (uint8_t)(y * 5 + 2);
    pixel(page, px, py, 9);
    pixel(page, (uint8_t)(px + 1), py, 15);
    if (py + 1 < PLAYFIELD_H) pixel(page, px, (uint8_t)(py + 1), 9);
}

static void fire_cue_draw(uint8_t *page, uint8_t px, uint8_t py,
                          uint8_t facing)
{
    uint8_t x = (uint8_t)(px + 4);
    uint8_t y = (uint8_t)(py + 2);
    if (facing == 7) x = (uint8_t)(px + 7);
    else if (facing == 11) x = px;
    else if (facing == 14) y = py;
    else if (facing == 13) y = (uint8_t)(py + 4);
    actor_pixel(page, x, y, 9);
    if (x < 159) actor_pixel(page, (uint8_t)(x + 1), y, 15);
}

static void build_background(const struct game_state *state)
{
    uint8_t x;
    uint8_t y;
    uint8_t row;
    uint8_t *dst;
    const uint8_t *tile;
    memcpy(background_bricks, state->bricks, GAME_BRICK_BYTES);
    background_round = state->round_id;
    background_map_ready = state->map_ready;
    for (y = 0; y < 19; ++y) {
        for (x = 0; x < 20; ++x) {
            if (background_map_ready && wall_at(background_bricks, x, y)) {
                if (x == 0 || x == 19 || y == 0 || y == 18)
                    tile = border_tile;
                else
                    tile = (y & 1) ? wall_odd : wall_even;
            } else {
                tile = floor_tile;
            }
            dst = background + (uint16_t)y * (5 * PLAYFIELD_STRIDE) + x * 4;
            for (row = 0; row < 5; ++row) {
                dst[0] = tile[0]; dst[1] = tile[1];
                dst[2] = tile[2]; dst[3] = tile[3];
                dst += PLAYFIELD_STRIDE;
                tile += 4;
            }
        }
    }
    background_valid = 1;
}

static void restore_rect(uint8_t *page, uint8_t page_index, uint8_t x, uint8_t y,
                         uint8_t width, uint8_t height)
{
    uint8_t row;
    uint8_t right;
    uint8_t bottom;
    uint8_t count;
    uint16_t offset;
    if (x >= PLAYFIELD_W || y >= PLAYFIELD_H) return;
    right = (uint8_t)(x + width - 1);
    if (right >= PLAYFIELD_W) right = PLAYFIELD_W - 1;
    bottom = (uint8_t)(y + height);
    if (bottom > PLAYFIELD_H) bottom = PLAYFIELD_H;
    if (page_index == 0 && bottom > PLAYFIELD_PAGE0_CPU_ROWS)
        bottom = PLAYFIELD_PAGE0_CPU_ROWS;
    count = (uint8_t)((right >> 1) - (x >> 1) + 1);
    for (row = y; row < bottom; ++row) {
        offset = (uint16_t)row * PLAYFIELD_STRIDE + (x >> 1);
        memcpy(page + offset, background + offset, count);
    }
}

static uint8_t visual_cell_clear(const struct game_state *state, uint8_t slot,
                                 uint8_t x, uint8_t y)
{
    uint8_t i;
    uint16_t cell;
    if (x >= 20 || y >= 19) return 0;
    cell = (uint16_t)y * 20 + x;
    if (state->bricks[cell >> 3] & (1u << (cell & 7))) return 0;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        if (i != slot && ((state->seat_mask | state->zombie_mask) & (1u << i)) &&
            !state->players[i].respawning &&
            state->players[i].x == x && state->players[i].y == y) return 0;
    }
    return 1;
}

static uint8_t visual_footprint_clear(const struct game_state *state,
                                      uint8_t slot, uint8_t px, uint8_t py)
{
    uint8_t x0 = px / 8;
    uint8_t x1 = (uint8_t)(px + 7) / 8;
    uint8_t y0 = py / 5;
    uint8_t y1 = (uint8_t)(py + 4) / 5;
    uint8_t x;
    uint8_t y;
    if (px > PLAYFIELD_W - 8 || py > PLAYFIELD_H - 5)
        return 0;
    for (y = y0; y <= y1; ++y)
        for (x = x0; x <= x1; ++x)
            if (!visual_cell_clear(state, slot, x, y)) return 0;
    return 1;
}

static uint16_t visual_distance(uint8_t x0, uint8_t y0,
                                uint8_t x1, uint8_t y1)
{
    return (uint16_t)(x0 > x1 ? x0 - x1 : x1 - x0) +
           (y0 > y1 ? y0 - y1 : y1 - y0);
}

static void move_visual(uint8_t i, const struct game_state *state)
{
    const struct game_player *player = &state->players[i];
    uint8_t target_x = (uint8_t)(player->x * 8);
    uint8_t target_y = (uint8_t)(player->y * 5);
    struct overlay_position *position = &visual[i];
    uint8_t stick = state->input_joy & 15;
    uint8_t next_x;
    uint8_t next_y;
    uint8_t aligned;
    uint8_t previous_target_x = authoritative[i].x;
    uint8_t previous_target_y = authoritative[i].y;
    uint8_t server_moved_away = 0;
    uint8_t server_move_matches_stick;
    if (authoritative[i].valid &&
        (authoritative[i].x != target_x || authoritative[i].y != target_y) &&
        visual[i].valid &&
        visual_distance(position->x, position->y, target_x, target_y) >
        visual_distance(position->x, position->y,
                        authoritative[i].x, authoritative[i].y))
        server_moved_away = 1;
    authoritative[i].x = target_x;
    authoritative[i].y = target_y;
    authoritative[i].valid = 1;
    if (!position->valid || (i != state->local_pid && (
        (position->x > target_x && position->x - target_x > 48) ||
        (target_x > position->x && target_x - position->x > 48) ||
        (position->y > target_y && position->y - target_y > 30) ||
        (target_y > position->y && target_y - position->y > 30)))) {
        position->x = target_x;
        position->y = target_y;
        position->valid = 1;
        return;
    }
    /* Shots originate at the server cell. Rejoin before drawing the local
       firing frame so the muzzle and authoritative shot agree. */
    if (i == state->local_pid && (state->input_joy & 0x10) &&
        visual_footprint_clear(state, i, target_x, target_y)) {
        position->x = target_x;
        position->y = target_y;
        return;
    }
    /* The server is authoritative. A new position farther from the visual
       one means prediction took the wrong route (or missed a respawn).
       Rejoin at the server cell instead of leaving shots and sprite apart. */
    server_move_matches_stick =
        (stick == 7 && target_y == previous_target_y &&
         target_x > previous_target_x) ||
        (stick == 11 && target_y == previous_target_y &&
         target_x < previous_target_x) ||
        (stick == 13 && target_x == previous_target_x &&
         target_y > previous_target_y) ||
        (stick == 14 && target_x == previous_target_x &&
         target_y < previous_target_y);
    if (i == state->local_pid && server_moved_away &&
        ((position->x > target_x ? position->x - target_x :
          target_x - position->x) > 8 ||
         (position->y > target_y ? position->y - target_y :
          target_y - position->y) > 5 ||
         (stick != 15 && !server_move_matches_stick)) &&
        visual_footprint_clear(state, i, target_x, target_y)) {
        position->x = target_x;
        position->y = target_y;
        return;
    }
    /* The server moves at 10 Hz and a half-duplex snapshot can arrive late.
       Keep the local sprite moving between snapshots, at most two cells ahead
       of the acknowledged position. Collision checks prevent drawing through
       walls while the server catches up. */
    if (i == state->local_pid && !state->phase && !(state->input_joy & 0x10) &&
        stick != 15) {
        next_x = position->x;
        next_y = position->y;
        /* Finish the previous cell transition before turning a corner. */
        if ((stick == 14 || stick == 13) && (next_x & 7)) {
            if (target_x > next_x)
                aligned = (uint8_t)(((next_x + 7) / 8) * 8);
            else if (target_x < next_x)
                aligned = (uint8_t)((next_x / 8) * 8);
            else
                aligned = (uint8_t)(((next_x + 4) / 8) * 8);
            if (next_x < aligned) next_x = (uint8_t)(next_x +
                (aligned - next_x > 2 ? 2 : aligned - next_x));
            else next_x = (uint8_t)(next_x -
                (next_x - aligned > 2 ? 2 : next_x - aligned));
        } else if ((stick == 7 || stick == 11) && next_y % 5) {
            if (target_y > next_y)
                aligned = (uint8_t)(((next_y + 4) / 5) * 5);
            else if (target_y < next_y)
                aligned = (uint8_t)((next_y / 5) * 5);
            else
                aligned = (uint8_t)(((next_y + 2) / 5) * 5);
            if (next_y < aligned) ++next_y;
            else --next_y;
        } else if (stick == 7 && next_x < target_x + 16 && next_x < 152)
            next_x = (uint8_t)(next_x + 2);
        else if (stick == 11 && next_x + 16 > target_x && next_x >= 2)
            next_x = (uint8_t)(next_x - 2);
        else if (stick == 13 && next_y < target_y + 10 && next_y < 90)
            ++next_y;
        else if (stick == 14 && next_y + 10 > target_y && next_y > 0)
            --next_y;
        if (visual_footprint_clear(state, i, next_x, next_y)) {
            position->x = next_x;
            position->y = next_y;
        } else if ((position->x / 8 != player->x ||
                    position->y / 5 != player->y) &&
                   visual_footprint_clear(state, i, target_x, target_y)) {
            position->x = target_x;
            position->y = target_y;
        }
        return;
    }
    next_x = position->x;
    next_y = position->y;
    if (next_x < target_x)
        next_x = (uint8_t)(next_x +
                 (target_x - next_x > 3 ? 3 : target_x - next_x));
    else if (next_x > target_x)
        next_x = (uint8_t)(next_x -
                 (next_x - target_x > 3 ? 3 : next_x - target_x));
    if (visual_footprint_clear(state, i, next_x, position->y))
        position->x = next_x;
    else if (visual_footprint_clear(state, i, target_x, target_y)) {
        position->x = target_x;
        position->y = target_y;
        return;
    }
    if (next_y < target_y)
        next_y = (uint8_t)(next_y +
                 (target_y - next_y > 2 ? 2 : target_y - next_y));
    else if (next_y > target_y)
        next_y = (uint8_t)(next_y -
                 (next_y - target_y > 2 ? 2 : next_y - target_y));
    if (visual_footprint_clear(state, i, position->x, next_y))
        position->y = next_y;
    else if (visual_footprint_clear(state, i, target_x, target_y)) {
        position->x = target_x;
        position->y = target_y;
    }
}

void playfield_invalidate(void)
{
    background_valid = 0;
    frame_valid[0] = frame_valid[1] = 0;
    memset(visual, 0, sizeof(visual));
    memset(authoritative, 0, sizeof(authoritative));
    memset(old_players, 0, sizeof(old_players));
    memset(old_shots, 0, sizeof(old_shots));
}

void playfield_invalidate_page(uint8_t page_index)
{
    if (page_index < 2) frame_valid[page_index] = 0;
}

const uint8_t *playfield_bottom_row(uint8_t y)
{
    if (!background_valid || y >= PLAYFIELD_H ||
        y < PLAYFIELD_PAGE0_CPU_ROWS) return 0;
    if (y == PLAYFIELD_PAGE0_CPU_ROWS) return bottom_row;
    return background + (uint16_t)y * PLAYFIELD_STRIDE;
}

uint8_t playfield_draw(uint8_t *page, uint8_t page_index,
                       const struct game_state *state, void (*pump)(void))
{
    uint8_t i;
    uint8_t rebuild;
    uint8_t copied = 0;
    const struct game_player *player;
    struct overlay_position *position;
    if (!page || !state || page_index > 1) return 0;
    active_page0 = page_index == 0;
    rebuild = !background_valid || background_round != state->round_id ||
        background_map_ready != state->map_ready ||
        memcmp(background_bricks, state->bricks, GAME_BRICK_BYTES) != 0;
    if (rebuild) {
        if (background_valid && background_round != state->round_id)
            memset(visual, 0, sizeof(visual));
        build_background(state);
        frame_valid[0] = frame_valid[1] = 0;
    }
    if (active_page0)
        memcpy(bottom_row,
               background + PLAYFIELD_PAGE0_CPU_ROWS * PLAYFIELD_STRIDE,
               PLAYFIELD_STRIDE);
    if (!frame_valid[page_index]) {
        memcpy(page, background, page_index == 0 ?
               PLAYFIELD_PAGE0_CPU_ROWS * PLAYFIELD_STRIDE : BACKGROUND_BYTES);
        copied = 1;
        memset(old_players[page_index], 0, sizeof(old_players[page_index]));
        memset(old_shots[page_index], 0, sizeof(old_shots[page_index]));
        frame_valid[page_index] = 1;
    } else {
        for (i = 0; i < GAME_PLAYERS; ++i) {
            if (old_players[page_index][i].valid)
                restore_rect(page, page_index, old_players[page_index][i].x,
                             old_players[page_index][i].y, 9, 5);
            if (old_shots[page_index][i].valid)
                restore_rect(page, page_index, old_shots[page_index][i].x,
                             old_shots[page_index][i].y, 2, 2);
        }
    }
    if (state->snapshot_ready) {
        for (i = 0; i < GAME_PLAYERS; ++i) {
            const struct game_shot *shot = &state->shots[i];
            old_shots[page_index][i].valid = 0;
            if (shot->active && shot->x < 20 && shot->y < 18) {
                shot_draw(page, shot->x, shot->y);
                old_shots[page_index][i].x = (uint8_t)(shot->x * 8 + 3);
                old_shots[page_index][i].y = (uint8_t)(shot->y * 5 + 2);
                old_shots[page_index][i].valid = 1;
            }
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
            wizard_draw(page, position->x, position->y,
                        player_colors[i], player->joy & 15);
            if (i == state->local_pid)
                actor_pixel(page, position->x, (uint8_t)(position->y + 2), 15);
            if (i == state->local_pid && state->fire_cue_frames)
                fire_cue_draw(page, position->x, position->y,
                              state->input_joy & 15);
            old_players[page_index][i] = *position;
        }
    } else {
        for (i = 0; i < GAME_PLAYERS; ++i) {
            visual[i].valid = 0;
            old_players[page_index][i].valid = 0;
            old_shots[page_index][i].valid = 0;
        }
    }
    if (pump) pump();
    return copied;
}
