/* Legacy CPU renderer interface, isolated from the production Suzy API. */
#ifndef MAZE_CPU_REFERENCE_H
#define MAZE_CPU_REFERENCE_H
#include "../src/playfield.h"
#define PLAYFIELD_PAGE0_CPU_ROWS 89
const uint8_t *playfield_bottom_row(uint8_t y);
#endif
