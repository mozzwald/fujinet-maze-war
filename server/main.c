#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
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
  PKT_BRICK_FULL = 0x50,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52
};

enum { MAX_PLAYERS = 4 };
enum { CLIENT_TIMEOUT_MS = 60000 };
enum { INPUT_STALE_MS = 500 };

struct player_state {
  uint8_t x;
  uint8_t y;
  uint8_t joy;
  uint8_t score;
  uint64_t respawn_at_ms;
  uint64_t zombie_next_ms;
};

struct shot_state {
  int active;
  uint8_t x;
  uint8_t y;
  int8_t dx;
  int8_t dy;
  uint8_t clear_burst;
};

struct client_slot {
  int in_use;
  struct sockaddr_in addr;
  socklen_t addr_len;
  uint64_t last_seen_ms;
  int sent_bricks;
  uint8_t rx_need;
  uint8_t rx_idx;
  uint8_t rx_buf[8];
  uint8_t have_delta_seq;
  uint8_t last_delta_seq;
};

static volatile sig_atomic_t g_running = 1;

static void compute_zombie_mask(const struct client_slot *clients, int zombies,
                                uint8_t *out_mask);

static void on_sigint(int sig) {
  (void)sig;
  g_running = 0;
}

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void log_client_event(const char *event, int slot,
                             const struct sockaddr_in *addr) {
  char ip[INET_ADDRSTRLEN];
  const char *ip_s = inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
  if (!ip_s) {
    ip_s = "?.?.?.?";
  }
  printf("client %s slot=%d addr=%s:%u\n", event, slot, ip_s,
         (unsigned)ntohs(addr->sin_port));
}

static int addr_equal(const struct sockaddr_in *a,
                      const struct sockaddr_in *b) {
  return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
         a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static int find_or_add_client(struct client_slot *clients,
                              const struct sockaddr_in *addr,
                              socklen_t addr_len,
                              uint64_t now,
                              int zombies,
                              int *is_new) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (clients[i].in_use && addr_equal(&clients[i].addr, addr)) {
      clients[i].last_seen_ms = now;
      if (is_new) {
        *is_new = 0;
      }
      return i;
    }
  }
  uint8_t zombie_mask[MAX_PLAYERS];
  compute_zombie_mask(clients, zombies, zombie_mask);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use && !zombie_mask[i]) {
      clients[i].in_use = 1;
      clients[i].addr = *addr;
      clients[i].addr_len = addr_len;
      clients[i].last_seen_ms = now;
      clients[i].sent_bricks = 0;
      clients[i].have_delta_seq = 0;
      clients[i].last_delta_seq = 0;
      if (is_new) {
        *is_new = 1;
      }
      return i;
    }
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      clients[i].in_use = 1;
      clients[i].addr = *addr;
      clients[i].addr_len = addr_len;
      clients[i].last_seen_ms = now;
      clients[i].sent_bricks = 0;
      clients[i].have_delta_seq = 0;
      clients[i].last_delta_seq = 0;
      if (is_new) {
        *is_new = 1;
      }
      return i;
    }
  }
  if (is_new) {
    *is_new = 0;
  }
  return -1;
}

static void reap_timed_out_clients(struct client_slot *clients, uint64_t now) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    if (now - clients[i].last_seen_ms < CLIENT_TIMEOUT_MS) {
      continue;
    }
    log_client_event("disconnected", i, &clients[i].addr);
    memset(&clients[i], 0, sizeof(clients[i]));
  }
}

static void build_snapshot(uint8_t seq, const struct player_state *players,
                           uint8_t *out, size_t out_len) {
  if (out_len < 19) {
    return;
  }
  out[0] = PKT_SNAPSHOT;
  out[1] = seq;
  out[2] = 0x01;
  out[3] = players[0].x;
  out[4] = players[0].y;
  out[5] = players[1].x;
  out[6] = players[1].y;
  out[7] = players[2].x;
  out[8] = players[2].y;
  out[9] = players[3].x;
  out[10] = players[3].y;
  out[11] = players[0].joy;
  out[12] = players[1].joy;
  out[13] = players[2].joy;
  out[14] = players[3].joy;
  out[15] = players[0].score;
  out[16] = players[1].score;
  out[17] = players[2].score;
  out[18] = players[3].score;
}

static int is_brick(const uint8_t *bricks, int x, int y) {
  if (x < 0 || x >= 20 || y < 0 || y >= 19) {
    return 1;
  }
  int idx = y * 20 + x;
  return (bricks[idx / 8] >> (idx % 8)) & 1;
}

