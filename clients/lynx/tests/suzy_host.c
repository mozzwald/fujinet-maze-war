#include "suzy_host.h"
#include <assert.h>
static uint8_t *target;
static unsigned max_batch;
void suzy_test_target(uint8_t *page) { target = page; }
unsigned suzy_test_max_batch(void) { return max_batch; }
void tgi_sprite(SCB_REHV_PAL *scb)
{
    uint8_t *data;
    unsigned x, y, n, i, pen, color;
    uint16_t offset;
    unsigned count = 0;
    do {
        ++count;
        assert(count <= 8);
        assert(scb->hsize == 0x100 && scb->vsize == 0x100);
        assert((scb->sprctl1 & (LITERAL | REHV)) == (LITERAL | REHV));
        data = scb->data;
        y = (unsigned)scb->vpos;
        while (*data) {
            n = (*data++ - 1) * 2;
            for (i = 0; i < n; ++i) {
                pen = (i & 1) ? (data[i >> 1] & 15) : (data[i >> 1] >> 4);
                if (i == n - 1 && !pen) break; /* literal trailing zero */
                color = (pen & 1) ? (scb->penpal[pen >> 1] & 15) :
                                     (scb->penpal[pen >> 1] >> 4);
                if ((scb->sprctl0 & 7) == TYPE_NONCOLL && !color) continue;
                x = (unsigned)scb->hpos + i;
                if (x >= 160 || y >= 102) continue;
                offset = (uint16_t)y * 80 + (x >> 1);
                if (x & 1) target[offset] = (target[offset] & 0xf0) | color;
                else target[offset] = (target[offset] & 15) | (color << 4);
            }
            data += n / 2;
            ++y;
        }
        scb = (SCB_REHV_PAL *)scb->next;
    } while (scb);
    if (count > max_batch) max_batch = count;
}

