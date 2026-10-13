#ifndef ART_EXPECT_H
#define ART_EXPECT_H
#include "art_pixels.h"
#include "colors.h"
static uint8_t art_player_pen(uint8_t image, uint8_t row, uint8_t col, uint8_t slot)
{
    uint8_t value = art_players[image][row][col];
    switch (value) {
    case 0: return art_floor_tile[row][col];
    case 1: return player_colors[slot];
    case 2: return 8;
    case 3: return 15;
    default: return 6;
    }
}
#endif
