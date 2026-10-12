/* Private maze art and visual movement state. Included by playfield.c only. */
struct overlay_position {
    uint8_t x;
    uint8_t y;
    uint8_t valid;
};

static struct overlay_position visual[GAME_PLAYERS];
static struct overlay_position authoritative[GAME_PLAYERS];
static struct overlay_position old_players[2][GAME_PLAYERS];
static struct overlay_position old_shots[2][GAME_PLAYERS];

/* The full 20x19 maze uses 8x5 cells. The remaining seven rows are HUD. */
static const uint8_t floor_tile[20] = {
    0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11,
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

static void move_visual(uint8_t i, const struct game_state *state)
{
    const struct game_player *player = &state->players[i];
    uint8_t target_x = (uint8_t)(player->x * 8);
    uint8_t target_y = (uint8_t)(player->y * 5);
    struct overlay_position *position = &visual[i];
    uint8_t next_x;
    uint8_t next_y;
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
    /* game.players already replays the inputs actually sent. Interpolate
       only toward that cell, then stop there. Advancing beyond it based on
       a held button made intersections and tap distances misleading. */
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

