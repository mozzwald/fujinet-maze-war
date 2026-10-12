#ifndef MAZE_LYNX_RENDER_H
#define MAZE_LYNX_RENDER_H

#include <stdint.h>

/* Identify the current cc65 TGI back page before direct framebuffer drawing. */
void render_init(void (*pump)(void));
/* Draws one complete back page and requests the next display swap.
   The callback advances network/input work between maze and HUD rows. */
void render_draw(uint8_t diagnostics, void (*pump)(void));

#endif
