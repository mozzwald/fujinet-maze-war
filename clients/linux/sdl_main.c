#include <signal.h>
#include "../../net/tcp_stream.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <SDL/SDL.h>
#ifdef __APPLE__
#undef main
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
  PKT_SNAPSHOT = 0x40,
  PKT_DELTA = 0x41,
  PKT_SHOT = 0x42,
  PKT_NAME = 0x43,
  PKT_SEATS = 0x44,
  PKT_RELIABLE_ACK = 0x45,
  PKT_HELLO = 0x46,
  PKT_WELCOME = 0x47,
  PKT_REJECT = 0x48,
  PKT_BRICK_FULL = 0x50,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52,
  PKT_RELIABLE_EVENT = 0x53,
  PKT_MATCH_END = 0x54,
  PKT_ROUND_START = 0x55
};

enum { MAX_PLAYERS = 4 };
enum { PROTOCOL_VERSION = 1, ROUND_PLAYING = 0, ROUND_OVER = 1 };
enum { MAZE_W = 20, MAZE_H = 19 };
enum { HOST_MAX = 63 };
/* Matches the Atari HUD field; the server sanitizes and space-pads. */
enum { NAME_LEN = 8 };
enum { NAME_RESEND_MS = 2000 };
enum { FX_MAX = 96 };

enum {
  FX_BRICK = 1,
  FX_PLAYER_EXPLODE = 2,
  FX_PLAYER_SPAWN = 3,
  FX_SHOT_SPARK = 4
};

struct player_state {
  uint8_t x;
  uint8_t y;
  uint8_t joy;
  uint8_t score;
};

struct player_anim {
  int known;
  float from_x;
  float from_y;
  float to_x;
  float to_y;
  uint64_t anim_start_ms;
  uint32_t anim_dur_ms;
  uint8_t dir;
  uint8_t step_phase;
  int dead;
  uint64_t dead_since_ms;
};

struct shot_state {
  int active;
  uint8_t x;
  uint8_t y;
  uint8_t dir;
};

struct fx_state {
  int active;
  uint8_t type;
  float gx;
  float gy;
  uint64_t start_ms;
  uint32_t dur_ms;
  uint8_t color_idx;
};

struct game_state {
  uint8_t brick_bits[48];
  uint8_t bricks[MAZE_W * MAZE_H];
  struct player_state players[MAX_PLAYERS];
  struct player_anim panim[MAX_PLAYERS];
  struct shot_state shots[MAX_PLAYERS];
  struct fx_state fx[FX_MAX];
  int fx_cursor;
  uint8_t zombie_mask;
  /* Slots a client actually holds. The zombie mask only names AI-driven
     slots, so without this an empty seat looks like a silent human. */
  uint8_t seat_mask;
  uint8_t names[MAX_PLAYERS][NAME_LEN];
  int local_pid;
  int have_snapshot;
  int have_welcome;
  uint8_t round_id;
  uint8_t round_phase;
  uint8_t kill_limit;
  uint8_t winner_pid;
  int round_authorized;
  int round_map_ready;
  int round_snapshot_ready;
};

struct input_state {
  int up;
  int down;
  int left;
  int right;
  int fire;
  int preferred_axis;
};

struct layout {
  int scale;
  int text_scale;
  int cell_w;
  int cell_h;
  int board_x;
  int board_y;
  int board_w;
  int board_h;
  int hud_y;
  int line_h;
  int win_w;
  int win_h;
};

struct theme {
  Uint32 black;
  Uint32 white;
  Uint32 border_blue;
  Uint32 border_gold;
  Uint32 brick_dark;
  Uint32 brick_light;
  Uint32 brick_flash;
  Uint32 text_green;
  Uint32 text_blue;
  Uint32 text_gold;
  Uint32 text_brown;
  Uint32 bullet_blue;
  Uint32 bullet_gold;
  Uint32 fx_orange;
  Uint32 fx_red;
  Uint32 fx_cyan;
  Uint32 player_colors[MAX_PLAYERS];
};

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int round_is_newer(uint8_t candidate, uint8_t current) {
  uint8_t distance = (uint8_t)(candidate - current);
  return distance != 0 && distance < 0x80;
}

static int game_ready(const struct game_state *g) {
  return g->have_welcome && g->round_phase == ROUND_PLAYING &&
         g->round_authorized && g->round_map_ready &&
         g->round_snapshot_ready;
}

