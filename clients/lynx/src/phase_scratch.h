#ifndef MAZE_PHASE_SCRATCH_H
#define MAZE_PHASE_SCRATCH_H
#include "protocol.h"
/* HTTP/AppKey command mode finishes before raw NetStream/game rendering.
   Keep the resident room list/name separate; only temporary bytes overlap. */
union phase_scratch {
    struct { uint8_t response[256], record[189]; } setup;
    struct { struct game_view frame; uint8_t dirty[GAME_BRICK_BYTES]; } play;
};
extern union phase_scratch phase_scratch;
#endif
