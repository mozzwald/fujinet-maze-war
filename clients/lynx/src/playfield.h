#ifndef MAZE_LYNX_PLAYFIELD_H
#define MAZE_LYNX_PLAYFIELD_H

#include <stdint.h>
#include "protocol.h"

#define PLAYFIELD_W 160
#define PLAYFIELD_H 95
#define PLAYFIELD_STRIDE 80
#define PLAYFIELD_PAGE_BYTES 8160
#define PLAYFIELD_PAGE0_CPU_ROWS 89

/* CPU writes to page 0's rows 89..101 cross Suzy and Mikey registers.
   The bottom maze strip and HUD are drawn through TGI. */
void playfield_invalidate(void);
void playfield_invalidate_page(uint8_t page_index);
/* Returns nonzero when this page's background was copied. */
uint8_t playfield_draw(uint8_t *page, uint8_t page_index,
                       const struct game_state *state, void (*pump)(void));
/* Page 0's register-overlapping rows are staged here for TGI drawing. */
const uint8_t *playfield_bottom_row(uint8_t y);

#endif
