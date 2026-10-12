#include <lynx.h>
#include <tgi.h>
#include <joystick.h>
#include <time.h>

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

static void sample_buttons(uint8_t buttons)
{
    uint8_t directions = 0;
    if (JOY_UP(buttons)) directions |= 1;
    if (JOY_RIGHT(buttons)) directions |= 2;
    if (JOY_DOWN(buttons)) directions |= 4;
    if (JOY_LEFT(buttons)) directions |= 8;
    game_sample_input(directions, JOY_BTN_1(buttons) != 0, (uint16_t)clock());
}

static void pump_game(void)
{
    sample_buttons(joy_read(JOY_1));
    netstream_service();
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
        while (tgi_busy()) pump_game();
        buttons = joy_read(JOY_1);
        sample_buttons(buttons);
        netstream_tick();
        diagnostics = JOY_BTN_2(buttons) != 0;
        render_draw(diagnostics, pump_game);
    }
}
