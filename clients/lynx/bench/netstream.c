/* Instrument the current transport without changing production code. */
#include "../src/netstream.h"
#include "../src/protocol.h"
#include "bench.h"

uint8_t bench_measuring;
unsigned long bench_max_poll_gap;
unsigned long bench_max_batch;
uint16_t bench_polls;
uint16_t bench_missing;
static unsigned long last_poll;
static uint8_t last_seq;
static uint8_t have_seq;

static uint8_t bench_receive(const uint8_t *p, uint8_t len)
{
    if (bench_measuring && len == 7 && p[0] == 0x42 && p[2] == 3) {
        if (have_seq) bench_missing += (uint8_t)(p[1] - last_seq - 1);
        last_seq = p[1];
        have_seq = 1;
    }
    return game_receive(p, len);
}

#define game_receive bench_receive
#define netstream_poll core_netstream_poll
#include "../src/netstream.c"
#undef netstream_poll
#undef game_receive

void bench_poll_reset(void)
{
    bench_max_poll_gap = bench_max_batch = 0;
    bench_polls = bench_missing = 0;
    have_seq = 0;
    last_poll = bench_now();
}

void bench_poll(void)
{
    unsigned long now = bench_now();
    unsigned long gap = (now - last_poll) & 0xffffffUL;
    if (bench_measuring && gap > bench_max_poll_gap)
        bench_max_poll_gap = gap;
    last_poll = now;
    if (bench_measuring) ++bench_polls;
    core_netstream_poll();
}

void netstream_poll(void)
{
    bench_poll();
}