static int is_outer_wall_cell(int x, int y) {
  return x == 0 || x == 19 || y == 0 || y == 18;
}

static void clear_brick(uint8_t *bricks, int x, int y) {
  if (x < 0 || x >= 20 || y < 0 || y >= 19) {
    return;
  }
  int idx = y * 20 + x;
  bricks[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static void debug_joy(uint8_t joy) {
  uint8_t stick = (uint8_t)(joy & 0x0F);
  int trig = (joy & 0x10) != 0;
  printf("JOY stick=%u trig=%d\n", stick, trig);
}

static void build_brick_delta(uint8_t seq, uint8_t x, uint8_t y,
                              uint8_t *out, size_t out_len) {
  if (out_len < 4) {
    return;
  }
  out[0] = PKT_BRICK_DELTA;
  out[1] = seq;
  out[2] = x;
  out[3] = y;
}

static void build_respawn(uint8_t seq, uint8_t pid, uint8_t x, uint8_t y,
                          uint8_t flags, uint8_t *out, size_t out_len) {
  if (out_len < 6) {
    return;
  }
  out[0] = PKT_RESPAWN;
  out[1] = seq;
  out[2] = pid;
  out[3] = x;
  out[4] = y;
  out[5] = flags;
}

static void build_shot(uint8_t seq, uint8_t pid, uint8_t x, uint8_t y,
                       uint8_t active, uint8_t *out, size_t out_len) {
  if (out_len < 6) {
    return;
  }
  out[0] = PKT_SHOT;
  out[1] = seq;
  out[2] = pid;
  out[3] = x;
  out[4] = y;
  out[5] = active;
}

static uint8_t shot_active_flags(const struct shot_state *s) {
  uint8_t dir = 0;
  if (s->dy > 0) {
    dir = 1;
  } else if (s->dx < 0) {
    dir = 2;
  } else if (s->dy < 0) {
    dir = 3;
  }
  return (uint8_t)(1u | (uint8_t)(dir << 1));
}

static int stick_to_cardinal_delta(uint8_t stick, int *dx, int *dy) {
  switch (stick & 0x0F) {
    case 0x07:  // right
      *dx = 1;
      *dy = 0;
      return 1;
    case 0x0D:  // down
      *dx = 0;
      *dy = 1;
      return 1;
    case 0x0B:  // left
      *dx = -1;
      *dy = 0;
      return 1;
    case 0x0E:  // up
      *dx = 0;
      *dy = -1;
      return 1;
    default:
      *dx = 0;
      *dy = 0;
      return 0;
  }
}

static int is_valid_stick_nibble(uint8_t stick) {
  switch (stick & 0x0F) {
    case 0x07:
    case 0x0D:
    case 0x0B:
    case 0x0E:
    case 0x0F:
      return 1;
    default:
      return 0;
  }
}

static int sanitize_client_joy(uint8_t raw, uint8_t *out) {
  if ((raw & 0xE0) != 0) {
    return 0;
  }
  uint8_t stick = (uint8_t)(raw & 0x0F);
  if (!is_valid_stick_nibble(stick)) {
    return 0;
  }
  *out = (uint8_t)((raw & 0x10) | stick);
  return 1;
}

static int delta_seq_is_fresh(struct client_slot *c, uint8_t seq) {
  if (!c->have_delta_seq) {
    c->have_delta_seq = 1;
    c->last_delta_seq = seq;
    return 1;
  }
  uint8_t diff = (uint8_t)(seq - c->last_delta_seq);
  if (diff == 0 || diff >= 0x80) {
    return 0;
  }
  c->last_delta_seq = seq;
  return 1;
}

static int is_player_at(const struct player_state *players, int x, int y,
                         int ignore_idx) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == ignore_idx) {
      continue;
    }
    if (players[i].x == (uint8_t)x && players[i].y == (uint8_t)y) {
      return 1;
    }
  }
  return 0;
}

static void pick_spawn(const uint8_t *bricks, const struct player_state *players,
                       uint8_t *out_x, uint8_t *out_y) {
  for (int tries = 0; tries < 200; tries++) {
    int x = rand() % 20;
    int y = rand() % 19;
    if (!is_brick(bricks, x, y) && !is_player_at(players, x, y, -1)) {
      *out_x = (uint8_t)x;
      *out_y = (uint8_t)y;
      return;
    }
  }
  for (int y = 0; y < 19; y++) {
    for (int x = 0; x < 20; x++) {
      if (!is_brick(bricks, x, y) && !is_player_at(players, x, y, -1)) {
        *out_x = (uint8_t)x;
        *out_y = (uint8_t)y;
        return;
      }
    }
  }
  *out_x = 0;
  *out_y = 0;
}

