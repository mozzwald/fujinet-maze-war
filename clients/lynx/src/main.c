#include <lynx.h>
#include <tgi.h>
#include <joystick.h>
#include <time.h>

#include "netstream.h"
#include "protocol.h"
#include "render.h"
#include "startup.h"
#include "setup.h"
#include "sound.h"
#include "boot_guard.h"

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
    sound_update();
}

void main(void)
{
    uint8_t buttons;
    uint8_t diagnostics;
    boot_guard(1);
    tgi_install(lynx_160_102_16_tgi);
    tgi_init();
    /* RAM below the video pages is game data, not a collision bitmap. */
    tgi_setcollisiondetection(0);
    while (tgi_busy()) netstream_poll();
    tgi_setframerate(60);
    joy_install(lynx_stdjoy_joy);
    sound_init();
    if (!startup_run()) {
        link_state = LINK_SERIAL_ERROR;
    } else {
        netstream_start(client_setup.host, client_setup.port);
        game_set_name(client_setup.name);
    }
    render_init(netstream_poll);
    for (;;) {
        while (tgi_busy()) pump_game();
        buttons = joy_read(JOY_1);
        sample_buttons(buttons);
        netstream_tick();
        sound_update();
        diagnostics = JOY_BTN_2(buttons) != 0;
        render_draw(diagnostics, pump_game);
    }
}
