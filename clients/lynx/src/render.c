#include <lynx.h>
#include <tgi.h>
#include <string.h>

#include "netstream.h"
#include "protocol.h"
#include "playfield.h"
#include "render.h"
#include "colors.h"

#define BOARD_X 2
#define BOARD_Y 13
#define CELL 4
#define HUD_X 86

/* TGI alternates these pages after each completed VBL swap. Suzy draws
   the full maze on each page, including memory across hardware registers. */
static uint8_t *const frame_page[2] = {
    (uint8_t *)0xe018, (uint8_t *)0xc038
};
static uint8_t draw_page;
static uint8_t direct_page_ok;
static uint8_t hud_signature[2][40];
static uint8_t hud_valid[2];
static uint8_t previous_status[2] = {255, 255};

static char hex_digit(uint8_t value)
{
    value &= 15;
    return (char)(value < 10 ? '0' + value : 'A' + value - 10);
}

static void hex_word(char *out, uint16_t value)
{
    out[0] = hex_digit((uint8_t)(value >> 12));
    out[1] = hex_digit((uint8_t)(value >> 8));
    out[2] = hex_digit((uint8_t)(value >> 4));
    out[3] = hex_digit((uint8_t)value);
    out[4] = 0;
}

static void diagnostic_field(uint8_t x, uint8_t y, char label, uint16_t value)
{
    char line[7];
    line[0] = label;
    line[1] = ':';
    hex_word(line + 2, value);
    tgi_outtextxy(x, y, line);
}

static const char *link_message(void)
{
    switch (link_state) {
    case LINK_STARTING: return "OPENING SERIAL";
    case LINK_WAIT_WELCOME: return "WAIT WELCOME";
    case LINK_WELCOME: return "WELCOME";
    case LINK_REJECT: return "REJECT";
    case LINK_TIMEOUT: return "HELLO TIMEOUT";
    case LINK_SERIAL_ERROR: return "SERIAL ERROR";
    case LINK_COMMAND_ERROR: return "FUJINET CMD ERR";
    case LINK_TX_ERROR: return "TX ERROR";
    }
    return "UNKNOWN";
}

static void draw_diagnostics(void)
{
    tgi_setcolor(COLOR_YELLOW);
    tgi_outtextxy(4, 2, "MAZE WAR LYNX");
    tgi_setcolor(link_state == LINK_WELCOME ? COLOR_LIGHTGREEN : COLOR_WHITE);
    tgi_outtextxy(4, 14, link_message());
    tgi_setcolor(COLOR_LIGHTBLUE);
    diagnostic_field(4, 28, 'H', link_stats.hello_attempts);
    diagnostic_field(84, 28, 'R', link_stats.rx_bytes);
    diagnostic_field(4, 40, 'T', link_stats.tx_bytes);
    diagnostic_field(84, 40, 'V', link_stats.valid_frames);
    diagnostic_field(4, 52, 'B', link_stats.bad_frames);
    diagnostic_field(84, 52, 'E', link_stats.serial_errors);
    diagnostic_field(4, 64, 'O', link_stats.rx_overflows);
    diagnostic_field(84, 64, 'S', link_stats.serial_status);
    diagnostic_field(4, 76, 'F', link_stats.last_frame_error);
    diagnostic_field(84, 76, 'C', link_stats.crc_errors);
    diagnostic_field(4, 88, 'A', game.reliable_rev);
    diagnostic_field(84, 88, 'G', (uint16_t)((game.round_authorized << 2) |
                         (game.map_ready << 1) | game.snapshot_ready));
}

static uint8_t brick_at(uint8_t x, uint8_t y)
{
    uint16_t index = (uint16_t)y * 20 + x;
    return (game.bricks[index >> 3] >> (index & 7)) & 1;
}