static void broadcast_packet(int sock, struct client_slot *clients,
                             const uint8_t *pkt, size_t len) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    sendto(sock, pkt, len, 0,
           (struct sockaddr *)&clients[i].addr, clients[i].addr_len);
  }
}

static void handle_client_packet(int slot, const uint8_t *pkt, size_t len,
                                 struct player_state *players, uint8_t *brick_bits,
                                 int sock, struct client_slot *clients,
                                 uint8_t *seq, int debug,
                                 uint64_t now, uint64_t *last_input_ms) {
  if (len == 4 && pkt[0] == PKT_DELTA) {
    uint8_t pid = (uint8_t)slot;
    if (pid < MAX_PLAYERS) {
      uint8_t seq_in = 0;
      uint8_t joy = 0;
      int parsed = 0;
      // Primary format: [0x41][seq][pid][joy]
      if (pkt[2] == pid && sanitize_client_joy(pkt[3], &joy)) {
        seq_in = pkt[1];
        parsed = 1;
      // Atari observed format under netstream framing: [0x41][pid][seq][joy]
      } else if (pkt[1] == pid && sanitize_client_joy(pkt[3], &joy)) {
        seq_in = pkt[2];
        parsed = 1;
        if (debug) {
          printf("DELTA slot=%d using swapped seq/pid decode\n", slot);
        }
      }
      if (!parsed) {
        if (debug) {
          printf("DROP DELTA slot=%d bad-bytes=[%02X %02X %02X %02X] expected-pid=%u\n",
                 slot, (unsigned)pkt[0], (unsigned)pkt[1],
                 (unsigned)pkt[2], (unsigned)pkt[3], (unsigned)pid);
        }
        return;
      }
      if (!delta_seq_is_fresh(&clients[slot], seq_in)) {
        if (debug) {
          printf("DROP DELTA slot=%d stale-seq=%u bytes=[%02X %02X %02X %02X]\n",
                 slot, (unsigned)seq_in, (unsigned)pkt[0], (unsigned)pkt[1],
                 (unsigned)pkt[2], (unsigned)pkt[3]);
        }
        return;
      }
      players[pid].joy = joy;
      last_input_ms[pid] = now;
      if (debug) {
        printf("DELTA slot=%d pid=%u ", slot, pid);
        debug_joy(joy);
      }
    }
    return;
  }

  if (len == 6 && pkt[0] == PKT_RESPAWN) {
    uint8_t pid = (uint8_t)slot;
    if (pid < MAX_PLAYERS) {
      uint8_t sx = 0, sy = 0;
      uint8_t out[6];
      pick_spawn(brick_bits, players, &sx, &sy);
      players[pid].x = sx;
      players[pid].y = sy;
      build_respawn((*seq)++, pid, sx, sy, 0x03, out, sizeof(out));
      broadcast_packet(sock, clients, out, sizeof(out));
      if (debug) {
        printf("TX respawn pid=%u x=%u y=%u\n", pid, sx, sy);
      }
    }
    return;
  }

  if (len == 4 && pkt[0] == PKT_BRICK_DELTA) {
    uint8_t x = pkt[2];
    uint8_t y = pkt[3];
    if (x < 20 && y < 19 && !is_outer_wall_cell((int)x, (int)y)) {
      uint8_t out[4];
      clear_brick(brick_bits, x, y);
      build_brick_delta((*seq)++, x, y, out, sizeof(out));
      broadcast_packet(sock, clients, out, sizeof(out));
      if (debug) {
        printf("TX brick_delta x=%u y=%u\n", x, y);
      }
    }
    return;
  }
}