static int iabs_i(int v) {
  return (v < 0) ? -v : v;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

static uint8_t pack_joy(uint8_t stick, uint8_t trig) {
  uint8_t joy = (uint8_t)(stick & 0x0F);
  if (trig) {
    joy |= 0x10;
  }
  return joy;
}

static int stick_to_dir(uint8_t stick, uint8_t *dir_out) {
  switch (stick & 0x0F) {
    case 0x07:
      *dir_out = 0;
      return 1;
    case 0x0D:
      *dir_out = 1;
      return 1;
    case 0x0B:
      *dir_out = 2;
      return 1;
    case 0x0E:
      *dir_out = 3;
      return 1;
    default:
      return 0;
  }
}

static void layout_init(struct layout *l, int scale) {
  if (scale < 2) {
    scale = 2;
  }
  if (scale > 6) {
    scale = 6;
  }
  l->scale = scale;
  l->text_scale = (scale > 2) ? (scale - 1) : 2;
  l->cell_w = 12 * l->scale;
  l->cell_h = 8 * l->scale;
  l->board_x = 6 * l->scale;
  l->board_y = 6 * l->scale;
  l->board_w = MAZE_W * l->cell_w;
  l->board_h = MAZE_H * l->cell_h;
  l->hud_y = l->board_y + l->board_h + (6 * l->scale);
  l->line_h = 8 * l->text_scale;
  l->win_w = l->board_w + (l->board_x * 2);
  l->win_h = l->hud_y + (l->line_h * 4) + (8 * l->scale);
}

static Uint32 map_rgb(SDL_Surface *screen, uint8_t r, uint8_t g, uint8_t b) {
  return SDL_MapRGB(screen->format, r, g, b);
}

static void theme_init(SDL_Surface *screen, struct theme *t) {
  t->black = map_rgb(screen, 0, 0, 0);
  t->white = map_rgb(screen, 230, 230, 230);
  t->border_blue = map_rgb(screen, 62, 94, 215);
  t->border_gold = map_rgb(screen, 171, 115, 14);
  t->brick_dark = map_rgb(screen, 118, 72, 8);
  t->brick_light = map_rgb(screen, 162, 148, 43);
  t->brick_flash = map_rgb(screen, 204, 184, 126);
  t->text_green = map_rgb(screen, 62, 190, 106);
  t->text_blue = map_rgb(screen, 80, 86, 230);
  t->text_gold = map_rgb(screen, 160, 148, 43);
  t->text_brown = map_rgb(screen, 122, 74, 8);
  t->bullet_blue = map_rgb(screen, 72, 104, 245);
  t->bullet_gold = map_rgb(screen, 184, 124, 14);
  t->fx_orange = map_rgb(screen, 220, 128, 20);
  t->fx_red = map_rgb(screen, 208, 64, 36);
  t->fx_cyan = map_rgb(screen, 80, 222, 210);
  t->player_colors[0] = t->text_green;
  t->player_colors[1] = t->text_blue;
  t->player_colors[2] = t->text_gold;
  t->player_colors[3] = t->text_brown;
}

static void fill_rect(SDL_Surface *screen, int x, int y, int w, int h, Uint32 color) {
  SDL_Rect r;
  if (w <= 0 || h <= 0) {
    return;
  }
  r.x = (Sint16)x;
  r.y = (Sint16)y;
  r.w = (Uint16)w;
  r.h = (Uint16)h;
  SDL_FillRect(screen, &r, color);
}

/* A slot with neither a client nor a zombie in it is not in the game: it gets
   no HUD line and no sprite on the board. Our own slot always counts, so the
   view is right before the first SEATS packet arrives. */
static int slot_in_play(const struct game_state *g, int i) {
  return (g->zombie_mask & (1u << i)) || (g->seat_mask & (1u << i)) ||
         i == g->local_pid;
}

/* A slot the server has no name for shows the role label instead. */
static int name_is_set(const uint8_t *name) {
  int i;
  for (i = 0; i < NAME_LEN; i++) {
    if (name[i] != 0 && name[i] != ' ') {
      return 1;
    }
  }
  return 0;
}

static int glyph_for_char(char ch, uint8_t out[7]) {
  int i;
  char c = (char)toupper((unsigned char)ch);
  static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
  for (i = 0; i < 7; i++) {
    out[i] = blank[i];
  }
  switch (c) {
    case 'A': { uint8_t g[7] = {14, 17, 17, 31, 17, 17, 17}; memcpy(out, g, 7); return 1; }
    case 'B': { uint8_t g[7] = {30, 17, 17, 30, 17, 17, 30}; memcpy(out, g, 7); return 1; }
    case 'C': { uint8_t g[7] = {14, 17, 16, 16, 16, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'D': { uint8_t g[7] = {30, 17, 17, 17, 17, 17, 30}; memcpy(out, g, 7); return 1; }
    case 'E': { uint8_t g[7] = {31, 16, 16, 30, 16, 16, 31}; memcpy(out, g, 7); return 1; }
    case 'F': { uint8_t g[7] = {31, 16, 16, 30, 16, 16, 16}; memcpy(out, g, 7); return 1; }
    case 'G': { uint8_t g[7] = {14, 17, 16, 23, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'H': { uint8_t g[7] = {17, 17, 17, 31, 17, 17, 17}; memcpy(out, g, 7); return 1; }
    case 'I': { uint8_t g[7] = {14, 4, 4, 4, 4, 4, 14}; memcpy(out, g, 7); return 1; }
    case 'J': { uint8_t g[7] = {1, 1, 1, 1, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'K': { uint8_t g[7] = {17, 18, 20, 24, 20, 18, 17}; memcpy(out, g, 7); return 1; }
    case 'L': { uint8_t g[7] = {16, 16, 16, 16, 16, 16, 31}; memcpy(out, g, 7); return 1; }
    case 'M': { uint8_t g[7] = {17, 27, 21, 21, 17, 17, 17}; memcpy(out, g, 7); return 1; }
    case 'N': { uint8_t g[7] = {17, 25, 21, 19, 17, 17, 17}; memcpy(out, g, 7); return 1; }
    case 'O': { uint8_t g[7] = {14, 17, 17, 17, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'P': { uint8_t g[7] = {30, 17, 17, 30, 16, 16, 16}; memcpy(out, g, 7); return 1; }
    case 'Q': { uint8_t g[7] = {14, 17, 17, 17, 21, 18, 13}; memcpy(out, g, 7); return 1; }
    case 'R': { uint8_t g[7] = {30, 17, 17, 30, 20, 18, 17}; memcpy(out, g, 7); return 1; }
    case 'S': { uint8_t g[7] = {14, 17, 16, 14, 1, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'T': { uint8_t g[7] = {31, 4, 4, 4, 4, 4, 4}; memcpy(out, g, 7); return 1; }
    case 'U': { uint8_t g[7] = {17, 17, 17, 17, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case 'V': { uint8_t g[7] = {17, 17, 17, 17, 17, 10, 4}; memcpy(out, g, 7); return 1; }
    case 'W': { uint8_t g[7] = {17, 17, 17, 21, 21, 21, 10}; memcpy(out, g, 7); return 1; }
    case 'X': { uint8_t g[7] = {17, 17, 10, 4, 10, 17, 17}; memcpy(out, g, 7); return 1; }
    case 'Y': { uint8_t g[7] = {17, 17, 10, 4, 4, 4, 4}; memcpy(out, g, 7); return 1; }
    case 'Z': { uint8_t g[7] = {31, 1, 2, 4, 8, 16, 31}; memcpy(out, g, 7); return 1; }
    case '0': { uint8_t g[7] = {14, 17, 19, 21, 25, 17, 14}; memcpy(out, g, 7); return 1; }
    case '1': { uint8_t g[7] = {4, 12, 4, 4, 4, 4, 14}; memcpy(out, g, 7); return 1; }
    case '2': { uint8_t g[7] = {14, 17, 1, 2, 4, 8, 31}; memcpy(out, g, 7); return 1; }
    case '3': { uint8_t g[7] = {30, 1, 1, 14, 1, 1, 30}; memcpy(out, g, 7); return 1; }
    case '4': { uint8_t g[7] = {2, 6, 10, 18, 31, 2, 2}; memcpy(out, g, 7); return 1; }
    case '5': { uint8_t g[7] = {31, 16, 16, 30, 1, 1, 30}; memcpy(out, g, 7); return 1; }
    case '6': { uint8_t g[7] = {14, 16, 16, 30, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case '7': { uint8_t g[7] = {31, 1, 2, 4, 8, 8, 8}; memcpy(out, g, 7); return 1; }
    case '8': { uint8_t g[7] = {14, 17, 17, 14, 17, 17, 14}; memcpy(out, g, 7); return 1; }
    case '9': { uint8_t g[7] = {14, 17, 17, 15, 1, 1, 14}; memcpy(out, g, 7); return 1; }
    case ':': { uint8_t g[7] = {0, 4, 4, 0, 4, 4, 0}; memcpy(out, g, 7); return 1; }
    case '-': { uint8_t g[7] = {0, 0, 0, 14, 0, 0, 0}; memcpy(out, g, 7); return 1; }
    case '.': { uint8_t g[7] = {0, 0, 0, 0, 0, 12, 12}; memcpy(out, g, 7); return 1; }
    case '\'': { uint8_t g[7] = {12, 12, 8, 0, 0, 0, 0}; memcpy(out, g, 7); return 1; }
    case '/': { uint8_t g[7] = {1, 2, 4, 8, 16, 0, 0}; memcpy(out, g, 7); return 1; }
    case ' ': return 1;
    default:
      return 0;
  }
}

static void draw_char(SDL_Surface *screen, int x, int y, char c, int scale, Uint32 color) {
  uint8_t rows[7];
  int ry;
  if (scale <= 0) {
    scale = 1;
  }
  if (!glyph_for_char(c, rows)) {
    return;
  }
  for (ry = 0; ry < 7; ry++) {
    int rx;
    for (rx = 0; rx < 5; rx++) {
      if ((rows[ry] >> (4 - rx)) & 1) {
        fill_rect(screen, x + (rx * scale), y + (ry * scale), scale, scale, color);
      }
    }
  }
}

static void draw_text(SDL_Surface *screen, int x, int y, const char *text, int scale, Uint32 color) {
  int i;
  int cx = x;
  int cy = y;
  if (!text) {
    return;
  }
  for (i = 0; text[i]; i++) {
    if (text[i] == '\n') {
      cy += 8 * scale;
      cx = x;
      continue;
    }
    draw_char(screen, cx, cy, text[i], scale, color);
    cx += 6 * scale;
  }
}

static void clear_screen(SDL_Surface *screen, const struct theme *theme) {
  fill_rect(screen, 0, 0, screen->w, screen->h, theme->black);
}

static void draw_checker_tile(SDL_Surface *screen, int x, int y, int w, int h,
                              Uint32 c0, Uint32 c1, int cell_px) {
  int yy;
  if (cell_px < 1) {
    cell_px = 1;
  }
  for (yy = 0; yy < h; yy += cell_px) {
    int xx;
    for (xx = 0; xx < w; xx += cell_px) {
      int v = ((xx / cell_px) + (yy / cell_px)) & 1;
      fill_rect(screen, x + xx, y + yy, cell_px, cell_px, v ? c1 : c0);
    }
  }
}

static void draw_brick_tile(SDL_Surface *screen, int x, int y, int w, int h,
                            const struct theme *theme, int scale, int flash) {
  int row_h;
  int mortar;
  int by;
  fill_rect(screen, x, y, w, h, flash ? theme->brick_flash : theme->brick_light);
  row_h = h / 4;
  if (row_h < 3) {
    row_h = 3;
  }
  mortar = scale / 2;
  if (mortar < 1) {
    mortar = 1;
  }
  for (by = 0; by < h; by += row_h) {
    int bx;
    int offset = ((by / row_h) & 1) ? (w / 4) : 0;
    fill_rect(screen, x, y + by, w, mortar, theme->brick_dark);
    for (bx = offset; bx < w; bx += (w / 2)) {
      fill_rect(screen, x + bx, y + by, mortar, row_h, theme->brick_dark);
    }
  }
}

static void interp_player(const struct player_anim *p, uint64_t now, float *x, float *y) {
  if (p->anim_dur_ms == 0 || now >= p->anim_start_ms + p->anim_dur_ms) {
    *x = p->to_x;
    *y = p->to_y;
    return;
  }
  {
    float t = (float)(now - p->anim_start_ms) / (float)p->anim_dur_ms;
    t = clampf(t, 0.0f, 1.0f);
    *x = p->from_x + ((p->to_x - p->from_x) * t);
    *y = p->from_y + ((p->to_y - p->from_y) * t);
  }
}

static void fx_emit(struct game_state *g, uint8_t type, float gx, float gy,
                    uint32_t dur_ms, uint8_t color_idx, uint64_t now) {
  struct fx_state *f = &g->fx[g->fx_cursor % FX_MAX];
  f->active = 1;
  f->type = type;
  f->gx = gx;
  f->gy = gy;
  f->start_ms = now;
  f->dur_ms = dur_ms;
  f->color_idx = color_idx;
  g->fx_cursor++;
}

static Uint32 fx_color(const struct theme *theme, uint8_t color_idx) {
  switch (color_idx) {
    case 0: return theme->fx_orange;
    case 1: return theme->fx_red;
    case 2: return theme->fx_cyan;
    case 3: return theme->bullet_blue;
    default: return theme->fx_orange;
  }
}

static uint8_t mirror_bits8(uint8_t v) {
  v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
  v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
  v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
  return v;
}

static void draw_bitmap8(SDL_Surface *screen, int x, int y, int pix,
                         const uint8_t rows[8], Uint32 fg, Uint32 alt) {
  int r;
  if (pix < 1) {
    pix = 1;
  }
  for (r = 0; r < 8; r++) {
    int c;
    uint8_t bits = rows[r];
    for (c = 0; c < 8; c++) {
      if ((bits >> (7 - c)) & 1u) {
        Uint32 col = (r <= 1) ? alt : fg;
        fill_rect(screen, x + (c * pix), y + (r * pix), pix, pix, col);
      }
    }
  }
}

static void select_player_rows(uint8_t dir, uint8_t step, uint8_t out_rows[8]) {
  static const uint8_t right_rows[2][8] = {
    {0x10, 0x38, 0x10, 0x7C, 0x18, 0x18, 0x14, 0x00},
    {0x10, 0x38, 0x10, 0x7E, 0x18, 0x14, 0x08, 0x10}
  };
  static const uint8_t down_rows[2][8] = {
    {0x10, 0x38, 0x10, 0x3C, 0x18, 0x3C, 0x24, 0x00},
    {0x10, 0x38, 0x10, 0x3C, 0x18, 0x24, 0x18, 0x24}
  };
  static const uint8_t up_rows[2][8] = {
    {0x10, 0x10, 0x38, 0x3C, 0x18, 0x18, 0x24, 0x00},
    {0x10, 0x10, 0x38, 0x3C, 0x18, 0x24, 0x18, 0x24}
  };
  const uint8_t *src;
  int i;
  int phase = (step & 1u) ? 1 : 0;

  if ((dir & 0x03u) == 1u) {
    src = down_rows[phase];
  } else if ((dir & 0x03u) == 3u) {
    src = up_rows[phase];
  } else {
    src = right_rows[phase];
  }

  for (i = 0; i < 8; i++) {
    out_rows[i] = src[i];
  }

  if ((dir & 0x03u) == 2u) {
    for (i = 0; i < 8; i++) {
      out_rows[i] = mirror_bits8(out_rows[i]);
    }
  }
}

static void draw_player_sprite(SDL_Surface *screen, int px, int py, int cw, int ch,
                               uint8_t dir, uint8_t step, Uint32 body,
                               int blink, const struct theme *theme) {
  int pix = ch / 10;
  int sw;
  int sh;
  int ox;
  int oy;
  uint8_t rows[8];
  Uint32 muzzle0 = blink ? theme->bullet_blue : theme->bullet_gold;
  Uint32 muzzle1 = blink ? theme->bullet_gold : theme->bullet_blue;
  if (pix < 1) {
    pix = 1;
  }

  sw = 8 * pix;
  sh = 8 * pix;
  ox = px + ((cw - sw) / 2);
  oy = py + ((ch - sh) / 2);

  select_player_rows(dir, step, rows);
  draw_bitmap8(screen, ox, oy, pix, rows, body, theme->text_brown);

  if ((dir & 0x03u) == 0u) {
    fill_rect(screen, ox + sw, oy + (3 * pix), 2 * pix, pix, muzzle0);
    fill_rect(screen, ox + sw + (2 * pix), oy + (3 * pix), pix, pix, muzzle1);
  } else if ((dir & 0x03u) == 2u) {
    fill_rect(screen, ox - (2 * pix), oy + (3 * pix), 2 * pix, pix, muzzle0);
    fill_rect(screen, ox - (3 * pix), oy + (3 * pix), pix, pix, muzzle1);
  } else if ((dir & 0x03u) == 1u) {
    fill_rect(screen, ox + (3 * pix), oy + sh, pix, 2 * pix, muzzle0);
    fill_rect(screen, ox + (3 * pix), oy + sh + (2 * pix), pix, pix, muzzle1);
  } else {
    fill_rect(screen, ox + (3 * pix), oy - (2 * pix), pix, 2 * pix, muzzle0);
    fill_rect(screen, ox + (3 * pix), oy - (3 * pix), pix, pix, muzzle1);
  }
}

static void draw_shot_sprite(SDL_Surface *screen, int px, int py, int cw, int ch,
                             uint8_t dir, int blink, const struct theme *theme) {
  Uint32 c0 = blink ? theme->bullet_blue : theme->bullet_gold;
  Uint32 c1 = blink ? theme->bullet_gold : theme->bullet_blue;
  int u = ch / 8;
  int cx;
  int cy;
  if (u < 1) {
    u = 1;
  }
  cx = px + (cw / 2);
  cy = py + (ch / 2);

  if (dir == 0) {
    fill_rect(screen, cx - u, cy - u, 3 * u, u, c0);
    fill_rect(screen, cx + (2 * u), cy - u, u, u, c1);
  } else if (dir == 2) {
    fill_rect(screen, cx - (2 * u), cy - u, 3 * u, u, c0);
    fill_rect(screen, cx - (2 * u), cy - u, u, u, c1);
  } else if (dir == 1) {
    fill_rect(screen, cx - u, cy - u, u, 3 * u, c0);
    fill_rect(screen, cx - u, cy + (2 * u), u, u, c1);
  } else {
    fill_rect(screen, cx - u, cy - (2 * u), u, 3 * u, c0);
    fill_rect(screen, cx - u, cy - (2 * u), u, u, c1);
  }
}

static void draw_fx(SDL_Surface *screen, const struct layout *l,
                    const struct theme *theme, struct game_state *g, uint64_t now) {
  int i;
  for (i = 0; i < FX_MAX; i++) {
    struct fx_state *f = &g->fx[i];
    int cell_x;
    int cell_y;
    int cx;
    int cy;
    int u;
    float t;
    if (!f->active || f->dur_ms == 0) {
      continue;
    }
    if (now >= f->start_ms + f->dur_ms) {
      f->active = 0;
      continue;
    }
    t = (float)(now - f->start_ms) / (float)f->dur_ms;
    t = clampf(t, 0.0f, 1.0f);
    cell_x = l->board_x + (int)(f->gx * (float)l->cell_w);
    cell_y = l->board_y + (int)(f->gy * (float)l->cell_h);
    cx = cell_x + (l->cell_w / 2);
    cy = cell_y + (l->cell_h / 2);
    u = l->cell_h / 8;
    if (u < 1) {
      u = 1;
    }

    if (f->type == FX_BRICK) {
      int burst = (int)((float)(l->cell_w / 2) * t);
      Uint32 c = fx_color(theme, f->color_idx);
      fill_rect(screen, cell_x, cell_y, l->cell_w, l->cell_h,
                (t < 0.35f) ? theme->brick_flash : theme->black);
      fill_rect(screen, cx - burst, cy - u, (burst * 2) + u, u, c);
      fill_rect(screen, cx - u, cy - burst, u, (burst * 2) + u, c);
    } else if (f->type == FX_PLAYER_EXPLODE) {
      int radius = (int)((float)(l->cell_w / 2) * t);
      Uint32 c = fx_color(theme, 1);
      fill_rect(screen, cx - radius, cy - u, (radius * 2) + u, u, c);
      fill_rect(screen, cx - u, cy - radius, u, (radius * 2) + u, c);
      fill_rect(screen, cx - radius, cy - radius, u, u, theme->fx_orange);
      fill_rect(screen, cx + radius, cy - radius, u, u, theme->fx_orange);
      fill_rect(screen, cx - radius, cy + radius, u, u, theme->fx_orange);
      fill_rect(screen, cx + radius, cy + radius, u, u, theme->fx_orange);
    } else if (f->type == FX_PLAYER_SPAWN) {
      int radius = (int)((float)(l->cell_w / 2) * (1.0f - t));
      Uint32 c = fx_color(theme, 2);
      fill_rect(screen, cx - radius, cy - u, (radius * 2) + u, u, c);
      fill_rect(screen, cx - u, cy - radius, u, (radius * 2) + u, c);
      fill_rect(screen, cx - u, cy - u, 2 * u, 2 * u, theme->white);
    } else if (f->type == FX_SHOT_SPARK) {
      int radius = (int)((float)(l->cell_h / 2) * t);
      Uint32 c = fx_color(theme, 3);
      fill_rect(screen, cx - radius, cy - u, (radius * 2) + u, u, c);
      fill_rect(screen, cx - u, cy - radius, u, (radius * 2) + u, c);
    }
  }
}

static void render_game(SDL_Surface *screen, const struct layout *l,
                        const struct theme *theme, struct game_state *g,
                        const char *host, int port, uint64_t now) {
  int x;
  int y;
  int i;
  int blink = ((now / 80ULL) & 1ULL) ? 1 : 0;
  char line[96];

  clear_screen(screen, theme);

  for (y = 0; y < MAZE_H; y++) {
    for (x = 0; x < MAZE_W; x++) {
      int idx = y * MAZE_W + x;
      int px = l->board_x + (x * l->cell_w);
      int py = l->board_y + (y * l->cell_h);
      int outer = (x == 0 || x == (MAZE_W - 1) || y == 0 || y == (MAZE_H - 1));
      if (g->bricks[idx]) {
        if (outer) {
          int cell_px = l->scale;
          if (cell_px < 2) {
            cell_px = 2;
          }
          draw_checker_tile(screen, px, py, l->cell_w, l->cell_h,
                            theme->border_blue, theme->border_gold, cell_px);
        } else {
          draw_brick_tile(screen, px, py, l->cell_w, l->cell_h, theme, l->scale, 0);
        }
      }
    }
  }

  for (i = 0; i < MAX_PLAYERS; i++) {
    if (g->shots[i].active) {
      int px = l->board_x + (g->shots[i].x * l->cell_w);
      int py = l->board_y + (g->shots[i].y * l->cell_h);
      draw_shot_sprite(screen, px, py, l->cell_w, l->cell_h,
                       g->shots[i].dir, blink, theme);
    }
  }

  for (i = 0; i < MAX_PLAYERS; i++) {
    float gx;
    float gy;
    int px;
    int py;
    if (!g->panim[i].known || g->panim[i].dead || !slot_in_play(g, i)) {
      continue;
    }
    interp_player(&g->panim[i], now, &gx, &gy);
    px = l->board_x + (int)(gx * (float)l->cell_w + 0.5f);
    py = l->board_y + (int)(gy * (float)l->cell_h + 0.5f);
    draw_player_sprite(screen, px, py, l->cell_w, l->cell_h,
                       g->panim[i].dir, g->panim[i].step_phase,
                       theme->player_colors[i], blink, theme);
  }

  draw_fx(screen, l, theme, g, now);

  for (i = 0; i < MAX_PLAYERS; i++) {
    int row_y = l->hud_y + (i * l->line_h);
    int role_x = l->board_x + (4 * l->text_scale);
    int score_x = l->board_x + (l->board_w - (20 * l->text_scale));
    char label[NAME_LEN + 1];
    const char *role;
    Uint32 color = theme->player_colors[i];
    if (!slot_in_play(g, i)) {
      continue; /* nobody in this seat: no name and no score */
    }
    if (g->zombie_mask & (1u << i)) {
      role = "ZOMBIE";
    } else if (name_is_set(g->names[i])) {
      memcpy(label, g->names[i], NAME_LEN);
      label[NAME_LEN] = '\0';
      {
        int t;
        for (t = NAME_LEN - 1; t >= 0 && label[t] == ' '; t--) {
          label[t] = '\0';
        }
      }
      role = label;
    } else {
      role = "WIZARD";
    }
    draw_text(screen, role_x, row_y, role, l->text_scale, color);
    snprintf(line, sizeof(line), "%3u", g->players[i].score);
    draw_text(screen, score_x, row_y, line, l->text_scale, color);
  }

  snprintf(line, sizeof(line), "HOST: %s:%d", host, port);
  draw_text(screen, l->board_x, l->win_h - (7 * l->text_scale), line,
            l->text_scale - 1, theme->text_gold);

  if (g->local_pid >= 0 && g->local_pid < MAX_PLAYERS) {
    snprintf(line, sizeof(line), "YOU: %d", g->local_pid);
    draw_text(screen, l->win_w - (26 * l->text_scale), l->win_h - (7 * l->text_scale),
              line, l->text_scale - 1, theme->player_colors[g->local_pid]);
  } else {
    draw_text(screen, l->win_w - (35 * l->text_scale), l->win_h - (7 * l->text_scale),
              "WAITING FOR SLOT", l->text_scale - 1, theme->text_blue);
  }

  if (g->round_phase == ROUND_OVER && g->have_welcome) {
    int box_w = l->board_w / 2;
    int box_h = 34 * l->scale;
    int box_x = l->board_x + (l->board_w - box_w) / 2;
    int box_y = l->board_y + (l->board_h - box_h) / 2;
    fill_rect(screen, box_x - 2 * l->scale, box_y - 2 * l->scale,
              box_w + 4 * l->scale, box_h + 4 * l->scale,
              theme->border_blue);
    fill_rect(screen, box_x, box_y, box_w, box_h, theme->black);
    draw_text(screen, box_x + 6 * l->scale, box_y + 5 * l->scale,
              "ROUND OVER", l->text_scale + 1, theme->text_gold);
    snprintf(line, sizeof(line), "PLAYER %u WINS",
             (unsigned)(g->winner_pid + 1));
    draw_text(screen, box_x + 6 * l->scale, box_y + 17 * l->scale,
              line, l->text_scale, theme->white);
    snprintf(line, sizeof(line), "FIRST TO %u - NEXT ROUND SOON",
             (unsigned)g->kill_limit);
    draw_text(screen, box_x + 6 * l->scale, box_y + 26 * l->scale,
              line, l->text_scale - 1, theme->text_blue);
  } else if (g->have_welcome && !game_ready(g)) {
    snprintf(line, sizeof(line), "SYNCING ROUND %u", (unsigned)g->round_id);
    draw_text(screen, l->board_x + 4 * l->scale,
              l->board_y + 4 * l->scale, line, l->text_scale,
              theme->white);
  }
}

static void render_prompt(SDL_Surface *screen, const struct layout *l,
                          const struct theme *theme, const char *host,
                          const char *name, int field, const char *msg,
                          uint64_t now) {
  int box_w = l->board_w - (8 * l->scale);
  int box_h = l->board_h / 3;
  int box_x = l->board_x + (4 * l->scale);
  int box_y = l->board_y + (l->board_h / 3);
  int blink = ((now / 450ULL) & 1ULL) ? 1 : 0;
  char host_line[HOST_MAX + 8];
  char name_line[NAME_LEN + 10];

  clear_screen(screen, theme);
  draw_checker_tile(screen, l->board_x, l->board_y, l->board_w, l->board_h,
                    theme->black, theme->black, l->cell_h);

  fill_rect(screen, box_x - (2 * l->scale), box_y - (2 * l->scale),
            box_w + (4 * l->scale), box_h + (4 * l->scale), theme->border_blue);
  fill_rect(screen, box_x, box_y, box_w, box_h, theme->black);

  draw_text(screen, box_x + (6 * l->scale), box_y + (4 * l->scale),
            "'MAZE WAR'", l->text_scale + 1, theme->text_gold);
  draw_text(screen, box_x + (6 * l->scale), box_y + (14 * l->scale),
            "ENTER HOSTNAME", l->text_scale, theme->text_green);

  snprintf(host_line, sizeof(host_line), "HOST: %s%s", host,
           (field == 0 && blink) ? "_" : " ");
  draw_text(screen, box_x + (6 * l->scale), box_y + (24 * l->scale),
            host_line, l->text_scale,
            (field == 0) ? theme->white : theme->text_blue);

  snprintf(name_line, sizeof(name_line), "NAME: %s%s", name,
           (field == 1 && blink) ? "_" : " ");
  draw_text(screen, box_x + (6 * l->scale), box_y + (31 * l->scale),
            name_line, l->text_scale,
            (field == 1) ? theme->white : theme->text_blue);

  draw_text(screen, box_x + (6 * l->scale), box_y + (39 * l->scale),
            (field == 0) ? "ENTER FOR NAME" : "ENTER TO CONNECT",
            l->text_scale - 1, theme->text_blue);
  draw_text(screen, box_x + (6 * l->scale), box_y + (45 * l->scale),
            "ESC TO QUIT", l->text_scale - 1, theme->text_blue);

  if (msg && msg[0]) {
    draw_text(screen, box_x + (6 * l->scale), box_y + box_h - (8 * l->scale),
              msg, l->text_scale - 1, theme->fx_red);
  }
}

static int host_prompt_loop(SDL_Surface *screen, const struct layout *l,
                            const struct theme *theme, char *host,
                            size_t host_len, char *name, size_t name_len,
                            const char *msg) {
  int done = 0;
  int accepted = 0;
  int field = 0; /* 0 = host, 1 = name */
  uint64_t next_frame = now_ms();
  while (!done) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        return 0;
      }
      if (ev.type == SDL_KEYDOWN) {
        SDLKey key = ev.key.keysym.sym;
        uint16_t uni = ev.key.keysym.unicode;
        if (key == SDLK_ESCAPE) {
          return 0;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
          if (field == 0) {
            if (host[0] != '\0') {
              field = 1; /* the name may be left empty */
            }
          } else {
            accepted = 1;
            done = 1;
          }
          continue;
        }
        if (key == SDLK_TAB) {
          field = field ? 0 : 1;
          continue;
        }
        {
          char *buf = (field == 0) ? host : name;
          size_t cap = (field == 0) ? host_len : name_len;
          if (key == SDLK_BACKSPACE) {
            size_t n = strlen(buf);
            if (n > 0) {
              buf[n - 1] = '\0';
            }
            continue;
          }
          if (uni >= 32 && uni < 127) {
            char ch = (char)uni;
            size_t n = strlen(buf);
            if (isalnum((unsigned char)ch) || ch == '.' || ch == '-') {
              if (n + 1 < cap) {
                buf[n] = ch;
                buf[n + 1] = '\0';
              }
            }
          }
        }
      }
    }

    if (now_ms() >= next_frame) {
      render_prompt(screen, l, theme, host, name, field, msg, now_ms());
      SDL_Flip(screen);
      next_frame = now_ms() + 16;
    }
    SDL_Delay(1);
  }
  return accepted;
}

static int resolve_host(const char *host, int port, struct sockaddr_in *out_addr) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  struct addrinfo *it;
  char port_str[16];
  int rc;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, sizeof(port_str), "%d", port);

  rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0 || !res) {
    return -1;
  }

  for (it = res; it; it = it->ai_next) {
    if (it->ai_family == AF_INET && it->ai_addrlen >= sizeof(struct sockaddr_in)) {
      memcpy(out_addr, it->ai_addr, sizeof(struct sockaddr_in));
      freeaddrinfo(res);
      return 0;
    }
  }

  freeaddrinfo(res);
  return -1;
}

static int open_tcp_socket(const struct sockaddr_in *addr) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return -1;
  if (connect(sock, (const struct sockaddr *)addr, sizeof(*addr)) < 0 ||
      tcp_configure(sock) < 0) {
    close(sock);
    return -1;
  }
  return sock;
}

static void decode_brick_full(struct game_state *g, const uint8_t *bits) {
  int i;
  memcpy(g->brick_bits, bits, 48);
  for (i = 0; i < MAZE_W * MAZE_H; i++) {
    g->bricks[i] = (uint8_t)((bits[i / 8] >> (i % 8)) & 1u);
  }
}

static void clear_brick_cell(struct game_state *g, int x, int y) {
  int idx;
  if (x < 0 || x >= MAZE_W || y < 0 || y >= MAZE_H) {
    return;
  }
  idx = y * MAZE_W + x;
  g->bricks[idx] = 0;
  g->brick_bits[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static void apply_player_snapshot(struct game_state *g, int pid,
                                  uint8_t x, uint8_t y, uint8_t joy,
                                  uint64_t now) {
  struct player_anim *pa;
  uint8_t new_dir;
  if (pid < 0 || pid >= MAX_PLAYERS) {
    return;
  }
  if (x >= MAZE_W || y >= MAZE_H) {
    return;
  }

  pa = &g->panim[pid];
  if (!pa->known) {
    pa->known = 1;
    pa->from_x = (float)x;
    pa->from_y = (float)y;
    pa->to_x = (float)x;
    pa->to_y = (float)y;
    pa->anim_start_ms = now;
    pa->anim_dur_ms = 0;
  } else {
    int ox = (int)(pa->to_x + 0.5f);
    int oy = (int)(pa->to_y + 0.5f);
    int dx = (int)x - ox;
    int dy = (int)y - oy;
    int manhattan = iabs_i(dx) + iabs_i(dy);

    if (pa->dead) {
      pa->from_x = (float)x;
      pa->from_y = (float)y;
      pa->to_x = (float)x;
      pa->to_y = (float)y;
      if (now > pa->dead_since_ms + 2500) {
        pa->dead = 0;
      }
    } else if (manhattan == 1) {
      float cx;
      float cy;
      interp_player(pa, now, &cx, &cy);
      pa->from_x = cx;
      pa->from_y = cy;
      pa->to_x = (float)x;
      pa->to_y = (float)y;
      pa->anim_start_ms = now;
      pa->anim_dur_ms = 100;
      pa->step_phase ^= 1;
      if (dx > 0) {
        pa->dir = 0;
      } else if (dx < 0) {
        pa->dir = 2;
      } else if (dy > 0) {
        pa->dir = 1;
      } else if (dy < 0) {
        pa->dir = 3;
      }
    } else if (manhattan > 0) {
      pa->from_x = (float)x;
      pa->from_y = (float)y;
      pa->to_x = (float)x;
      pa->to_y = (float)y;
      pa->anim_start_ms = now;
      pa->anim_dur_ms = 0;
    }
  }

  if (stick_to_dir((uint8_t)(joy & 0x0F), &new_dir)) {
    pa->dir = new_dir;
  }
}

static void send_reliable_ack(int sock, struct tcp_tx *tx, uint8_t *seq,
                              uint16_t rev) {
  uint8_t pkt[4];
  pkt[0] = PKT_RELIABLE_ACK;
  pkt[1] = (*seq)++;
  pkt[2] = (uint8_t)rev;
  pkt[3] = (uint8_t)(rev >> 8);
  tcp_tx_queue_frame(sock, tx, pkt, sizeof(pkt));
}

static int reliable_inner_is_valid(const uint8_t *buf, ssize_t n) {
  if (n == 5 && buf[0] == PKT_BRICK_DELTA) {
    return 1;
  }
  if (n == 7 && buf[0] == PKT_RESPAWN) {
    return 1;
  }
  if (n == 3 + NAME_LEN && buf[0] == PKT_NAME) {
    return 1;
  }
  if (n == 52 && buf[0] == PKT_BRICK_FULL) return 1;
  if (n == 43 && buf[0] == PKT_MATCH_END) return 1;
  if (n == 3 && buf[0] == PKT_ROUND_START) return 1;
  return 0;
}

static void handle_packet(struct game_state *g, const uint8_t *buf, ssize_t n,
                          uint64_t now, int debug, int sock,
                          struct tcp_tx *tx, uint8_t *seq,
                          uint16_t *reliable_applied_rev,
                          int from_reliable) {
  if (n == 5 && buf[0] == PKT_WELCOME && buf[1] == PROTOCOL_VERSION) {
    g->have_welcome = 1;
    g->round_id = buf[2];
    g->round_phase = buf[3];
    g->kill_limit = buf[4];
    g->round_authorized = 0;
    g->round_map_ready = 0;
    g->round_snapshot_ready = 0;
    return;
  }
  if (n >= 2 && buf[0] == PKT_REJECT) {
    fprintf(stderr, "server rejected protocol version (requires %u)\n",
            (unsigned)buf[1]);
    return;
  }
  if (!g->have_welcome) return;

  if (n >= 7 && buf[0] == PKT_RELIABLE_EVENT) {
    uint16_t rev = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    const uint8_t *inner = &buf[4];
    ssize_t inner_len = n - 4;
    if (rev != (uint16_t)(*reliable_applied_rev + 1) ||
        !reliable_inner_is_valid(inner, inner_len)) {
      send_reliable_ack(sock, tx, seq, *reliable_applied_rev);
      return;
    }
    *reliable_applied_rev = rev;
    send_reliable_ack(sock, tx, seq, *reliable_applied_rev);
    handle_packet(g, inner, inner_len, now, debug, sock, tx, seq,
                  reliable_applied_rev, 1);
    return;
  }

  if (n == 3 && buf[0] == PKT_ROUND_START) {
    uint8_t incoming = buf[1];
    if (incoming == g->round_id || round_is_newer(incoming, g->round_id)) {
      if (incoming != g->round_id || !g->round_authorized) {
        memset(g->shots, 0, sizeof(g->shots));
        memset(g->panim, 0, sizeof(g->panim));
        memset(g->fx, 0, sizeof(g->fx));
        g->round_map_ready = 0;
        g->round_snapshot_ready = 0;
        g->have_snapshot = 0;
      }
      g->round_id = incoming;
      g->kill_limit = buf[2];
      g->round_phase = ROUND_PLAYING;
      g->round_authorized = 1;
    }
    return;
  }

  if (n == 43 && buf[0] == PKT_MATCH_END) {
    if (buf[1] == g->round_id) {
      g->round_phase = ROUND_OVER;
      g->winner_pid = buf[2];
      g->seat_mask = (uint8_t)(buf[3] & (uint8_t)~buf[4]);
      g->zombie_mask = buf[4];
      g->kill_limit = buf[5];
      for (int i = 0; i < MAX_PLAYERS; i++) g->players[i].score = buf[6 + i];
      memcpy(g->names, &buf[11], MAX_PLAYERS * NAME_LEN);
      memset(g->shots, 0, sizeof(g->shots));
      memset(g->fx, 0, sizeof(g->fx));
    }
    return;
  }

  if (n == 52 && buf[0] == PKT_BRICK_FULL && buf[51] == g->round_id) {
    decode_brick_full(g, &buf[3]);
    if (from_reliable) g->round_map_ready = 1;
    return;
  }

  if (n == 5 && buf[0] == PKT_BRICK_DELTA && buf[4] == g->round_id &&
      g->round_phase == ROUND_PLAYING) {
    int x = buf[2];
    int y = buf[3];
    if (x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H) {
      if (g->bricks[y * MAZE_W + x]) {
        fx_emit(g, FX_BRICK, (float)x, (float)y, 220, 0, now);
      }
      clear_brick_cell(g, x, y);
    }
    return;
  }

  if (n >= 3 && buf[0] == PKT_SEATS) {
    if (g->round_phase != ROUND_OVER)
      g->seat_mask = (uint8_t)(buf[2] & 0x0F);
    return;
  }

  if (n >= 3 + NAME_LEN && buf[0] == PKT_NAME) {
    uint8_t np = buf[2];
    if (np < MAX_PLAYERS && g->round_phase != ROUND_OVER) {
      memcpy(g->names[np], &buf[3], NAME_LEN);
    }
    return;
  }

  if (n == 7 && buf[0] == PKT_SHOT && buf[6] == g->round_id &&
      g->round_phase == ROUND_PLAYING) {
    int pid = buf[2];
    if (pid >= 0 && pid < MAX_PLAYERS) {
      uint8_t flags = buf[5];
      int active = (flags & 0x01) != 0;
      if (active && buf[3] < MAZE_W && buf[4] < MAZE_H) {
        g->shots[pid].active = 1;
        g->shots[pid].x = buf[3];
        g->shots[pid].y = buf[4];
        g->shots[pid].dir = (uint8_t)((flags >> 1) & 0x03);
      } else {
        if (g->shots[pid].active) {
          fx_emit(g, FX_SHOT_SPARK,
                  (float)g->shots[pid].x,
                  (float)g->shots[pid].y,
                  120, 3, now);
        }
        g->shots[pid].active = 0;
      }
    }
    return;
  }

  if (n == 7 && buf[0] == PKT_RESPAWN && buf[6] == g->round_id &&
      g->round_phase == ROUND_PLAYING) {
    int pid = buf[2];
    uint8_t flags = buf[5];
    if (pid >= 0 && pid < MAX_PLAYERS) {
      struct player_anim *pa = &g->panim[pid];
      if (flags & 0x02) {
        if (buf[3] < MAZE_W && buf[4] < MAZE_H) {
          pa->known = 1;
          pa->dead = 0;
          pa->from_x = (float)buf[3];
          pa->from_y = (float)buf[4];
          pa->to_x = (float)buf[3];
          pa->to_y = (float)buf[4];
          pa->anim_start_ms = now;
          pa->anim_dur_ms = 0;
          fx_emit(g, FX_PLAYER_SPAWN, (float)buf[3], (float)buf[4], 420, 2, now);
        }
      } else if (flags & 0x01) {
        float ex = pa->to_x;
        float ey = pa->to_y;
        if (!pa->dead) {
          interp_player(pa, now, &ex, &ey);
          fx_emit(g, FX_PLAYER_EXPLODE, ex, ey, 420, 1, now);
        }
        pa->dead = 1;
        pa->dead_since_ms = now;
      }
    }
    return;
  }

  if (n == 21 && buf[0] == PKT_SNAPSHOT && buf[20] == g->round_id &&
      g->round_phase == ROUND_PLAYING) {
    int pid;
    int ack_valid;
    uint8_t ack_seq = 0;
    g->have_snapshot = 1;
    g->round_snapshot_ready = 1;
    pid = (int)((buf[2] >> 1) & 0x03);
    g->zombie_mask = (uint8_t)((buf[2] >> 3) & 0x0F);
    ack_valid = (buf[2] & 0x80) != 0;
    if (n >= 20) {
      ack_seq = buf[19];
    } else {
      ack_valid = 0;
    }
    if (pid != g->local_pid) {
      g->local_pid = pid;
      if (debug) {
        fprintf(stderr, "local pid=%d\n", g->local_pid);
      }
    }

    g->players[0].x = buf[3];
    g->players[0].y = buf[4];
    g->players[1].x = buf[5];
    g->players[1].y = buf[6];
    g->players[2].x = buf[7];
    g->players[2].y = buf[8];
    g->players[3].x = buf[9];
    g->players[3].y = buf[10];

    g->players[0].joy = buf[11];
    g->players[1].joy = buf[12];
    g->players[2].joy = buf[13];
    g->players[3].joy = buf[14];

    g->players[0].score = buf[15];
    g->players[1].score = buf[16];
    g->players[2].score = buf[17];
    g->players[3].score = buf[18];
    if (debug) {
      fprintf(stderr, "snapshot ack pid=%d ack_valid=%d ack_seq=%u\n", pid,
              ack_valid, (unsigned)ack_seq);
    }

    for (pid = 0; pid < MAX_PLAYERS; pid++) {
      apply_player_snapshot(g, pid, g->players[pid].x, g->players[pid].y,
                            g->players[pid].joy, now);
    }
    return;
  }
}

static uint8_t compute_stick(const struct input_state *in) {
  int h = 0;
  int v = 0;

  if (in->left && !in->right) {
    h = -1;
  } else if (in->right && !in->left) {
    h = 1;
  }

  if (in->up && !in->down) {
    v = -1;
  } else if (in->down && !in->up) {
    v = 1;
  }

  if (h != 0 && v != 0) {
    if (in->preferred_axis == 0) {
      v = 0;
    } else {
      h = 0;
    }
  }

  if (h > 0) {
    return 0x07;
  }
  if (h < 0) {
    return 0x0B;
  }
  if (v > 0) {
    return 0x0D;
  }
  if (v < 0) {
    return 0x0E;
  }
  return 0x0F;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s [--port PORT] [--host HOST] [--pid N] [--name NAME] "
          "[--scale N] [--debug]\n"
          "  --name NAME  display name, up to 8 chars; also promptable at start\n",
          argv0);
}

int main(int argc, char **argv) {
  int port = 9000;
  int debug = 0;
  int opt_pid = -1;
  int scale = 4;
  char host[HOST_MAX + 1] = "127.0.0.1";
  char name[NAME_LEN + 1] = "";
  uint8_t my_name[NAME_LEN];
  uint64_t last_name_send_ms = 0;
  char prompt_msg[128] = "";
  int sock = -1;
  struct tcp_tx tx = {0};
  struct tcp_rx rx = {0};
  signal(SIGPIPE, SIG_IGN);
  struct sockaddr_in server_addr;
  struct layout layout;
  struct theme theme;
  SDL_Surface *screen = NULL;
  struct game_state game;
  struct input_state input;
  uint8_t seq = 0;
  uint8_t last_joy = 0xFF;
  uint16_t reliable_applied_rev = 0;
  uint64_t last_send_ms = 0;
  uint64_t next_frame_ms;
  int running = 1;
  int i;

  memset(&game, 0, sizeof(game));
  memset(&input, 0, sizeof(input));
  input.preferred_axis = 0;
  memset(&server_addr, 0, sizeof(server_addr));

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      strncpy(host, argv[++i], HOST_MAX);
      host[HOST_MAX] = '\0';
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      strncpy(name, argv[++i], NAME_LEN);
      name[NAME_LEN] = '\0';
    } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
      opt_pid = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      scale = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--debug") == 0) {
      debug = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (port <= 0 || port > 65535) {
    fprintf(stderr, "Invalid port\n");
    return 1;
  }
  if (opt_pid < -1 || opt_pid >= MAX_PLAYERS) {
    fprintf(stderr, "Invalid pid\n");
    return 1;
  }

  layout_init(&layout, scale);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  atexit(SDL_Quit);
  SDL_EnableUNICODE(1);

  screen = SDL_SetVideoMode(layout.win_w, layout.win_h, 32, SDL_SWSURFACE);
  if (!screen) {
    fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_WM_SetCaption("Maze War SDL Client", "maze-war-client-sdl");
  theme_init(screen, &theme);

  while (1) {
    if (!host_prompt_loop(screen, &layout, &theme, host, sizeof(host), name,
                          sizeof(name), prompt_msg)) {
      return 0;
    }
    if (resolve_host(host, port, &server_addr) == 0) {
      break;
    }
    snprintf(prompt_msg, sizeof(prompt_msg), "COULD NOT RESOLVE HOST");
  }

  sock = open_tcp_socket(&server_addr);
  if (sock < 0) {
    perror("socket");
    return 1;
  }
  {
    uint8_t hello[2] = {PKT_HELLO, PROTOCOL_VERSION};
    tcp_tx_queue_frame(sock, &tx, hello, sizeof(hello));
  }

  game.local_pid = opt_pid;
  game.zombie_mask = 0;
  memset(game.names, 0, sizeof(game.names));
  game.seat_mask = 0;
  memset(my_name, ' ', sizeof(my_name));
  {
    size_t i;
    for (i = 0; i < NAME_LEN && name[i]; i++) {
      my_name[i] = (uint8_t)name[i];
    }
  }

  if (debug) {
    char ip[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &server_addr.sin_addr, ip, sizeof(ip));
    fprintf(stderr, "connecting to %s:%d\n", ip, port);
  }

  next_frame_ms = now_ms();

  while (running) {
    uint64_t now;
    SDL_Event ev;

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        running = 0;
      } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        int pressed = (ev.type == SDL_KEYDOWN) ? 1 : 0;
        SDLKey key = ev.key.keysym.sym;
        if (key == SDLK_ESCAPE && pressed) {
          running = 0;
        } else if (key == SDLK_UP) {
          input.up = pressed;
          if (pressed) {
            input.preferred_axis = 1;
          }
        } else if (key == SDLK_DOWN) {
          input.down = pressed;
          if (pressed) {
            input.preferred_axis = 1;
          }
        } else if (key == SDLK_LEFT) {
          input.left = pressed;
          if (pressed) {
            input.preferred_axis = 0;
          }
        } else if (key == SDLK_RIGHT) {
          input.right = pressed;
          if (pressed) {
            input.preferred_axis = 0;
          }
        } else if (key == SDLK_SPACE) {
          input.fire = pressed;
        }
      }
    }

    now = now_ms();

    if (tcp_tx_flush(sock, &tx) < 0) break;
    for (int frames = 0; frames < 64; frames++) {
      uint8_t buf[64];
      int n = tcp_recv_frame(sock, &rx, buf, sizeof(buf));
      if (n < 0) { running = 0; break; }
      if (n == 0) break;
      handle_packet(&game, buf, n, now, debug, sock, &tx, &seq,
                    &reliable_applied_rev, 0);
    }
    if (!running) break;

    /* The server repeats names it knows, but it cannot repeat one it never
       received, so keep sending until our own slot comes back named. */
    if (game.have_welcome && name[0] &&
        (now - last_name_send_ms) >= NAME_RESEND_MS) {
      int known = (game.local_pid >= 0) && name_is_set(game.names[game.local_pid]);
      if (!known) {
        uint8_t pkt[3 + NAME_LEN];
        pkt[0] = PKT_NAME;
        pkt[1] = seq++;
        pkt[2] = (uint8_t)((game.local_pid >= 0) ? game.local_pid : 0);
        memcpy(&pkt[3], my_name, NAME_LEN);
        tcp_tx_queue_frame(sock, &tx, pkt, sizeof(pkt));
      }
      last_name_send_ms = now;
    }

    {
      int ready = game_ready(&game);
      uint8_t stick = ready ? compute_stick(&input) : 0x0F;
      uint8_t joy = ready ? pack_joy(stick, (uint8_t)input.fire) : 0x0F;
      if (game.have_welcome && (joy != last_joy ||
          (joy != 0x0F && (now - last_send_ms) >= 100) ||
          (now - last_send_ms) >= 1000)) {
        uint8_t pkt[5];
        int tx_pid = (game.local_pid >= 0) ? game.local_pid : ((opt_pid >= 0) ? opt_pid : 0);
        pkt[0] = PKT_DELTA;
        pkt[1] = seq++;
        pkt[2] = (uint8_t)tx_pid;
        pkt[3] = joy;
        pkt[4] = game.round_id;
        tcp_tx_queue_frame(sock, &tx, pkt, sizeof(pkt));
        last_joy = joy;
        last_send_ms = now;
      }
    }

    if (now >= next_frame_ms) {
      render_game(screen, &layout, &theme, &game, host, port, now);
      SDL_Flip(screen);
      next_frame_ms = now + 16;
    }

    SDL_Delay(1);
  }

  close(sock);
  return 0;
}
