/* Keep an independent CPU oracle for production renderer regression tests. */
#define REFERENCE_PLAIN_FLOOR
#define playfield_draw cpu_playfield_draw
#define playfield_invalidate cpu_playfield_invalidate
#define playfield_invalidate_page cpu_playfield_invalidate_page
#define playfield_bottom_row cpu_playfield_bottom_row
#include "../bench/reference_playfield.c"