static void process_client_bytes(int slot, const uint8_t *buf, size_t n,
                                 struct player_state *players, uint8_t *brick_bits,
                                 int sock, struct client_slot *clients,
                                 uint8_t *seq, int debug,
                                 uint64_t now, uint64_t *last_input_ms) {
  struct client_slot *c = &clients[slot];
  for (size_t i = 0; i < n; i++) {
    uint8_t b = buf[i];
    if (c->rx_need == 0) {
      if (b == PKT_DELTA || b == PKT_BRICK_DELTA) {
        c->rx_need = 4;
      } else if (b == PKT_RESPAWN) {
        c->rx_need = 6;
      } else {
        continue;
      }
      c->rx_idx = 0;
    }

    if (c->rx_idx < sizeof(c->rx_buf)) {
      c->rx_buf[c->rx_idx] = b;
    }
    c->rx_idx++;

    // Some Atari netstream paths prepend an extra 0x41 byte before DELTA:
    // [0x41][0x41][seq][pid][joy]. Detect and normalize it before dispatch.
    if (c->rx_need == 4 && c->rx_idx == 4 && c->rx_buf[0] == PKT_DELTA &&
        c->rx_buf[1] == PKT_DELTA && c->rx_buf[2] >= MAX_PLAYERS &&
        c->rx_buf[3] < MAX_PLAYERS) {
      c->rx_need = 5;
      continue;
    }

    if (c->rx_need == 5 && c->rx_idx >= 5) {
      uint8_t pkt4[4];
      pkt4[0] = PKT_DELTA;
      pkt4[1] = c->rx_buf[2];
      pkt4[2] = c->rx_buf[3];
      pkt4[3] = c->rx_buf[4];
      if (debug) {
        printf("DELTA normalize slot=%d raw=[%02X %02X %02X %02X %02X]\n",
               slot,
               (unsigned)c->rx_buf[0], (unsigned)c->rx_buf[1],
               (unsigned)c->rx_buf[2], (unsigned)c->rx_buf[3],
               (unsigned)c->rx_buf[4]);
      }
      handle_client_packet(slot, pkt4, sizeof(pkt4), players, brick_bits,
                           sock, clients, seq, debug, now,
                           last_input_ms);
      c->rx_need = 0;
      c->rx_idx = 0;
      continue;
    }

    if (c->rx_need != 0 && c->rx_idx >= c->rx_need) {
      if (c->rx_need == 4 && c->rx_buf[0] == PKT_DELTA &&
          c->rx_buf[2] != (uint8_t)slot && c->rx_buf[1] != (uint8_t)slot) {
        if (debug) {
          printf("DELTA resync slot=%d raw=[%02X %02X %02X %02X]\n",
                 slot,
                 (unsigned)c->rx_buf[0], (unsigned)c->rx_buf[1],
                 (unsigned)c->rx_buf[2], (unsigned)c->rx_buf[3]);
        }
        memmove(&c->rx_buf[0], &c->rx_buf[1], c->rx_idx - 1);
        c->rx_idx--;
        c->rx_need = 4;
        continue;
      }
      if (c->rx_need <= sizeof(c->rx_buf)) {
        handle_client_packet(slot, c->rx_buf, c->rx_need, players, brick_bits,
                             sock, clients, seq, debug, now,
                             last_input_ms);
      }
      c->rx_need = 0;
      c->rx_idx = 0;
    }
  }
}

static void compute_zombie_mask(const struct client_slot *clients, int zombies,
                                uint8_t *out_mask) {
  memset(out_mask, 0, MAX_PLAYERS);
  if (zombies <= 0) {
    return;
  }
  int remaining = zombies;
  for (int i = 1; i < MAX_PLAYERS && remaining > 0; i++) {
    if (clients[i].in_use) {
      continue;
    }
    out_mask[i] = 1;
    remaining--;
  }
}

static uint8_t stick_from_dir(uint8_t dir) {
  switch (dir & 0x03) {
    case 0: return 0x07;  // right (bit3=0)
    case 1: return 0x0D;  // down  (bit1=0)
    case 2: return 0x0B;  // left  (bit2=0)
    case 3: return 0x0E;  // up    (bit0=0)
    default: return 0x0F;
  }
}

static int dir_free(uint8_t dir, const struct player_state *players,
                    const uint8_t *bricks, int idx) {
  int dx = 0, dy = 0;
  switch (dir & 0x03) {
    case 0: dx = 1; break;
    case 1: dy = 1; break;
    case 2: dx = -1; break;
    case 3: dy = -1; break;
  }
  int nx = (int)players[idx].x + dx;
  int ny = (int)players[idx].y + dy;
  if (is_brick(bricks, nx, ny)) {
    return 0;
  }
  if (is_player_at(players, nx, ny, idx)) {
    return 0;
  }
  return 1;
}

static int clear_row_shot(const uint8_t *bricks, int y, int x0, int x1) {
  if (x0 == x1) {
    return 1;
  }
  int step = (x1 > x0) ? 1 : -1;
  for (int x = x0 + step; x != x1; x += step) {
    if (is_brick(bricks, x, y)) {
      return 0;
    }
  }
  return 1;
}

