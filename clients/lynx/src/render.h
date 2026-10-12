#ifndef MAZE_LYNX_RENDER_H
#define MAZE_LYNX_RENDER_H

#include <stdint.h>
struct render_timing {
    uint16_t frame_ticks, draw_ticks, max_frame_ticks, max_draw_ticks;
};
extern struct render_timing render_timing;

/* Identify the current cc65 TGI back page before direct framebuffer drawing. */
void render_init(void (*pump)(void));
/* Draws one complete back page and requests the next display swap.
   The callback advances network/input work between maze and HUD rows. */
void render_draw(uint8_t diagnostics, void (*pump)(void));
/* Compact text for setup controls; uses the current TGI color. */
void render_small_text(uint8_t x, uint8_t y, const char *line);

#endif
