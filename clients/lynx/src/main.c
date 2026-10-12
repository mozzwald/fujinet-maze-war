#include <lynx.h>
#include <tgi.h>
#include <joystick.h>

#include "netstream.h"
#include "protocol.h"
#include "render.h"

#ifndef SERVER_HOST
#define SERVER_HOST "127.0.0.1"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 9000
#endif

typedef char port_must_fit[(SERVER_PORT > 0 && SERVER_PORT <= 65535UL) ? 1 : -1];

static uint8_t read_game_joy(uint8_t buttons)
{
    uint8_t stick = 15;
    /* Server movement is cardinal. Resolve simultaneous directions before
       encoding so a diagonal never reaches the game protocol. */
    if (JOY_UP(buttons)) stick = 14;
    else if (JOY_RIGHT(buttons)) stick = 7;
    else if (JOY_DOWN(buttons)) stick = 13;
    else if (JOY_LEFT(buttons)) stick = 11;
    if (JOY_BTN_1(buttons)) stick |= 0x10;
    return stick;
}

static void pump_game(void)
{
    game_set_joy(read_game_joy(joy_read(JOY_1)));
    netstream_poll();
}

void main(void)
{
    uint8_t buttons;
    uint8_t diagnostics;
    tgi_install(lynx_160_102_16_tgi);
    tgi_init();
    while (tgi_busy()) netstream_poll();
    tgi_setframerate(60);
    joy_install(lynx_stdjoy_joy);
    render_init(netstream_poll);
    render_draw(0, netstream_poll);
    netstream_start(SERVER_HOST, SERVER_PORT);
    for (;;) {
        while (tgi_busy()) {
            game_set_joy(read_game_joy(joy_read(JOY_1)));
            netstream_poll();
        }
        buttons = joy_read(JOY_1);
        game_set_joy(read_game_joy(buttons));
        netstream_tick();
        diagnostics = JOY_BTN_2(buttons) != 0;
        render_draw(diagnostics, pump_game);
    }
}