static int clear_col_shot(const uint8_t *bricks, int x, int y0, int y1) {
  if (y0 == y1) {
    return 1;
  }
  int step = (y1 > y0) ? 1 : -1;
  for (int y = y0 + step; y != y1; y += step) {
    if (is_brick(bricks, x, y)) {
      return 0;
    }
  }
  return 1;
}

static void zombie_ai(int idx, struct player_state *players,
                      const uint8_t *bricks, const uint8_t *human_mask) {
  uint8_t zx = players[idx].x;
  uint8_t zy = players[idx].y;

  // Shoot if any player is in same row
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!human_mask[p] || p == idx || players[p].respawn_at_ms != 0) {
      continue;
    }
    if (players[p].y == zy &&
        clear_row_shot(bricks, (int)zy, (int)zx, (int)players[p].x)) {
      uint8_t dir = (players[p].x > zx) ? 0 : 2;
      players[idx].joy = stick_from_dir(dir) | 0x10;
      return;
    }
  }
  // Shoot if any player is in same column
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!human_mask[p] || p == idx || players[p].respawn_at_ms != 0) {
      continue;
    }
    if (players[p].x == zx &&
        clear_col_shot(bricks, (int)zx, (int)zy, (int)players[p].y)) {
      uint8_t dir = (players[p].y > zy) ? 1 : 3;
      players[idx].joy = stick_from_dir(dir) | 0x10;
      return;
    }
  }

  // Find nearest player (Manhattan)
  int best = -1;
  int best_dist = 0x7FFF;
  int best_dx = 0;
  int best_dy = 0;
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!human_mask[p] || p == idx || players[p].respawn_at_ms != 0) {
      continue;
    }
    int dx = (int)zx - (int)players[p].x;
    int dy = (int)zy - (int)players[p].y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int dist = adx + ady;
    if (dist < best_dist) {
      best_dist = dist;
      best = p;
      best_dx = dx;
      best_dy = dy;
    }
  }
  if (best < 0) {
    players[idx].joy = 0x0F;
    return;
  }

  uint8_t dir_x = (best_dx > 0) ? 2 : 0;
  uint8_t dir_y = (best_dy > 0) ? 3 : 1;

  // Choose longer axis first, then try short; else shoot out wall
  if (abs(best_dy) >= abs(best_dx)) {
    if (dir_free(dir_y, players, bricks, idx)) {
      players[idx].joy = stick_from_dir(dir_y);
      return;
    }
    if (dir_free(dir_x, players, bricks, idx)) {
      players[idx].joy = stick_from_dir(dir_x);
      return;
    }
    players[idx].joy = stick_from_dir(dir_y) | 0x10;
    return;
  }
  if (dir_free(dir_x, players, bricks, idx)) {
    players[idx].joy = stick_from_dir(dir_x);
    return;
  }
  if (dir_free(dir_y, players, bricks, idx)) {
    players[idx].joy = stick_from_dir(dir_y);
    return;
  }
  players[idx].joy = stick_from_dir(dir_x) | 0x10;
}

static void apply_move_if_free(struct player_state *p,
                               const uint8_t *bricks,
                               const struct player_state *players,
                               int idx) {
  uint8_t stick = (uint8_t)(p->joy & 0x0F);
  int dx = 0;
  int dy = 0;
  if (!stick_to_cardinal_delta(stick, &dx, &dy)) {
    return;
  }

  int nx = (int)p->x + dx;
  int ny = (int)p->y + dy;
  if (nx < 0) nx = 0;
  if (nx > 19) nx = 19;
  if (ny < 0) ny = 0;
  if (ny > 18) ny = 18;
  if (!is_brick(bricks, nx, ny) && !is_player_at(players, nx, ny, idx)) {
    p->x = (uint8_t)nx;
    p->y = (uint8_t)ny;
  }
}

