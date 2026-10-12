#include <lynx.h>
#include <tgi.h>
#include <serial.h>
#include <stdint.h>
#include <string.h>
#include "../src/netstream.h"
#include "../src/protocol.h"
#include "../src/render.h"
#include "../src/frame.h"
#include "bench.h"

static unsigned long renders[120];
static unsigned long frames[120];
static uint8_t report[60];

static void put16(uint8_t *p, uint16_t n)
{ p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void put32(uint8_t *p, unsigned long n)
{ put16(p, (uint16_t)n); put16(p + 2, (uint16_t)(n >> 16)); }

static void send(const uint8_t *p, uint8_t n)
{
    uint8_t wire[FRAME_WIRE_MAX];
    uint8_t len = frame_encode(p, n, wire, sizeof(wire));
    uint8_t i;
    for (i = 0; i < len; ++i) {
        while (ser_put((char)wire[i]) != SER_ERR_OK) bench_poll();
        bench_poll();
    }
}

static void settle(unsigned long us)
{
    unsigned long start = bench_now();
    while (((bench_now() - start) & 0xffffffUL) < us) bench_poll();
}

static void wall_cell(uint8_t x, uint8_t y, uint8_t present)
{
    uint16_t cell = (uint16_t)y * 20 + x;
    if (present) game.bricks[cell >> 3] |= (uint8_t)(1u << (cell & 7));
    else game.bricks[cell >> 3] &= (uint8_t)~(1u << (cell & 7));
}

static void scene(uint8_t actors)
{
    uint8_t x, y, i;
    game_reset();
    game.have_welcome = game.round_authorized = 1;
    game.map_ready = game.snapshot_ready = 1;
    game.round_id = 1; game.kill_limit = 10;
    game.seat_mask = actors == 1 ? 1 : 15;
    game.input_joy = 15;
    for (y = 0; y < 19; ++y)
        for (x = 0; x < 20; ++x)
            if (!x || x == 19 || !y || y == 18 ||
                ((y == 6 || y == 10) && x >= 2 && x <= 17))
                wall_cell(x, y, 1);
    for (i = 0; i < 4; ++i) {
        game.players[i].x = 3 + i * 2;
        game.players[i].y = i == 3 ? 17 : 3 + i * 4;
        game.players[i].joy = i & 1 ? 11 : 7;
        memcpy(game.names[i], "PLAYER01", 8);
        game.names[i][7] = '1' + i;
        if (i < (actors == 1 ? 1 : 3)) {
            game.shots[i].active = 1;
            game.shots[i].x = 4 + i * 3;
            game.shots[i].y = 4 + i * 4;
        }
    }
}

static void motion(uint8_t frame)
{
    uint8_t i, step = frame / 6;
    uint8_t x = step < 10 ? 3 + step : 23 - step;
    if (x > 17) x = 17;
    for (i = 0; i < 4; ++i) {
        game.players[i].x = x;
        game.players[i].joy = step < 10 ? 7 : 11;
        if (game.shots[i].active) game.shots[i].x = 2 + (frame / 2 + i * 3) % 16;
    }
}

static void render_frame(uint8_t sample)
{
    unsigned long start, end;
    while (tgi_busy()) bench_poll();
    bench_poll();
    start = bench_now();
    render_draw(0, bench_poll);
    end = bench_now();
    renders[sample] = (end - start) & 0xffffffUL;
    while (tgi_busy()) bench_poll();
    frames[sample] = (bench_now() - start) & 0xffffffUL;
}

static void phase(uint8_t scenario, uint8_t count)
{
    uint8_t i, j, n;
    uint16_t before_rx, before_bad, before_serial, before_overflow;
    unsigned long start, elapsed;
    report[0] = 0x7c; report[1] = bench_mode; report[2] = scenario;
    report[3] = bench_batch_limit; report[4] = count;
    send(report, 5);
    settle(50000UL);
    before_rx = link_stats.valid_frames;
    before_bad = link_stats.bad_frames;
    before_serial = link_stats.serial_errors;
    before_overflow = link_stats.rx_overflows;
    bench_poll_reset();
    bench_measuring = 1;
    elapsed = 0;
    for (i = 0; i < count; ++i) {
        start = bench_now();
        if (scenario == 3 && !(i & 1)) wall_cell(2 + i / 2, 6, 0);
        if (scenario == 1 || scenario == 2 || scenario == 3) motion(i);
        if (scenario == 4) game.shots[0].active = (i % 6) < 3;
        render_frame(i);
        elapsed += (bench_now() - start) & 0xffffffUL;
    }
    bench_poll();
    bench_measuring = 0;
    report[0] = 0x7d; report[1] = bench_mode; report[2] = scenario;
    report[3] = count;
    put32(report + 4, elapsed);
    put32(report + 8, bench_max_poll_gap);
    put32(report + 12, bench_max_batch);
    put16(report + 16, link_stats.valid_frames - before_rx);
    put16(report + 18, link_stats.bad_frames - before_bad);
    put16(report + 20, link_stats.serial_errors - before_serial);
    put16(report + 22, link_stats.rx_overflows - before_overflow);
    put16(report + 24, bench_missing);
    put16(report + 26, bench_polls);
    send(report, 28);
    for (i = 0; i < count; i += 5) {
        report[0] = 0x7e; report[1] = bench_mode; report[2] = scenario;
        report[3] = i;
        n = count - i > 5 ? 5 : count - i;
        report[4] = n;
        for (j = 0; j < n; ++j) {
            put32(report + 5 + j * 8, renders[i + j]);
            put32(report + 9 + j * 8, frames[i + j]);
        }
        send(report, 5 + n * 8);
    }
    settle(50000UL);
}

void main(void)
{
    uint8_t mode;
    tgi_install(lynx_160_102_16_tgi);
    tgi_init();
    while (tgi_busy()) {}
    tgi_setframerate(60);
    bench_clock_init();
    if (!netstream_start("benchmark", 19125)) for (;;) {}
    while (link_state != LINK_WELCOME) bench_poll();
    for (mode = 0; mode < 5; ++mode) {
        bench_mode = mode;
        bench_batch_limit = mode < 2 ? 8 : (uint8_t)(4u << (mode - 2));
        scene(4);
        render_init(bench_poll);
        phase(0, 2);
        scene(1);
        render_frame(0); render_frame(0);
        phase(1, 60);
        scene(4);
        render_frame(0); render_frame(0);
        phase(2, 60);
        phase(3, 24);
        phase(4, 12);
        scene(4);
        game.players[0].x = 5; game.players[0].y = 17;
        game.players[3].x = 15;
        bench_render_reset();
        render_frame(0); render_frame(0);
        report[0] = 0x7b; report[1] = mode;
        send(report, 2);
        settle(300000UL);
    }
    report[0] = 0x7f;
    send(report, 1);
    for (;;) bench_poll();
}
