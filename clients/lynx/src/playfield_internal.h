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