static void draw_maze(void (*pump)(void))
{
    uint8_t x;
    uint8_t y;
    uint8_t start;
    uint8_t wall;
    for (y = 0; y < 19; ++y) {
        x = 0;
        while (x < 20) {
            wall = brick_at(x, y);
            if (!wall) {
                ++x;
                continue;
            }
            start = x;
            while (x < 20 && brick_at(x, y)) ++x;
            tgi_setcolor(y == 0 || y == 18 ? COLOR_BLUE : COLOR_LIGHTGREY);
            tgi_bar(BOARD_X + start * CELL, BOARD_Y + y * CELL,
                    BOARD_X + x * CELL - 1, BOARD_Y + y * CELL + CELL - 1);
        }
        if (pump) pump();
    }
}

static void draw_shots(void)
{
    uint8_t i;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        const struct game_shot *shot = &game.shots[i];
        if (!shot->active || shot->x >= 20 || shot->y >= 19) continue;
        tgi_setcolor(COLOR_WHITE);
        tgi_bar(BOARD_X + shot->x * CELL + 1,
                BOARD_Y + shot->y * CELL + 1,
                BOARD_X + shot->x * CELL + 2,
                BOARD_Y + shot->y * CELL + 2);
    }
}

static void draw_players(void)
{
    uint8_t i;
    uint8_t x;
    uint8_t y;
    uint8_t facing;
    for (i = 0; i < GAME_PLAYERS; ++i) {
        const struct game_player *player = &game.players[i];
        if (!((game.seat_mask | game.zombie_mask) & (1u << i)) ||
            player->respawning || player->x >= 20 || player->y >= 19)
            continue;
        x = BOARD_X + player->x * CELL;
        y = BOARD_Y + player->y * CELL;
        tgi_setcolor(player_colors[i]);
        tgi_bar(x, y, x + 3, y + 3);
        facing = player->joy & 15;
        tgi_setcolor(i == game.local_pid ? COLOR_WHITE : COLOR_BLACK);
        if (facing == 14) tgi_setpixel(x + 1, y);
        else if (facing == 7) tgi_setpixel(x + 3, y + 1);
        else if (facing == 13) tgi_setpixel(x + 1, y + 3);
        else if (facing == 11) tgi_setpixel(x, y + 1);
    }
}

static void draw_seat(uint8_t i)
{
    char name[9];
    char score[6];
    uint8_t j;
    uint8_t points = game.players[i].score;
    uint8_t row_y = 16 + i * 18;
    uint8_t occupied = (game.seat_mask | game.zombie_mask) & (1u << i);
    if (!occupied) {
        const char vacant[9] = "VACANT  ";
        for (j = 0; j < 8; ++j) name[j] = vacant[j];
    } else if (game.zombie_mask & (1u << i)) {
        const char zombie[9] = "ZOMBIE  ";
        for (j = 0; j < 8; ++j) name[j] = zombie[j];
    } else {
        for (j = 0; j < GAME_NAME_LEN; ++j)
            name[j] = game.names[i][j] ? game.names[i][j] : ' ';
    }
    name[8] = 0;
    score[0] = 'S'; score[1] = ':';
    score[2] = (char)('0' + points / 100);
    score[3] = (char)('0' + (points / 10) % 10);
    score[4] = (char)('0' + points % 10);
    score[5] = 0;
    tgi_setcolor(occupied ? player_colors[i] : COLOR_LIGHTGREY);
    {
        char seat[2];
        seat[0] = i == game.local_pid ? '*' : (char)('1' + i);
        seat[1] = 0;
        tgi_outtextxy(HUD_X, row_y, seat);
    }
    tgi_outtextxy(HUD_X + 9, row_y, name);
    if (occupied) tgi_outtextxy(HUD_X + 9, row_y + 8, score);
}

