#include "suzy_host.h"
#include <assert.h>
static uint8_t *target;
static unsigned max_batch;
static unsigned bits(const uint8_t *data, unsigned *at, unsigned count)
{
    unsigned value = 0;
    while (count--) {
        value = (value << 1) | ((data[*at >> 3] >> (7 - (*at & 7))) & 1);
        ++*at;
    }
    return value;
}
void suzy_test_target(uint8_t *page) { target = page; }
unsigned suzy_test_max_batch(void) { return max_batch; }
void tgi_sprite(SCB_REHV_PAL *scb)
{
    uint8_t *data;
    unsigned x, y, n, i, pen, color, length, at, total, literal, run;
    uint8_t pens[512];
    uint16_t offset;
    unsigned count = 0;
    do {
        ++count;
        assert(count <= 8);
        assert(scb->hsize == 0x100 && scb->vsize == 0x100);
        assert((scb->sprctl1 & REHV) == REHV);
        data = scb->data;
        y = (unsigned)scb->vpos;
        while (*data) {
            length = *data++ - 1;
            if (scb->sprctl1 & LITERAL) {
                n = length * 2;
                for (i = 0; i < n; ++i)
                    pens[i] = (i & 1) ? (data[i >> 1] & 15) : (data[i >> 1] >> 4);
                /* Real Suzy's byte-end bug is absent in Handy. A trailing
                   zero is also an end marker, not an opaque final pixel. */
                if (n && (!pens[n-1] || (pens[n-1] & 1))) --n;
            } else {
                at = n = 0; total = length * 8;
                while (at + 5 <= total) {
                    literal = bits(data, &at, 1);
                    run = bits(data, &at, 4);
                    if (!literal && !run) break;
                    ++run;
                    assert(n + run <= sizeof(pens));
                    if (literal) {
                        assert(at + run * 4 <= total);
                        while (run--) pens[n++] = bits(data, &at, 4);
                    } else {
                        assert(at + 4 <= total);
                        pen = bits(data, &at, 4);
                        while (run--) pens[n++] = pen;
                    }
                }
            }
            for (i = 0; i < n; ++i) {
                pen = pens[i];
                color = (pen & 1) ? (scb->penpal[pen >> 1] & 15) :
                                     (scb->penpal[pen >> 1] >> 4);
                if ((scb->sprctl0 & 7) == TYPE_NONCOLL && !color) continue;
                x = (unsigned)scb->hpos + i;
                if (x >= 160 || y >= 102) continue;
                offset = (uint16_t)y * 80 + (x >> 1);
                if (x & 1) target[offset] = (target[offset] & 0xf0) | color;
                else target[offset] = (target[offset] & 15) | (color << 4);
            }
            data += length;
            ++y;
        }
        scb = (SCB_REHV_PAL *)scb->next;
    } while (scb);
    if (count > max_batch) max_batch = count;
}

