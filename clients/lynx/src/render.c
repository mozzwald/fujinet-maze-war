#include <lynx.h>
#include <tgi.h>
#include <string.h>


#include "netstream.h"
#include "protocol.h"
#include "playfield.h"
#include "render.h"
#include "colors.h"
#include "command.h"
#include "phase_scratch.h"


extern SCB_REHV_PAL maze_text_scb;
struct render_timing render_timing;
static uint16_t last_render_at;
static uint8_t timing_ready;

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
    case LINK_STALE: return "WAITING FOR SERVER";
    case LINK_REJOIN: return "REJOINING ROOM";
    case LINK_LOST: return "LINK LOST";
    }
    return "UNKNOWN";
}

static void draw_diagnostics(void)
{
    char latency[36] = "W0000 D0000 Q0000 F0000 C0000 I0000";
    char waits[30] = "U0000 S0000 M0000 G0000 J0000";
    char state[36];
    static const char labels[] = "PSERAJK";
    uint8_t field;
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
    for (field=0; field<TRACE_WORDS; ++field) {
        state[field*5] = labels[field];
        hex_word(state+field*5+1,reconcile_trace[field]);
    }
    render_small_text(4,84,state);
    hex_word(waits+1,link_stats.quiet_ticks); waits[5]=' ';
    hex_word(waits+7,game.repeat_stalls); waits[11]=' ';
    hex_word(waits+13,game.input_history); waits[17]=' ';
    hex_word(waits+19,link_stats.serial_api_errors); waits[23]=' ';
    hex_word(waits+25,link_stats.rejoins);
    render_small_text(4,90,waits);
    if (link_state == LINK_COMMAND_ERROR)
        render_small_text(4,96,command_error_text());
    else {
        hex_word(latency+1,game.repeat_window); latency[5]=' ';
        hex_word(latency+7,game.ack_delay); latency[11]=' ';
        hex_word(latency+13,game.pending_count); latency[17]=' ';
        hex_word(latency+19,render_timing.frame_ticks);
        latency[23]=' '; hex_word(latency+25,game.local_corrections);
        latency[29]=' '; hex_word(latency+31,game.input_retries);
        render_small_text(4,96,latency);
    }
}

/* Nine four-pixel pitches per slot: name[8] + score[1], then a blank pitch. */
#include "font_art.h"

/* Foreground-only text packing finishes before any serial callback. */
/* Disjoint per-function temporaries: foreground-only, assigned before use.
   Explicit globals let cc65 use true zero-page instructions; IRQs never use
   these and no owning function recursively reenters through a callback. */
#ifdef __CC65__
#pragma bss-name (push, "APPZP", "zp")
#endif
static uint8_t render_small_text_index;
static uint8_t render_small_text_row;
static uint8_t render_small_text_col;
static uint8_t render_small_text_bits;
static uint8_t render_small_text_count;
static uint8_t render_small_text_stride;
static char render_small_text_c;
#ifdef __CC65__
#pragma zpsym ("render_small_text_index")
#pragma zpsym ("render_small_text_row")
#pragma zpsym ("render_small_text_col")
#pragma zpsym ("render_small_text_bits")
#pragma zpsym ("render_small_text_count")
#pragma zpsym ("render_small_text_stride")
#pragma zpsym ("render_small_text_c")
#endif
static char draw_score_line[10];
static uint8_t draw_score_i;
static uint8_t draw_score_score;
static uint8_t draw_score_occupied;
static const char *draw_score_role;
#ifdef __CC65__
#pragma zpsym ("draw_score_line")
#pragma zpsym ("draw_score_i")
#pragma zpsym ("draw_score_score")
#pragma zpsym ("draw_score_occupied")
#pragma zpsym ("draw_score_role")
#endif
#ifdef __CC65__
#pragma bss-name (pop)
#endif