static void draw_game(void (*pump)(void))
{
    uint8_t i;
    char line[10];
    tgi_setcolor(COLOR_YELLOW);
    tgi_outtextxy(2, 2, "MAZE WAR");
    tgi_setcolor(COLOR_LIGHTBLUE);
    line[0] = 'R';
    line[1] = ':';
    line[2] = hex_digit(game.round_id >> 4);
    line[3] = hex_digit(game.round_id);
    line[4] = 0;
    tgi_outtextxy(HUD_X, 2, line);
    if (game.map_ready) draw_maze(pump);
    if (game.snapshot_ready) {
        draw_shots();
        draw_players();
    }
    if (pump) pump();
    for (i = 0; i < GAME_PLAYERS; ++i) {
        draw_seat(i);
        if (pump) pump();
    }
    tgi_setcolor(COLOR_WHITE);
    if (link_state != LINK_WELCOME)
        tgi_outtextxy(2, 91, link_message());
    else if (game.phase)
        tgi_outtextxy(HUD_X, 91, "ROUND OVER");
    else if (!game_ready())
        tgi_outtextxy(HUD_X, 91, "SYNCING");
    else
        tgi_outtextxy(HUD_X, 91, "PLAYING");
    if (game.phase && game.winner_pid < GAME_PLAYERS) {
        line[0] = 'P'; line[1] = (char)('1' + game.winner_pid);
        line[2] = ' '; line[3] = 'W'; line[4] = 'I';
        line[5] = 'N'; line[6] = 'S'; line[7] = 0;
        tgi_setcolor(player_colors[game.winner_pid]);
        tgi_outtextxy(2, 91, line);
    }
    tgi_setcolor(COLOR_LIGHTBLUE);
    tgi_outtextxy(151, 2, "B");
}

/* Ten four-pixel pitches fit each 40-pixel player slot: name[8] + score[2]. */
static const uint8_t mini_font[38][4] = {
    {2,5,7,5}, {6,5,6,7}, {3,4,4,3}, {6,5,5,6},
    {7,6,4,7}, {7,6,4,4}, {3,4,5,3}, {5,7,5,5},
    {7,2,2,7}, {1,1,5,2}, {5,6,6,5}, {4,4,4,7},
    {5,7,7,5}, {5,7,7,5}, {2,5,5,2}, {6,5,6,4},
    {2,5,7,3}, {6,5,6,5}, {3,4,3,6}, {7,2,2,2},
    {5,5,5,7}, {5,5,5,2}, {5,5,7,7}, {5,2,2,5},
    {5,5,2,2}, {7,1,2,7},
    {7,5,5,7}, {2,6,2,7}, {7,1,6,7}, {7,1,3,7},
    {5,5,7,1}, {7,4,3,6}, {3,4,7,7}, {7,1,1,1},
    {7,5,7,7}, {7,5,7,1},
    {0,0,7,0}, {0,0,0,2}
};

static void mini_text(uint8_t x, uint8_t y, const char *line)
{
    uint8_t index;
    uint8_t row;
    uint8_t col;
    uint8_t bits;
    while (*line) {
        if (*line >= 'A' && *line <= 'Z') index = (uint8_t)(*line - 'A');
        else if (*line >= '0' && *line <= '9') index = (uint8_t)(*line - '0' + 26);
        else if (*line == '-') index = 36;
        else if (*line == '.') index = 37;
        else { ++line; x += 4; continue; }
        for (row = 0; row < 4; ++row) {
            bits = mini_font[index][row];
            for (col = 0; col < 3; ++col)
                if (bits & (4 >> col)) tgi_setpixel(x + col, y + row);
        }
        ++line;
        x += 4;
    }
}

static void draw_score(uint8_t slot)
{
    char line[11];
    uint8_t i;
    uint8_t score = game.players[slot].score;
    uint8_t occupied = (game.seat_mask | game.zombie_mask) & (1u << slot);
    const char *role = (game.zombie_mask & (1u << slot)) ? "ZOMBIE  " :
                       ((game.seat_mask & (1u << slot)) ? 0 : "VACANT  ");
    for (i = 0; i < GAME_NAME_LEN; ++i)
        line[i] = role ? role[i] :
            (game.names[slot][i] ? game.names[slot][i] : ' ');
    line[8] = occupied ? (char)('0' + (score / 10) % 10) : '-';
    line[9] = occupied ? (char)('0' + score % 10) : '-';
    line[10] = 0;
    tgi_setcolor(occupied ? player_colors[slot] : COLOR_LIGHTGREY);
    mini_text((uint8_t)(slot * 40), 97, line);
    if (slot == game.local_pid) {
        tgi_setcolor(COLOR_WHITE);
        tgi_bar(slot * 40, 101, slot * 40 + 3, 101);
    }
}