static void start_shot(int shooter, struct player_state *players,
                       struct shot_state *shots, uint8_t *bricks,
                       int sock, struct client_slot *clients, uint8_t *seq,
                       int debug) {
  if (players[shooter].respawn_at_ms != 0) {
    return;
  }
  if (shots[shooter].active) {
    return;
  }
  uint8_t stick = (uint8_t)(players[shooter].joy & 0x0F);
  int trig = (players[shooter].joy & 0x10) != 0;
  int dx = 0;
  int dy = 0;
  if (!trig || !stick_to_cardinal_delta(stick, &dx, &dy)) {
    return;
  }
  int sx = (int)players[shooter].x + dx;
  int sy = (int)players[shooter].y + dy;
  if (is_brick(bricks, sx, sy)) {
    if (!is_outer_wall_cell(sx, sy)) {
      clear_brick(bricks, sx, sy);
      uint8_t pkt[4];
      build_brick_delta((*seq)++, (uint8_t)sx, (uint8_t)sy, pkt, sizeof(pkt));
      broadcast_packet(sock, clients, pkt, sizeof(pkt));
      if (debug) {
        printf("TX brick_delta x=%u y=%u\n", pkt[2], pkt[3]);
      }
    }
    return;
  }
  if (sx < 0 || sx > 19 || sy < 0 || sy > 18) {
    return;
  }
  if (is_player_at(players, sx, sy, shooter)) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
      if (p == shooter) {
        continue;
      }
      if (players[p].respawn_at_ms != 0) {
        continue;
      }
      if (players[p].x == (uint8_t)sx && players[p].y == (uint8_t)sy) {
        uint64_t now = now_ms();
        players[shooter].score++;
        players[p].respawn_at_ms = now + 2000;
        {
          uint8_t rpkt[6];
          build_respawn((*seq)++, (uint8_t)p, 0, 0, 0x01, rpkt, sizeof(rpkt));
          broadcast_packet(sock, clients, rpkt, sizeof(rpkt));
        }
        /* Defensive clear: ensure any stale client-side shot sprite is removed. */
        {
          uint8_t spkt[6];
          build_shot((*seq)++, (uint8_t)shooter, 0, 0, 0, spkt, sizeof(spkt));
          broadcast_packet(sock, clients, spkt, sizeof(spkt));
        }
        if (debug) {
          printf("TX immediate hit shooter=%d victim=%d\n", shooter, p);
        }
        return;
      }
    }
    return;
  }
  shots[shooter].active = 1;
  shots[shooter].x = (uint8_t)sx;
  shots[shooter].y = (uint8_t)sy;
  shots[shooter].dx = (int8_t)dx;
  shots[shooter].dy = (int8_t)dy;
  shots[shooter].clear_burst = 0;
}

static void step_shots(struct player_state *players, struct shot_state *shots,
                       uint8_t *bricks, int sock,
                       struct client_slot *clients, uint8_t *seq,
                       int debug) {
  uint64_t now = now_ms();
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!shots[i].active) {
      continue;
    }
    int nx = (int)shots[i].x + shots[i].dx;
    int ny = (int)shots[i].y + shots[i].dy;
    if (nx < 0 || nx > 19 || ny < 0 || ny > 18) {
      shots[i].active = 0;
      shots[i].clear_burst = 3;
      uint8_t pkt[6];
      build_shot((*seq)++, (uint8_t)i, 0, 0, 0, pkt, sizeof(pkt));
      broadcast_packet(sock, clients, pkt, sizeof(pkt));
      continue;
    }
    if (is_brick(bricks, nx, ny)) {
      if (!is_outer_wall_cell(nx, ny)) {
        clear_brick(bricks, nx, ny);
        uint8_t pkt[4];
        build_brick_delta((*seq)++, (uint8_t)nx, (uint8_t)ny, pkt, sizeof(pkt));
        broadcast_packet(sock, clients, pkt, sizeof(pkt));
        if (debug) {
          printf("TX brick_delta x=%u y=%u\n", pkt[2], pkt[3]);
        }
      }
      shots[i].active = 0;
      shots[i].clear_burst = 3;
      uint8_t spkt[6];
      build_shot((*seq)++, (uint8_t)i, 0, 0, 0, spkt, sizeof(spkt));
      broadcast_packet(sock, clients, spkt, sizeof(spkt));
      continue;
    }
    for (int p = 0; p < MAX_PLAYERS; p++) {
      if (p == i) {
        continue;
      }
      if (players[p].respawn_at_ms != 0) {
        continue;
      }
      if (players[p].x == (uint8_t)nx && players[p].y == (uint8_t)ny) {
        players[i].score++;
        players[p].respawn_at_ms = now + 2000;
        uint8_t pkt[6];
        build_respawn((*seq)++, (uint8_t)p, 0, 0, 0x01, pkt, sizeof(pkt));
        broadcast_packet(sock, clients, pkt, sizeof(pkt));
        if (debug) {
          printf("TX respawn pending pid=%u\n", (unsigned)p);
        }
        shots[i].active = 0;
        shots[i].clear_burst = 3;
        uint8_t spkt[6];
        build_shot((*seq)++, (uint8_t)i, 0, 0, 0, spkt, sizeof(spkt));
        broadcast_packet(sock, clients, spkt, sizeof(spkt));
        goto next_shot;
      }
    }
    shots[i].x = (uint8_t)nx;
    shots[i].y = (uint8_t)ny;
    {
      uint8_t spkt[6];
      build_shot((*seq)++, (uint8_t)i, (uint8_t)nx, (uint8_t)ny,
                 shot_active_flags(&shots[i]),
                 spkt, sizeof(spkt));
      broadcast_packet(sock, clients, spkt, sizeof(spkt));
    }
  next_shot:
    ;
  }
  // Re-send shot clear a few ticks after a shot ends to heal over UDP loss
  // without flooding the link with full shot state every frame.
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (shots[i].clear_burst == 0) {
      continue;
    }
    shots[i].clear_burst--;
    uint8_t spkt[6];
    build_shot((*seq)++, (uint8_t)i, 0, 0, 0, spkt, sizeof(spkt));
    broadcast_packet(sock, clients, spkt, sizeof(spkt));
  }
}

