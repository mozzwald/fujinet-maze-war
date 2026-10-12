/* Minimal test adapter. Hardware values match cc65's _suzy.h; the host
   rasterizer validates ordering/cache pixels, not Suzy timing. */
#ifndef SUZY_BENCH_HOST_H
#define SUZY_BENCH_HOST_H
#include <stdint.h>
enum { BPP_4 = 0xc0, TYPE_NONCOLL = 5, TYPE_BACKNONCOLL = 1,
       LITERAL = 0x80, REHV = 0x10, NO_COLLIDE = 0x20 };
typedef struct SCB_REHV_PAL {
    uint8_t sprctl0, sprctl1, sprcoll;
    char *next;
    uint8_t *data;
    int hpos, vpos;
    unsigned hsize, vsize;
    uint8_t penpal[8];
} SCB_REHV_PAL;
void tgi_sprite(SCB_REHV_PAL *sprite);
#endif
