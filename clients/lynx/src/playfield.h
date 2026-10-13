#ifndef MAZE_LYNX_PLAYFIELD_H
#define MAZE_LYNX_PLAYFIELD_H

#include <stdint.h>
#include "protocol.h"

#define PLAYFIELD_W 160
#define PLAYFIELD_H 95
#define PLAYFIELD_STRIDE 80
#define PLAYFIELD_PAGE_BYTES 8160

/* Suzy paints all maze rows. The CPU must not write page 0's mapped rows. */
void playfield_invalidate(void);
void playfield_invalidate_page(uint8_t page_index);
/* Returns nonzero when this page's complete maze was repainted. */
uint8_t playfield_draw(uint8_t *page, uint8_t page_index,
                       const struct game_state *state, void (*pump)(void));
#ifdef LYNX_RENDER_HOST
void playfield_visual_position(uint8_t slot, uint8_t *x, uint8_t *y);
uint8_t playfield_shot_position(uint8_t page, uint8_t slot, uint8_t *x, uint8_t *y);
#endif

#endif
