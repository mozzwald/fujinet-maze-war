#ifndef MAZE_LYNX_COLORS_H
#define MAZE_LYNX_COLORS_H

#include "protocol.h"

/* Atari 8 PLR0..3COL = $C8, $86, $58, $28: green, blue,
   purple, orange. These are the closest bright Lynx palette entries. */
static const uint8_t player_colors[GAME_PLAYERS] = {10, 14, 12, 8};

#endif
