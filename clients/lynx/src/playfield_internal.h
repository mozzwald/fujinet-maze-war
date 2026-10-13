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

/* Each actor has one bounded interpolation segment. `next` stores the
   destination pixels, not an index into a playback queue. Old route points
   establish a legal last leg; they never add animation time after release. */
struct visual_motion {
    uint16_t next, tick;
    uint8_t epoch, progress, from_x, from_y;
};
static struct visual_motion motion[GAME_PLAYERS];

static uint8_t single_leg(uint8_t x, uint8_t y, uint8_t tx, uint8_t ty)
{
    return (y == ty && (x > tx ? x-tx : tx-x) <= 8) ||
           (x == tx && (y > ty ? y-ty : ty-y) <= 5);
}

static void move_visual(uint8_t i, const struct game_state *state)
{
    const struct game_player *player = &state->players[i];
    const struct game_motion *path = &state->motion[i];
    struct overlay_position *position = &visual[i];
    struct visual_motion *m = &motion[i];
    uint8_t tx = player->x * 8, ty = player->y * 5;
    uint8_t x, y, index, ticks;
    uint16_t target = tx | ((uint16_t)ty << 8);
    uint16_t elapsed;
    if (!position->valid ||
#if defined(LYNX_LOCAL_CELL_TEST) && LYNX_LOCAL_CELL_TEST
        i == state->local_pid ||
#endif
        (i == state->local_pid && (state->input_joy & 0x10) &&
         (state->input_joy & 15) != 15)) goto place;
    if (m->next != target) {
        /* A correction can ease one clear cell, but a teleport or a corner
           must never create a diagonal route through the maze. */
        if (!path->serial && m->epoch == path->epoch) goto place;
        x = position->x; y = position->y;
        /* A remote update commonly arrives while its previous cell is still
           being drawn. Retarget smoothly across up to two clear cells;
           restricting this to one cell forced a jump on ordinary jitter.
           Local forward prediction keeps its one-cell stopping bound. */
        if (!single_leg(x,y,tx,ty) &&
            !((i != state->local_pid || m->epoch != path->epoch) &&
              ((y == ty && (x > tx ? x-tx : tx-x) <= 16) ||
               (x == tx && (y > ty ? y-ty : ty-y) <= 10)) &&
              visual_footprint_clear(state,i,(x+tx)/2,(y+ty)/2))) {
            if (path->serial > 1) {
                index = (path->serial - 2) % GAME_MOTION_POINTS;
                x = path->x[index] * 8; y = path->y[index] * 5;
            } else if (m->epoch == path->epoch) {
                x = (uint8_t)m->next; y = (uint8_t)(m->next >> 8);
            } else goto place;
            if (!single_leg(x,y,tx,ty) ||
                !visual_footprint_clear(state,i,x,y)) goto place;
            /* Catch up to the last recorded leg rather than replaying old
               cells. Its existing timestamp also bounds its finish time. */
            position->x = x; position->y = y;
        }
        m->from_x = x; m->from_y = y;
        m->tick = state->input_clock;
        ticks = GAME_PRESS_TICKS;
        if (path->serial && m->epoch == path->epoch) {
            index = (path->serial - 1) % GAME_MOTION_POINTS;
            if (path->x[index] == player->x && path->y[index] == player->y) {
                m->tick = path->at[index];
                ticks = path->ticks[index];
            }
        }
        m->progress = ticks ? ticks : GAME_MOVE_TICKS;
        m->next = target; m->epoch = path->epoch;
    }
    if (!m->progress) return;
    elapsed = (uint16_t)(state->input_clock - m->tick);
    if (elapsed >= 0x8000u) return;
    if (elapsed >= m->progress) goto place;
    ticks = (uint8_t)elapsed;
    x = tx >= m->from_x ? m->from_x + (tx-m->from_x)*ticks/m->progress :
                          m->from_x - (m->from_x-tx)*ticks/m->progress;
    y = ty >= m->from_y ? m->from_y + (ty-m->from_y)*ticks/m->progress :
                          m->from_y - (m->from_y-ty)*ticks/m->progress;
    if (!visual_footprint_clear(state,i,x,y)) goto place;
    position->x = x; position->y = y;
    return;
place:
    position->x = tx; position->y = ty; position->valid = 1;
    m->next = target; m->epoch = path->epoch; m->progress = 0;
}