static void step_players(struct player_state *players, struct shot_state *shots,
                         uint8_t *bricks, int sock,
                         struct client_slot *clients, uint8_t *seq,
                         int debug, int zombies,
                         const uint64_t *last_input_ms) {
  uint64_t now = now_ms();
  uint8_t zombie_mask[MAX_PLAYERS];
  uint8_t human_mask[MAX_PLAYERS];
  compute_zombie_mask(clients, zombies, zombie_mask);
  memset(human_mask, 0, sizeof(human_mask));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (zombie_mask[i]) {
      continue;
    }
    if (clients[i].in_use) {
      human_mask[i] = 1;
      continue;
    }
    if (last_input_ms[i] == 0) {
      continue;
    }
    if ((now - last_input_ms[i]) >= CLIENT_TIMEOUT_MS) {
      continue;
    }
    human_mask[i] = 1;
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (players[i].respawn_at_ms != 0 &&
        now >= players[i].respawn_at_ms) {
      uint8_t sx = 0, sy = 0;
      pick_spawn(bricks, players, &sx, &sy);
      players[i].x = sx;
      players[i].y = sy;
      players[i].respawn_at_ms = 0;
      uint8_t pkt[6];
      build_respawn((*seq)++, (uint8_t)i, sx, sy, 0x03, pkt, sizeof(pkt));
      broadcast_packet(sock, clients, pkt, sizeof(pkt));
      if (debug) {
        printf("TX respawn pid=%u x=%u y=%u\n", (unsigned)i, sx, sy);
      }
    }
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!zombie_mask[i]) {
      if (last_input_ms[i] == 0 || (now - last_input_ms[i]) > INPUT_STALE_MS) {
        players[i].joy = 0x0F;
      }
    }
    int can_act = (players[i].respawn_at_ms == 0);
    if (zombie_mask[i] && can_act) {
      if (now >= players[i].zombie_next_ms) {
        zombie_ai(i, players, bricks, human_mask);
        players[i].zombie_next_ms = now + 250;
      } else {
        can_act = 0;
      }
    }
    uint8_t stick = (uint8_t)(players[i].joy & 0x0F);
    int trig = (players[i].joy & 0x10) != 0;
    if (can_act) {
      start_shot(i, players, shots, bricks, sock, clients, seq, debug);
      if (!(trig && stick != 0x0F)) {
        apply_move_if_free(&players[i], bricks, players, i);
      }
    }
  }
  step_shots(players, shots, bricks, sock, clients, seq, debug);
}

static void build_brick_full(uint8_t seq, const uint8_t *bits,
                             uint8_t *out, size_t out_len) {
  if (out_len < 51) {
    return;
  }
  out[0] = PKT_BRICK_FULL;
  out[1] = seq;
  out[2] = 0x01;
  memcpy(&out[3], bits, 48);
}

static int load_brick_layout(const char *path, uint8_t *bits, size_t bits_len) {
  if (bits_len < 48) {
    return -1;
  }
  memset(bits, 0, bits_len);
  FILE *fp = fopen(path, "r");
  if (!fp) {
    return -1;
  }
  char line[256];
  int y = 0;
  while (fgets(line, sizeof(line), fp) && y < 19) {
    size_t len = strcspn(line, "\r\n");
    if (len == 0) {
      continue;
    }
    if (len != 20) {
      fclose(fp);
      return -1;
    }
    for (int x = 0; x < 20; x++) {
      char c = line[x];
      if (c != '.' && c != '#') {
        fclose(fp);
        return -1;
      }
      if (c == '#') {
        int idx = y * 20 + x;
        bits[idx / 8] |= (uint8_t)(1u << (idx % 8));
      }
    }
    y++;
  }
  fclose(fp);
  return (y == 19) ? 0 : -1;
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s [--port PORT] [--tick-hz N] [--zombies N] [--brick PATH] [--debug]\n",
          argv0);
}