void render_small_text(uint8_t x, uint8_t y, const char *line)
{
    /* Text packing and maze/command scratch have disjoint lifetimes. */
#define bitmap phase_scratch.text
    if (x >= 160 || y > 96) return;
    render_small_text_count = (uint8_t)strlen(line);
    if (render_small_text_count > (160-x)/4) render_small_text_count = (160-x)/4;
    if (!render_small_text_count) return;
    /* A zero guard byte prevents real Suzy dropping the last text pixel. */
    render_small_text_stride = render_small_text_count*2+2;
    memset(bitmap,0,sizeof(bitmap));
    for (render_small_text_row = 0; render_small_text_row < 6; ++render_small_text_row) {
        bitmap[render_small_text_row*render_small_text_stride] = render_small_text_stride;
        for (render_small_text_col = 0; render_small_text_col < render_small_text_count; ++render_small_text_col) {
            render_small_text_c = line[render_small_text_col];
            if (render_small_text_c >= 'a' && render_small_text_c <= 'z') render_small_text_c -= 'a'-'A';
            if (render_small_text_c >= 'A' && render_small_text_c <= 'Z') render_small_text_index = render_small_text_c-'A';
            else if (render_small_text_c >= '0' && render_small_text_c <= '9') render_small_text_index = render_small_text_c-'0'+26;
            else if (render_small_text_c == '-') render_small_text_index = 36;
            else if (render_small_text_c == '.') render_small_text_index = 37;
            else if (render_small_text_c == '/') render_small_text_index = 38;
            else continue;
            render_small_text_bits = mini_font[render_small_text_index][render_small_text_row];
            bitmap[render_small_text_row*render_small_text_stride+1+render_small_text_col*2] = (render_small_text_bits & 8 ? 0x10 : 0) | (render_small_text_bits & 4 ? 1 : 0);
            bitmap[render_small_text_row*render_small_text_stride+2+render_small_text_col*2] = (render_small_text_bits & 2 ? 0x10 : 0) | (render_small_text_bits & 1 ? 1 : 0);
        }
    }
    /* One synchronous Suzy sprite per line, rather than hundreds of
       individual pixel sprites. Transparent pen zero preserves the HUD. */
    memset(&maze_text_scb,0,sizeof(maze_text_scb));
    maze_text_scb.sprctl0 = BPP_4 | TYPE_NONCOLL;
    maze_text_scb.sprctl1 = LITERAL | REHV;
    maze_text_scb.sprcoll = NO_COLLIDE;
    maze_text_scb.data = bitmap; maze_text_scb.hpos = x; maze_text_scb.vpos = y;
    maze_text_scb.hsize = maze_text_scb.vsize = 0x100;
    maze_text_scb.penpal[0] = tgi_getcolor();
    tgi_sprite(&maze_text_scb);
#undef bitmap
}

static void draw_score(uint8_t slot)
{
    draw_score_score = game.players[slot].score;
    draw_score_occupied = (game.seat_mask | game.zombie_mask) & (1u << slot);
    draw_score_role = (game.zombie_mask & (1u << slot)) ? "ZOMBIE  " :
                       ((game.seat_mask & (1u << slot)) ? 0 : "VACANT  ");
    for (draw_score_i = 0; draw_score_i < GAME_NAME_LEN; ++draw_score_i)
        draw_score_line[draw_score_i] = draw_score_role ? draw_score_role[draw_score_i] :
            (game.names[slot][draw_score_i] ? game.names[slot][draw_score_i] : ' ');
    draw_score_line[8] = draw_score_occupied ? (char)('0' + draw_score_score % 10) : '-';
    draw_score_line[9] = 0;
    tgi_setcolor(draw_score_occupied ? player_colors[slot] : COLOR_LIGHTGREY);
    render_small_text((uint8_t)(slot * 40), 95, draw_score_line);
    if (slot == game.local_pid) {
        tgi_setcolor(COLOR_WHITE);
        tgi_bar(slot * 40, 101, slot * 40 + 35, 101);
    }
}


static uint8_t status_code(void)
{
    if (link_state != LINK_WELCOME) return (uint8_t)(link_state + 1);
    if (game_round_stage(&game) == ROUND_RESULTS) return 18;
    if (game.phase) return 16;
    if (!game_ready()) return 17;
    return 0;
}

