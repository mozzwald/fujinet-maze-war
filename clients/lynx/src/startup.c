#include <stdint.h>
#include <lynx.h>
#include <tgi.h>
#include <joystick.h>
#include <string.h>
#include "startup.h"
#include "setup.h"
#include "command.h"
#include "render.h"

static uint8_t last_buttons;
static uint16_t repeat_at;
static uint8_t saved = 1;
static void begin_screen(const char *title)
{
    while (tgi_busy()) {}
    tgi_setbgcolor(COLOR_BLACK);
    tgi_clear();
    tgi_setcolor(COLOR_YELLOW);
    tgi_outtextxy(4,3,title);
    tgi_setcolor(COLOR_WHITE);
}
static void show(void) { tgi_updatedisplay(); }
static void release_buttons(void)
{
    while (tgi_busy()) {}
    while (SUZY.joystick) {}
    last_buttons = 0;
}
static uint8_t buttons(void)
{
    uint8_t now = SUZY.joystick, fresh = now & (uint8_t)~last_buttons;
    uint16_t ticks = command_clock();
    if (now != last_buttons) repeat_at = ticks+15;
    else if ((now & 0xf0) && (int16_t)(ticks-repeat_at) >= 0) {
        fresh |= now & 0xf0;
        repeat_at = ticks+6;
    }
    last_buttons = now;
    return fresh;
}
/* Battleship-inspired character selector, implemented with Maze War UI. */
static uint8_t edit_name(void)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.";
    char previous[9], draft[9], checked[9], choice[2];
    uint8_t selected = 0, len, b, dirty = 1;
    memcpy(previous, client_setup.name, 9);
    memcpy(draft, previous, 9);
    len = (uint8_t)strlen(draft);
    release_buttons();
    for (;;) {
        if (dirty) {
            begin_screen("ENTER YOUR NAME");
            tgi_outtextxy(4,22,draft);
            tgi_setcolor(COLOR_LIGHTBLUE);
            tgi_bar(4+len*8,32,8+len*8,33);
            tgi_setcolor(COLOR_YELLOW);
            if (!selected) tgi_outtextxy(4,44,"DONE");
            else if (alphabet[selected-1] == ' ') tgi_outtextxy(4,44,"SPACE");
            else { choice[0] = alphabet[selected-1]; choice[1] = 0; tgi_outtextxy(4,44,choice); }
            tgi_setcolor(COLOR_WHITE);
            tgi_outtextxy(4,62,"D-PAD CHOOSE");
            tgi_outtextxy(4,74,"A SELECT  B DELETE");
            tgi_outtextxy(4,86,previous[0] ? "OPT2 CANCEL" : "A ON DONE FINISH");
            show();
            dirty = 0;
        }
        while (tgi_busy()) {}
        b = buttons();
        if (b) dirty = 1;
        if ((b & 4) && previous[0]) { release_buttons(); return 0; }
        if (JOY_UP(b) || JOY_LEFT(b)) selected = selected < sizeof(alphabet)-1 ? selected+1 : 0;
        if (JOY_DOWN(b) || JOY_RIGHT(b)) selected = selected ? selected-1 : sizeof(alphabet)-1;
        if (JOY_BTN_2(b) && len) draft[--len] = 0;
        if (JOY_BTN_1(b)) {
            if (!selected) {
                if (setup_name(checked,(const uint8_t *)draft,len)) {
                    strcpy(client_setup.name, checked);
                    begin_screen("SAVING NAME"); show();
                    saved = setup_save_name();
                    release_buttons();
                    return 1;
                }
            } else if (len < 8) {
                draft[len++] = alphabet[selected-1]; draft[len] = 0;
                selected = 0;
            }
        }
    }
}
static void lobby_screen(uint8_t result, uint8_t selected)
{
    uint8_t i, y;
    char greeting[20] = "HELLO ", occupancy[8] = "0/0", page[9] = "PAGE 1";
    struct lobby_room *room;
    begin_screen("MAZE WAR LOBBY");
    strcat(greeting,client_setup.name);
    render_small_text(4,17,greeting);
    page[5] += client_setup.page;
    render_small_text(130,18,page+5);
    if (result != LOBBY_OK) {
        tgi_setcolor(COLOR_LIGHTBLUE);
        tgi_outtextxy(4,35,result == LOBBY_EMPTY ? "NO ROOMS" : "LOBBY UNAVAILABLE");
        tgi_outtextxy(4,50,"B DEFAULT ROOM");
    }
    for (i = 0; i < client_setup.count; ++i) {
        room = &client_setup.rooms[i]; y = 27+i*14;
        tgi_setcolor(i == selected ? COLOR_YELLOW : COLOR_WHITE);
        if (i == selected) tgi_bar(2,y,3,y+3);
        render_small_text(8,y,room->label);
        occupancy[0] = '0'+room->players; occupancy[2] = '0'+room->maximum;
        render_small_text(8,y+6,occupancy);
        if (room->players == room->maximum) render_small_text(28,y+6,"FULL");
    }
    tgi_setcolor(COLOR_WHITE);
    render_small_text(4,86,"A JOIN  B DEFAULT  LEFT RIGHT PAGE");
    render_small_text(4,93,"OPT1 REFRESH  OPT2 EDIT NAME");
    if (result == LOBBY_ERROR || !saved)
        render_small_text(4,80,command_error_text());
    if (!saved) { tgi_setcolor(COLOR_YELLOW); render_small_text(72,17,"NAME NOT SAVED"); }
    show();
}
uint8_t startup_run(void)
{
    uint8_t result, selected = 0, b, refresh = 1, dirty = 1;
    setup_defaults();
    begin_screen("LOADING YOUR NAME"); show();
    if (!command_open()) return 0;
    if (!setup_load_name()) edit_name();
    release_buttons();
    for (;;) {
        if (refresh) {
            begin_screen("FETCHING LOBBY");
            tgi_outtextxy(4,30,"PLEASE WAIT"); show();
            result = setup_lobby(); selected = 0; refresh = 0; dirty = 1;
            release_buttons();
        }
        if (dirty) { lobby_screen(result,selected); dirty = 0; }
        while (tgi_busy()) {}
        b = buttons();
        if (b) dirty = 1;
        if (b & 4) { edit_name(); release_buttons(); }
        if (b & 8) refresh = 1;
        if (JOY_DOWN(b) && selected+1 < client_setup.count) ++selected;
        if (JOY_UP(b) && selected) --selected;
        if (JOY_LEFT(b) && client_setup.page) { --client_setup.page; refresh = 1; }
        if (JOY_RIGHT(b) && client_setup.page < 7 && client_setup.records == 4) {
            ++client_setup.page; refresh = 1;
        }
        if (JOY_BTN_2(b)) break;
        if (JOY_BTN_1(b) && client_setup.count &&
            client_setup.rooms[selected].players < client_setup.rooms[selected].maximum) {
            client_setup.port = client_setup.rooms[selected].port;
            break;
        }
    }
    release_buttons();
    return 1;
}
