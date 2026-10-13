/* Private maze art and visual movement state. Included by playfield.c only. */
struct overlay_position {
    uint8_t x;
    uint8_t y;
    uint8_t valid;
};

static struct overlay_position visual[GAME_PLAYERS];
static struct overlay_position old_players[2][GAME_PLAYERS];
static struct overlay_position old_shots[2][GAME_PLAYERS];

static uint8_t wall_at(const uint8_t *bricks, uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * 20 + x;
    return (bricks[index >> 3] >> (index & 7)) & 1;
}

static uint8_t visual_cell_clear(const struct game_view *state, uint8_t slot,
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

static uint8_t visual_footprint_clear(const struct game_view *state,
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

#ifndef LYNX_RENDER_HOST
/* These helpers finish before any serial callback; none can reenter. */
#pragma static-locals (push, on)
#endif
static uint8_t interpolate(uint8_t from, uint8_t to, uint8_t elapsed, uint8_t ticks)
{
    uint8_t distance = from > to ? from-to : to-from;
    distance = distance * elapsed / ticks;
    return from > to ? from-distance : from+distance;
}

/* Set up the same interpolation engine from two buffered observations. */
static void remote_segment(uint8_t slot, const struct game_view *state)
{
    const struct game_motion *path = &state->motion[slot];
    struct visual_motion *m = &motion[slot];
    uint16_t view = state->input_clock - GAME_REMOTE_DELAY;
    uint16_t span;
    uint8_t count, n, a, b, tx, ty;
    count = path->serial > GAME_MOTION_POINTS ? GAME_MOTION_POINTS : (uint8_t)path->serial;
    m->progress = 0;
    m->next = state->players[slot].x * 8 | ((uint16_t)(state->players[slot].y * 5) << 8);
    if (!count) return;
    a = (path->serial - count) % GAME_MOTION_POINTS;
    b = a;
    for (n = 0; n < count; ++n) {
        b = (path->serial - count + n) % GAME_MOTION_POINTS;
        if ((uint16_t)(view - path->at[b]) >= 0x8000u) break;
        a = b;
    }
    m->from_x = path->x[a] * 8; m->from_y = path->y[a] * 5;
    tx = path->x[b] * 8; ty = path->y[b] * 5;
    m->next = m->from_x | ((uint16_t)m->from_y << 8);
    span = (uint16_t)(path->at[b] - path->at[a]);
    if (span && span <= 18 &&
        ((m->from_y == ty && (m->from_x > tx ? m->from_x-tx : tx-m->from_x) <= 16) ||
         (m->from_x == tx && (m->from_y > ty ? m->from_y-ty : ty-m->from_y) <= 10)) &&
        visual_footprint_clear(state,slot,(m->from_x+tx)/2,(m->from_y+ty)/2)) {
        m->next = tx | ((uint16_t)ty << 8);
        m->tick = path->at[a] + GAME_REMOTE_DELAY;
        m->progress = (uint8_t)span;
    }
}

static void shot_visual(const struct game_view *state,
                        const struct game_shot *shot, uint8_t *px, uint8_t *py)
{
    uint16_t span = (uint16_t)(shot->at - shot->previous_at);
    uint16_t elapsed = (uint16_t)(state->input_clock - GAME_SHOT_DELAY - shot->previous_at);
    uint8_t x = shot->x * 8 + 3, y = shot->y * 5 + 2;
    uint8_t ax = shot->previous_x * 8 + 3, ay = shot->previous_y * 5 + 2;
    if (shot->segment && span && span <= 18 &&
        ((shot->direction == 0 && ay == y && ax < x && x-ax <= 16) ||
         (shot->direction == 1 && ax == x && ay < y && y-ay <= 10) ||
         (shot->direction == 2 && ay == y && ax > x && ax-x <= 16) ||
         (shot->direction == 3 && ax == x && ay > y && ay-y <= 10))) {
        if (elapsed >= 0x8000u) elapsed = 0;
        if (elapsed < span) {
            x = interpolate(ax,x,(uint8_t)elapsed,(uint8_t)span);
            y = interpolate(ay,y,(uint8_t)elapsed,(uint8_t)span);
        }
    }
    *px = x; *py = y;
}

#ifndef LYNX_RENDER_HOST
#pragma static-locals (pop)
#endif
static void move_visual(uint8_t i, const struct game_view *state)
{
    const struct game_player *player = &state->players[i];
    const struct game_motion *path = &state->motion[i];
    struct overlay_position *position = &visual[i];
    struct visual_motion *m = &motion[i];
    uint8_t tx = player->x * 8, ty = player->y * 5;
    uint8_t x, y, index, ticks;
    uint16_t target = tx | ((uint16_t)ty << 8);
    uint16_t elapsed;
    if (i != state->local_pid && !state->phase) {
        remote_segment(i,state);
        tx = (uint8_t)m->next; ty = (uint8_t)(m->next >> 8);
        goto animate;
    }
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
animate:
    if (!m->progress) goto place;
    elapsed = (uint16_t)(state->input_clock - m->tick);
    if (elapsed >= 0x8000u) {
        if (!visual_footprint_clear(state,i,m->from_x,m->from_y)) goto place;
        position->x = m->from_x; position->y = m->from_y;
        position->valid = 1;
        return;
    }
    if (elapsed >= m->progress) goto place;
    ticks = (uint8_t)elapsed;
    x = interpolate(m->from_x,tx,ticks,m->progress);
    y = interpolate(m->from_y,ty,ticks,m->progress);
    if (!visual_footprint_clear(state,i,x,y)) goto place;
    position->x = x; position->y = y;
    /* A remote can start inside a buffered segment before ever reaching
       place:. Both page caches must remember and erase this first draw. */
    position->valid = 1;
    return;
place:
    if (i != state->local_pid && !visual_footprint_clear(state,i,tx,ty)) {
        tx = player->x * 8; ty = player->y * 5;
    }
    position->x = tx; position->y = ty; position->valid = 1;
    m->next = target; m->epoch = path->epoch; m->progress = 0;
}
