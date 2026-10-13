#ifndef MAZE_PHASE_SCRATCH_H
#define MAZE_PHASE_SCRATCH_H
#include "protocol.h"
/* HTTP/AppKey command mode finishes before raw NetStream/game rendering.
   Keep the resident room list/name separate; only temporary bytes overlap.
   Text packing occurs after command calls or playfield_draw return. Suzy
   finishes synchronously before the next scratch user, with no callbacks. */
union phase_scratch {
    struct { uint8_t response[256], record[189]; } setup;
    struct { struct game_view frame; uint8_t dirty[GAME_BRICK_BYTES]; } play;
    uint8_t text[6 * (40 * 2 + 2) + 1];
};
extern union phase_scratch phase_scratch;
#endif
