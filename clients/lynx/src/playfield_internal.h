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

/* Consumed path index and fractional cell progress are independent of the
   two cached display pages and of how often a render is called. */
struct visual_motion {
    uint16_t next, tick;
    uint8_t epoch, progress, from_x, from_y;
};
static struct visual_motion motion[GAME_PLAYERS];

static void move_visual(uint8_t i, const struct game_state *state)
{
    const struct game_player *player = &state->players[i];
    const struct game_motion *path = &state->motion[i];
    struct overlay_position *position = &visual[i];
    struct visual_motion *m = &motion[i];
    uint8_t target_x = player->x * 8, target_y = player->y * 5;
    uint8_t index, x, y, take, progress, distance;
    uint16_t elapsed, event_at;
    if (!position->valid || m->epoch != path->epoch ||
        (uint16_t)(path->serial - m->next) > GAME_MOTION_POINTS ||
        (i == state->local_pid && (state->input_joy & 0x10))) {
        /* Spawn/correction/history overflow/fire are explicit rejoin points.
           A muzzle must originate at the same cell as its server shot. */
        position->x = target_x; position->y = target_y; position->valid = 1;
        m->next = path->serial; m->epoch = path->epoch;
        m->progress = 0; m->tick = state->input_clock;
        return;
    }
    while (m->next != path->serial) {
        index = m->next % GAME_MOTION_POINTS;
        x = path->x[index] * 8; y = path->y[index] * 5;
        if (!m->progress) {
            m->from_x = position->x; m->from_y = position->y;
            distance = (x > position->x ? x-position->x : position->x-x);
            distance += (y > position->y ? y-position->y : position->y-y);
            if (!((distance == 8 && y == position->y) ||
                  (distance == 5 && x == position->x))) break;
            event_at = path->at[index];
            if ((uint16_t)(event_at - m->tick) < 0x8000u) m->tick = event_at;
        }
        elapsed = (uint16_t)(state->input_clock - m->tick);
        if (!elapsed || elapsed >= 0x8000u) return;
        take = GAME_MOVE_TICKS - m->progress;
        if (elapsed < take) take = (uint8_t)elapsed;
        progress = m->progress + take;
        x = x >= m->from_x ? m->from_x + (x-m->from_x)*progress/GAME_MOVE_TICKS :
                            m->from_x - (m->from_x-x)*progress/GAME_MOVE_TICKS;
        y = y >= m->from_y ? m->from_y + (y-m->from_y)*progress/GAME_MOVE_TICKS :
                            m->from_y - (m->from_y-y)*progress/GAME_MOVE_TICKS;
        if (!visual_footprint_clear(state,i,x,y)) break;
        position->x = x; position->y = y;
        m->tick += take; m->progress = progress;
        if (progress != GAME_MOVE_TICKS) return;
        ++m->next; m->progress = 0;
    }
    /* No recorded route means an authoritative placement, never guessed
       X-then-Y movement around a corner. Clipping remains a final safeguard. */
    if (visual_footprint_clear(state,i,target_x,target_y)) {
        position->x = target_x; position->y = target_y;
    }
    m->next = path->serial; m->progress = 0; m->tick = state->input_clock;
}