static void draw_round_results(void (*pump)(void))
{
    char line[32];
    uint8_t names[GAME_PLAYERS][GAME_NAME_LEN], scores[GAME_PLAYERS];
    uint8_t i, j, points, mask = game.seat_mask | game.zombie_mask;
    memcpy(names, game.names, sizeof(names));
    for (i = 0; i < GAME_PLAYERS; ++i) scores[i] = game.players[i].score;
    tgi_clear();
    tgi_setcolor(COLOR_YELLOW);
    tgi_outtextxy(40, 5, "ROUND OVER");
    i = game.winner_pid;
    for (j = 0; j < GAME_NAME_LEN; ++j) line[j] = game.names[i][j];
    line[8] = 0;
    strcat(line, !(game.zombie_mask & (1u << i)) && game.result_zombie_history ?
           " BEATS ZOMBIES" : " WINS");
    tgi_setcolor(player_colors[i]);
    render_small_text((160-strlen(line)*4)/2, 22, line);
    tgi_setcolor(COLOR_LIGHTGREY);
    render_small_text(24, 34, "PLAYER          SCORE");
    if (pump) pump();
    for (i = 0; i < GAME_PLAYERS; ++i) {
        if (!(mask & (1u << i))) continue;
        memset(line, ' ', 21);
        line[0] = '1'+i;
        for (j = 0; j < GAME_NAME_LEN; ++j) line[3+j] = names[i][j];
        points = scores[i];
        line[18] = points >= 10 ? '0'+(points/10)%10 : ' ';
        line[19] = '0'+points%10;
        line[20] = 0;
        tgi_setcolor(player_colors[i]);
        render_small_text(24, 45+i*10, line);
        if (pump) pump();
    }
    tgi_setcolor(COLOR_WHITE);
    render_small_text(60, 94, "NEXT ROUND");
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
    if (status == 18) {
        if (previous_status[draw_page] != status) {
            playfield_invalidate_page(draw_page);
            hud_valid[draw_page] = 0;
            draw_round_results(pump);
            previous_status[draw_page] = status;
        }
        return;
    }
    if (status != previous_status[draw_page]) {
        playfield_invalidate_page(draw_page);
        previous_status[draw_page] = status;
    }
    playfield_draw(frame_page[draw_page], draw_page, &game, pump);
    draw_scores_if_changed();
    if (status && status != 16) {
        tgi_setcolor(COLOR_BLACK);
        tgi_bar(0, 30, 159, 52);
        tgi_setcolor(COLOR_WHITE);
        render_small_text(12,33,link_state == LINK_WELCOME ? "SYNCING" : link_message());
        if (link_state == LINK_COMMAND_ERROR || link_state == LINK_SERIAL_ERROR)
            render_small_text(12,43,"RESET FUJINET THEN LYNX");
        else if (link_state == LINK_REJECT || link_state == LINK_TIMEOUT ||
                 link_state == LINK_TX_ERROR || link_state == LINK_LOST)
            render_small_text(12,43,"OPT1 RETRY SAME ROOM");
    }
}

void render_init(void (*pump)(void))
{
    uint8_t i;
    uint8_t changed0;
    uint8_t changed1;
    memset(&render_timing,0,sizeof(render_timing));
    timing_ready = 0;
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
    uint16_t started = command_clock();
    if (timing_ready) {
        render_timing.frame_ticks = (uint16_t)(started-last_render_at);
        if (render_timing.frame_ticks > render_timing.max_frame_ticks)
            render_timing.max_frame_ticks = render_timing.frame_ticks;
    }
    last_render_at = started; timing_ready = 1;
    tgi_setbgcolor(COLOR_BLACK);
    if (diagnostics || !direct_page_ok) {
        playfield_invalidate();
        hud_valid[0] = hud_valid[1] = 0;
        previous_status[0] = previous_status[1] = 255;
        tgi_clear();
        draw_diagnostics();
        if (!direct_page_ok) {
            tgi_setcolor(COLOR_YELLOW);
            render_small_text(4,96,"VIDEO PAGE ERROR");
        }
    } else {
        draw_full_map(pump);
    }
    tgi_updatedisplay();
    draw_page ^= 1;
    if (game.fire_cue_frames) --game.fire_cue_frames;
    render_timing.draw_ticks = (uint16_t)(command_clock()-started);
    if (render_timing.draw_ticks > render_timing.max_draw_ticks)
        render_timing.max_draw_ticks = render_timing.draw_ticks;
}