static uint8_t status_code(void)
{
    if (link_state != LINK_WELCOME) return (uint8_t)(link_state + 1);
    if (game.phase) return 16;
    if (!game_ready()) return 17;
    return 0;
}

static void draw_scores_if_changed(void)
{
    uint8_t signature[40];
    uint8_t i;
    signature[0] = game.seat_mask;
    signature[1] = game.zombie_mask;
    signature[2] = game.local_pid;
    signature[3] = game.phase;
    for (i = 0; i < GAME_PLAYERS; ++i)
        signature[4 + i] = game.players[i].score;
    memcpy(signature + 8, game.names, sizeof(game.names));
    if (hud_valid[draw_page] &&
        memcmp(signature, hud_signature[draw_page], sizeof(signature)) == 0)
        return;
    tgi_setcolor(COLOR_BLACK);
    tgi_bar(0, PLAYFIELD_H, 159, 101);
    for (i = 0; i < GAME_PLAYERS; ++i)
        draw_score(i);
    memcpy(hud_signature[draw_page], signature, sizeof(signature));
    hud_valid[draw_page] = 1;
}

static void draw_full_map(void (*pump)(void))
{
    uint8_t status = status_code();
    char result[8] = "P1 WINS";
    if (status != previous_status[draw_page]) {
        playfield_invalidate_page(draw_page);
        previous_status[draw_page] = status;
    }
    playfield_draw(frame_page[draw_page], draw_page, &game, pump);
    draw_scores_if_changed();
    if (status) {
        tgi_setcolor(COLOR_BLACK);
        tgi_bar(37, 30, 123, 44);
        tgi_setcolor(COLOR_WHITE);
        if (link_state != LINK_WELCOME)
            tgi_outtextxy(44, 33, link_message());
        else if (game.phase && game.winner_pid < GAME_PLAYERS) {
            result[1] = (char)('1' + game.winner_pid);
            tgi_outtextxy(52, 33, result);
        } else
            tgi_outtextxy(52, 33, "SYNCING");
    }
}

void render_init(void (*pump)(void))
{
    uint8_t i;
    uint8_t changed0;
    uint8_t changed1;
    while (tgi_busy()) if (pump) pump();
    playfield_invalidate();
    hud_valid[0] = hud_valid[1] = 0;
    previous_status[0] = previous_status[1] = 255;
    for (i = 0; i < 2; ++i) {
        while (tgi_busy()) if (pump) pump();
        tgi_clear();
        tgi_updatedisplay();
    }
    while (tgi_busy()) if (pump) pump();
    frame_page[0][0] = 0x11;
    frame_page[1][0] = 0x11;
    tgi_setcolor(COLOR_YELLOW);
    tgi_setpixel(0, 0);
    changed0 = frame_page[0][0] != 0x11;
    changed1 = frame_page[1][0] != 0x11;
    direct_page_ok = changed0 != changed1;
    draw_page = changed0 ? 0 : 1;
}

void render_draw(uint8_t diagnostics, void (*pump)(void))
{
    tgi_setbgcolor(COLOR_BLACK);
    if (diagnostics || !direct_page_ok) {
        playfield_invalidate();
        hud_valid[0] = hud_valid[1] = 0;
        previous_status[0] = previous_status[1] = 255;
        tgi_clear();
        if (diagnostics) draw_diagnostics();
        else draw_game(pump);
    } else {
        draw_full_map(pump);
    }
    tgi_updatedisplay();
    draw_page ^= 1;
    if (game.fire_cue_frames) --game.fire_cue_frames;
}