int main(int argc, char **argv) {
  int port = 9000;
  int tick_hz = 10;
  int debug = 0;
  int zombies = 1;
  const char *brick_path = "server/brick_layout.txt";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--tick-hz") == 0 && i + 1 < argc) {
      tick_hz = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--zombies") == 0 && i + 1 < argc) {
      zombies = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--brick") == 0 && i + 1 < argc) {
      brick_path = argv[++i];
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

  if (port <= 0 || port > 65535 || tick_hz <= 0) {
    fprintf(stderr, "Invalid port or tick-hz.\\n");
    return 1;
  }
  if (zombies < 0) {
    zombies = 0;
  }
  if (zombies > (MAX_PLAYERS - 1)) {
    zombies = MAX_PLAYERS - 1;
  }

  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  int one = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return 1;
  }

  struct client_slot clients[MAX_PLAYERS];
  struct player_state players[MAX_PLAYERS];
  struct shot_state shots[MAX_PLAYERS];
  uint64_t last_input_ms[MAX_PLAYERS];
  memset(clients, 0, sizeof(clients));
  memset(players, 0, sizeof(players));
  memset(shots, 0, sizeof(shots));
  memset(last_input_ms, 0, sizeof(last_input_ms));
  uint8_t brick_bits[48];
  if (load_brick_layout(brick_path, brick_bits, sizeof(brick_bits)) != 0) {
    memset(brick_bits, 0, sizeof(brick_bits));
    fprintf(stderr, "Warning: failed to load brick layout: %s\n", brick_path);
  }
  srand((unsigned int)time(NULL));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    uint8_t sx = 0, sy = 0;
    pick_spawn(brick_bits, players, &sx, &sy);
    players[i].x = sx;
    players[i].y = sy;
    players[i].joy = 0x0F;
  }

  uint8_t seq = 0;
  uint64_t next_tick = now_ms();
  const uint64_t tick_ms = 1000ULL / (uint64_t)tick_hz;

  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("maze-war server listening on UDP port %d, %d Hz, zombies=%d\n",
         port, tick_hz, zombies);

  while (g_running) {
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    uint64_t now = now_ms();
    int timeout_ms = 0;
    if (next_tick > now) {
      uint64_t delta = next_tick - now;
      timeout_ms = (delta > 1000) ? 1000 : (int)delta;
    }

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr > 0 && (pfd.revents & POLLIN)) {
      uint8_t buf[256];
      struct sockaddr_in src;
      socklen_t src_len = sizeof(src);
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &src_len);
      if (n > 0) {
        int is_new = 0;
        int slot = find_or_add_client(clients, &src, src_len, now, zombies, &is_new);
        if (debug) {
          printf("RX(%zd) from slot %d\n", n, slot);
        }
        if (slot >= 0 && is_new) {
          log_client_event("connected", slot, &clients[slot].addr);
          uint8_t bfull[51];
          build_brick_full(seq++, brick_bits, bfull, sizeof(bfull));
          sendto(sock, bfull, sizeof(bfull), 0,
                 (struct sockaddr *)&clients[slot].addr, clients[slot].addr_len);
          clients[slot].sent_bricks = 1;
          if (debug) {
            printf("TX brick_full -> slot %d\n", slot);
          }
        }
        if (slot >= 0) {
          process_client_bytes(slot, buf, (size_t)n, players, brick_bits, sock,
                               clients, &seq, debug, now,
                               last_input_ms);
        }
      }
    }

    reap_timed_out_clients(clients, now_ms());

    now = now_ms();
    if (now >= next_tick) {
      step_players(players, shots, brick_bits, sock, clients, &seq, debug,
                   zombies, last_input_ms);
      uint8_t pkt[19];
      build_snapshot(seq++, players, pkt, sizeof(pkt));
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].in_use) {
          continue;
        }
        // Encode recipient's authoritative local player id in snapshot flags.
        pkt[2] = (uint8_t)(0x01u | ((uint8_t)i << 1));
        ssize_t wn = sendto(sock, pkt, sizeof(pkt), 0,
                            (struct sockaddr *)&clients[i].addr,
                            clients[i].addr_len);
        if (debug && wn == (ssize_t)sizeof(pkt)) {
          printf("TX snapshot -> slot %d\n", i);
        }
      }
      next_tick = now + tick_ms;
    }
  }

  close(sock);
  if (debug) {
    puts("server stopped");
  }
  return 0;
}
