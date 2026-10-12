#ifndef LYNX_RENDER_BENCH_H
#define LYNX_RENDER_BENCH_H
#include <stdint.h>

extern uint8_t bench_mode;
extern uint8_t bench_batch_limit;
extern uint8_t bench_measuring;
extern unsigned long bench_max_poll_gap;
extern unsigned long bench_max_batch;
extern uint16_t bench_polls;
extern uint16_t bench_missing;

void bench_clock_init(void);
unsigned long bench_now(void);
void bench_poll(void);
void bench_poll_reset(void);
void bench_render_reset(void);

#endif
