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

#include "transport_stats.h"
#include "transport_normalize.h"

enum {
  PKT_SNAPSHOT = 0x40,
  PKT_DELTA = 0x41,
  PKT_SHOT = 0x42,
  PKT_NAME = 0x43,
  PKT_SEATS = 0x44,
  PKT_BRICK_FULL = 0x50,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52
};

enum { MAX_PLAYERS = 4 };
/* A slot must be released fast enough that a reconnecting player does not
   sit beside their own ghost -- FujiNet picks a fresh UDP source port every
   time it reopens the stream, so a reconnect always lands in a new slot and
   the old one lingers until it times out. It must also survive the longest
   legitimate quiet stretch: clients are not required to send continuously,
   and only speak when they act. 15s is ~150 missed packets from the 10Hz
   Atari client, and sits just past the client's own ~13s give-up watchdog,
   so the slot frees shortly after the client has stopped sending. */
enum { CLIENT_TIMEOUT_MS = 15000 };
enum { INPUT_STALE_MS = 500 };
/* Client inputs are queued and applied one per tick, in order, instead of the
   newest arrival overwriting whatever had not been read yet. Overwriting lost
   every input that landed between ticks -- a quick corner turn never reached
   the simulation at all -- while the ack still advanced to the newest sequence
   received, so the client believed the turn had been applied, dropped it from
   its pending ring, and then snapped back once the drift crossed the
   reconcile threshold.

   The queue is short on purpose. It only needs to absorb the burst a player
   makes changing direction; anything deeper would just be added input latency.
   On overflow the arriving input is dropped and deliberately NOT acked, so the
   client keeps it pending and replays it. */
enum { INPUT_QUEUE_MAX = 6 };
enum { TRANSPORT_SUMMARY_MS = 2000 };
/* BRICK_DELTA is sent once and never acknowledged, so a single lost packet
   used to desync a client's maze from the server for the rest of the match
   -- visible as a square that is drawn blank but still blocks movement, or
   drawn solid but is walkable. Re-broadcasting the full map on this period
   bounds that divergence. Clients treat a later BRICK_FULL as a repair and
   only redraw cells that actually changed, so this is not a visible redraw. */
enum { BRICK_RESYNC_MS = 3000 };
/* Names change only on join, leave or rename, and each of those broadcasts
   immediately. The rotation is just a safety net for a lost NAME, so it runs
   on a slow timer: at tick rate it was adding a packet to every single tick,
   which is bandwidth taken from BRICK_DELTA on a link that drops things. */
enum { NAME_ROTATE_MS = 1000 };
/* Which slots a human actually holds. Clients cannot derive this: the zombie
   mask in the snapshot only tells them which slots the AI drives, so an empty
   seat and a silent human looked identical and every client listed four
   players. Broadcast on change so a join or a drop shows up at once, and
   repeat on this timer because the packet is unacknowledged like NAME. */
enum { SEAT_REPEAT_MS = 1000 };
/* A destroyed brick used to be announced once. Losing that one packet left the
   wall painted on a client until the next full resync, which is why a brick
   could take seconds to vanish. Echo it on following ticks like SHOT clears
   do -- one packet per tick, never a burst. */
enum { BRICK_ECHO_MAX = 8, BRICK_ECHO_REPEATS = 2 };
/* Display name length. The Atari HUD gives each slot columns 4..11 of its
   20-column line before the score digit at column 15, so 8 is what fits. */
#define NAME_LEN 8

#define ZOMBIE_THINK_MS 575
#define ZOMBIE_MOVE_MS 275
#define ZOMBIE_FIRE_MS 900

struct player_state {
  uint8_t x;
  uint8_t y;
  uint8_t joy;
  uint8_t score;
  uint8_t zombie_fire_pending;
  uint64_t respawn_at_ms;
  uint64_t zombie_think_next_ms;
  uint64_t zombie_move_next_ms;
  uint64_t zombie_fire_next_ms;
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
  /* All-zero means unnamed: the client never sent a NAME, or the slot changed
     hands. Clients fall back to their WIZARD/ZOMBIE label in that case. */
  uint8_t name[NAME_LEN];
  struct sockaddr_in addr;
  socklen_t addr_len;
  uint64_t last_seen_ms;
  int sent_bricks;
  struct transport_rx_state rx;
  struct transport_counters transport;
  uint8_t have_delta_seq;
  uint8_t last_delta_seq;
  uint8_t have_applied_input_seq;
  uint8_t applied_input_seq;
  struct {
    uint8_t seq;
    uint8_t joy;
    uint64_t ready_at_ms; /* --lag-ms: not applied before this */
  } input_q[INPUT_QUEUE_MAX];
  uint8_t input_head;
  uint8_t input_count;
};

static volatile sig_atomic_t g_running = 1;

static void compute_zombie_mask(const struct client_slot *clients, int zombies,
                                uint8_t *out_mask);
static void log_transport_summary_if_nonzero(int slot,
                                             const struct transport_counters *c);
static void log_transport_summaries(const struct client_slot *clients,
                                    const struct transport_counters *global);
static int packet_has_bad_joy_for_slot(const uint8_t *pkt, size_t len,
                                       uint8_t slot);
static void reset_client_slot(struct client_slot *client);
static void reset_slot_gameplay(int slot, struct player_state *players,
                                struct shot_state *shots,
                                uint64_t *last_input_ms, uint64_t now);

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
      clients[i].have_applied_input_seq = 0;
      clients[i].applied_input_seq = 0;
      clients[i].input_head = 0;
      clients[i].input_count = 0;
      memset(clients[i].name, 0, NAME_LEN); /* the seat's previous occupant */
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
      clients[i].have_applied_input_seq = 0;
      clients[i].applied_input_seq = 0;
      clients[i].input_head = 0;
      clients[i].input_count = 0;
      memset(clients[i].name, 0, NAME_LEN); /* the seat's previous occupant */
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

static void reset_client_slot(struct client_slot *client) {
  if (!client) {
    return;
  }
  memset(client, 0, sizeof(*client));
}

static void reap_timed_out_clients(struct client_slot *clients, uint64_t now,
                                   int debug, struct player_state *players,
                                   struct shot_state *shots,
                                   uint64_t *last_input_ms) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    if (now - clients[i].last_seen_ms < CLIENT_TIMEOUT_MS) {
      continue;
    }
    log_client_event("disconnected", i, &clients[i].addr);
    if (debug) {
      log_transport_summary_if_nonzero(i, &clients[i].transport);
    }
    reset_client_slot(&clients[i]);
    /* The slot falls back to AI control on the next tick, so hand the zombie a
       clean actor rather than the departed human's leftover state. */
    reset_slot_gameplay(i, players, shots, last_input_ms, now);
  }
}

static void log_transport_summary_if_nonzero(int slot,
                                             const struct transport_counters *c) {
  if (!c) {
    return;
  }
  if (c->raw_datagrams == 0 && c->raw_bytes == 0 && c->delta_primary == 0 &&
      c->delta_swapped == 0 && c->delta_extra_41 == 0 &&
      c->delta_resync == 0 && c->drop_bad_joy == 0 &&
      c->drop_stale_seq == 0 && c->accepted_delta == 0) {
    return;
  }
  transport_stats_log_summary(stdout, slot, c);
}

static void log_transport_summaries(const struct client_slot *clients,
                                    const struct transport_counters *global) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    log_transport_summary_if_nonzero(i, &clients[i].transport);
  }
  log_transport_summary_if_nonzero(-1, global);
}

static void build_snapshot(uint8_t seq, const struct player_state *players,
                           uint8_t ack_seq, uint8_t *out, size_t out_len) {
  if (out_len < 20) {
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
  out[19] = ack_seq;
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

/* Names come from a remote client, so treat them as untrusted: fold to the
   uppercase subset the Atari character set can actually draw and pad with
   spaces, rather than passing arbitrary bytes through to a screen buffer. */
static void sanitize_name(const uint8_t *in, uint8_t *out) {
  int w = 0;
  for (int i = 0; i < NAME_LEN; i++) {
    uint8_t c = in[i];
    if (c >= 'a' && c <= 'z') {
      c = (uint8_t)(c - 'a' + 'A');
    }
    int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
             c == '-' || c == '.';
    if (!ok) {
      continue;
    }
    out[w++] = c;
  }
  while (w < NAME_LEN) {
    out[w++] = ' ';
  }
}

static int name_is_set(const uint8_t *name) {
  for (int i = 0; i < NAME_LEN; i++) {
    if (name[i] != 0 && name[i] != ' ') {
      return 1;
    }
  }
  return 0;
}

static void build_name(uint8_t seq, uint8_t pid, const uint8_t *name,
                       uint8_t *out, size_t out_len) {
  if (out_len < 3 + NAME_LEN) {
    return;
  }
  out[0] = PKT_NAME;
  out[1] = seq;
  out[2] = pid;
  memcpy(&out[3], name, NAME_LEN);
}

/* bit n set = slot n is held by a connected client. */
static uint8_t compute_seat_mask(const struct client_slot *clients) {
  uint8_t mask = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (clients[i].in_use) {
      mask |= (uint8_t)(1u << i);
    }
  }
  return mask;
}

static void build_seats(uint8_t seq, uint8_t mask, uint8_t *out,
                        size_t out_len) {
  if (out_len < 3) {
    return;
  }
  out[0] = PKT_SEATS;
  out[1] = seq;
  out[2] = (uint8_t)(mask & 0x0Fu);
}

static void build_brick_delta(uint8_t seq, uint8_t x, uint8_t y,
                              uint8_t *out, size_t out_len);
static void broadcast_packet(int sock, struct client_slot *clients,
                             const uint8_t *pkt, size_t len);
static ssize_t send_checked(int sock, const struct sockaddr *addr,
                            socklen_t addrlen, const uint8_t *pkt, size_t len);

/* Pending brick-destruction echoes. Single-threaded server, one game, so a
   file-scope queue keeps the three break sites from having to thread it. */
static struct {
  uint8_t x;
  uint8_t y;
  uint8_t left;
} g_brick_echo[BRICK_ECHO_MAX];

static void queue_brick_echo(uint8_t x, uint8_t y) {
  int spare = -1;
  for (int i = 0; i < BRICK_ECHO_MAX; i++) {
    if (g_brick_echo[i].left > 0 && g_brick_echo[i].x == x &&
        g_brick_echo[i].y == y) {
      g_brick_echo[i].left = BRICK_ECHO_REPEATS;
      return;
    }
    if (spare < 0 && g_brick_echo[i].left == 0) {
      spare = i;
    }
  }
  if (spare < 0) {
    spare = 0; /* full: the oldest loses its echo, the resync still covers it */
  }
  g_brick_echo[spare].x = x;
  g_brick_echo[spare].y = y;
  g_brick_echo[spare].left = BRICK_ECHO_REPEATS;
}

/* At most one echo per tick: several at once is the burst that loses packets. */
static void flush_brick_echo(int sock, struct client_slot *clients,
                             uint8_t *seq, int debug) {
  for (int i = 0; i < BRICK_ECHO_MAX; i++) {
    if (g_brick_echo[i].left == 0) {
      continue;
    }
    g_brick_echo[i].left--;
    uint8_t pkt[4];
    build_brick_delta((*seq)++, g_brick_echo[i].x, g_brick_echo[i].y, pkt,
                      sizeof(pkt));
    broadcast_packet(sock, clients, pkt, sizeof(pkt));
    if (debug) {
      printf("TX brick_delta echo x=%u y=%u\n", g_brick_echo[i].x,
             g_brick_echo[i].y);
    }
    return;
  }
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

static void debug_combat_order(int debug, const char *phase, int slot,
                               const char *detail) {
  if (!debug) {
    return;
  }
  printf("combat order phase=%s slot=%d %s\n", phase, slot, detail);
}

static void debug_combat_event(int debug, const char *kind, int slot,
                               const char *detail) {
  if (!debug) {
    return;
  }
  printf("combat event kind=%s slot=%d %s\n", kind, slot, detail);
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

static const char *transport_delta_format_name(
    enum transport_delta_format format) {
  switch (format) {
    case TRANSPORT_DELTA_PRIMARY:
      return "primary";
    case TRANSPORT_DELTA_SWAPPED:
      return "swapped";
    case TRANSPORT_DELTA_EXTRA_41:
      return "extra-41";
    default:
      return "unknown";
  }
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

static int sanitize_client_joy(uint8_t raw, uint8_t *out) {
  uint8_t stick = (uint8_t)(raw & 0x0F);
  if ((raw & 0xE0) != 0) {
    return 0;
  }
  switch (stick) {
    case 0x07:
    case 0x0D:
    case 0x0B:
    case 0x0E:
    case 0x0F:
      *out = (uint8_t)(raw & 0x1F);
      return 1;
    default:
      return 0;
  }
}

static int packet_has_bad_joy_for_slot(const uint8_t *pkt, size_t len,
                                       uint8_t slot) {
  uint8_t joy = 0;

  if (!pkt || len < 4 || pkt[0] != PKT_DELTA) {
    return 0;
  }
  if (len == 4 && pkt[2] == slot) {
    return !sanitize_client_joy(pkt[3], &joy);
  }
  if (len == 4 && pkt[1] == slot) {
    return !sanitize_client_joy(pkt[3], &joy);
  }
  if (len == 5 && pkt[1] == PKT_DELTA && pkt[3] == slot) {
    return !sanitize_client_joy(pkt[4], &joy);
  }
  return 0;
}

static int is_player_at(const struct player_state *players, int x, int y,
                         int ignore_idx) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == ignore_idx) {
      continue;
    }
    /* A player awaiting respawn is not on the board. Its coordinates still hold
       the cell it died in, and clients hide it, so counting it here turned the
       death cell into an invisible wall for the whole respawn delay -- and you
       are usually walking straight at someone when you kill them. Every other
       subsystem (zombie targeting, fire evaluation, shot hits) already skips
       respawning players; movement collision was the one that did not. */
    if (players[i].respawn_at_ms != 0) {
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

/* Give a slot a clean actor when it changes hands in either direction: human
   takes over a zombie, or a human drops and the zombie backfills. Without this
   the new owner inherits the old one's facing, score and in-flight shot -- the
   ghost-shot / stale-facing class of bug.

   The actor is deliberately NOT moved. Its position is the slot's current
   physical location, not stale state: the wizard becomes a zombie (or vice
   versa) where it stands, exactly as the original game does. Teleporting on
   handoff would also make every other client see an unexplained jump.
   respawn_at_ms is likewise left alone, so a handoff that lands mid-death lets
   the normal respawn finalizer complete and re-show the actor on clients. */
static void reset_slot_gameplay(int slot, struct player_state *players,
                                struct shot_state *shots,
                                uint64_t *last_input_ms, uint64_t now) {
  if (slot < 0 || slot >= MAX_PLAYERS) {
    return;
  }
  /* A shot in flight belongs to whoever fired it. Retire it with the same
     clear burst a normal shot end uses, so clients erase it instead of leaving
     it painted and crediting it to the slot's new owner. */
  if (shots[slot].active) {
    shots[slot].active = 0;
    shots[slot].clear_burst = 3;
  }
  players[slot].joy = 0x0F; /* neutral: no inherited facing or movement */
  players[slot].score = 0;
  players[slot].zombie_fire_pending = 0;
  /* Zombie schedules are absolute timestamps. A stale one is already in the
     past, which would make the backfilled zombie think, move and fire on its
     very first tick instead of settling into its normal cadence. */
  players[slot].zombie_think_next_ms = now;
  players[slot].zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
  players[slot].zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
  last_input_ms[slot] = 0;
}

/* Announce one slot's name, cycling a slot per tick.
   Deliberately one packet at a time: sending all four in a burst right behind
   the 51-byte BRICK_FULL made the Atari lose the map every time, because that
   whole group leaves the server as five back-to-back datagrams and the FujiNet
   serial path does not absorb the burst. Spread out, nothing is dropped.
   Empty slots are announced as blank rather than skipped, so a client stops
   showing a name once that player leaves. */
static void broadcast_next_name(int sock, struct client_slot *clients,
                                uint8_t *seq, int *rotate) {
  static const uint8_t blank[NAME_LEN] = {' ', ' ', ' ', ' ',
                                          ' ', ' ', ' ', ' '};
  int i = *rotate % MAX_PLAYERS;
  *rotate = (i + 1) % MAX_PLAYERS;
  const uint8_t *name = (clients[i].in_use && name_is_set(clients[i].name))
                            ? clients[i].name
                            : blank;
  uint8_t pkt[3 + NAME_LEN];
  build_name((*seq)++, (uint8_t)i, name, pkt, sizeof(pkt));
  for (int t = 0; t < MAX_PLAYERS; t++) {
    if (!clients[t].in_use) {
      continue;
    }
    send_checked(sock, (struct sockaddr *)&clients[t].addr,
                 clients[t].addr_len, pkt, sizeof(pkt));
  }
}

/* Every server->client packet carries a trailing sum checksum.
   The Atari receives over SIO as a byte stream, so a dropped or duplicated
   byte shifts framing and payload bytes start being read as packet type
   markers. Bounds checks alone let far too much of that through: corrupt
   positions landed actors on the border and erased it, corrupt scores
   flickered, a corrupt brick delta cleared a random cell, and a corrupt
   sequence number parked the client ~100 ticks in the future so every real
   snapshot was dropped as stale for seconds. A checksum makes a misframed
   packet fail closed instead. */
enum { PKT_CKSUM_MAX = 64 };

/* COBS: encode so no zero byte can appear inside a frame, then terminate with
   one. The checksum makes a corrupt frame fail closed, but it cannot realign a
   parser that has lost byte alignment -- it still hunts for a type marker and a
   payload byte that looks like one starts a false packet. With a zero
   delimiter the next boundary always resynchronises, so a byte lost, gained or
   flipped costs exactly one frame. Overhead is one byte for packets this size.
   (The same conclusion the FujiRealm realtime protocol reached.) */
static size_t cobs_encode(const uint8_t *in, size_t n, uint8_t *out) {
  size_t rd = 0, wr = 1, code_i = 0;
  uint8_t code = 1;
  while (rd < n) {
    if (in[rd] == 0) {
      out[code_i] = code;
      code_i = wr++;
      code = 1;
      rd++;
    } else {
      out[wr++] = in[rd++];
      if (++code == 0xFF) {
        out[code_i] = code;
        code_i = wr++;
        code = 1;
      }
    }
  }
  out[code_i] = code;
  return wr;
}

static ssize_t send_checked(int sock, const struct sockaddr *addr,
                            socklen_t addrlen, const uint8_t *pkt, size_t len) {
  uint8_t raw[PKT_CKSUM_MAX];
  uint8_t buf[PKT_CKSUM_MAX + PKT_CKSUM_MAX / 254 + 2];
  if (len + 1 > sizeof(raw)) {
    return -1;
  }
  memcpy(raw, pkt, len);
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum = (uint8_t)(sum + pkt[i]);
  }
  raw[len] = sum;
  size_t enc = cobs_encode(raw, len + 1, buf);
  buf[enc++] = 0x00; /* frame delimiter */
  ssize_t n = sendto(sock, buf, enc, 0, addr, addrlen);
  /* Report the payload length callers passed in, not the wire length: framing
     is transport, and every call site checks the result against the size of the
     packet it built. */
  return (n == (ssize_t)enc) ? (ssize_t)len : -1;
}

static void broadcast_packet(int sock, struct client_slot *clients,
                             const uint8_t *pkt, size_t len) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    send_checked(sock, (struct sockaddr *)&clients[i].addr,
                 clients[i].addr_len, pkt, len);
  }
}

static void handle_client_packet(int slot, const uint8_t *pkt, size_t len,
                                 struct player_state *players, uint8_t *brick_bits,
                                 int sock, struct client_slot *clients,
                                 uint8_t *seq, int debug,
                                 uint64_t now, uint64_t *last_input_ms,
                                 struct transport_counters *global_transport,
                                 int lag_ms) {
  if (pkt[0] == PKT_DELTA) {
    uint8_t pid = (uint8_t)slot;
    if (pid < MAX_PLAYERS) {
      struct transport_delta_packet delta;
      if (!transport_decode_delta_for_slot(pkt, len, pid, &delta)) {
        if (packet_has_bad_joy_for_slot(pkt, len, pid)) {
          clients[slot].transport.drop_bad_joy++;
          global_transport->drop_bad_joy++;
        }
        if (debug) {
          printf("DROP DELTA slot=%d bad-len=%zu", slot, len);
          for (size_t i = 0; i < len; i++) {
            printf("%s%02X", (i == 0) ? " bytes=[" : " ",
                   (unsigned)pkt[i]);
          }
          printf("%s expected-pid=%u\n", (len > 0) ? "]" : "",
                 (unsigned)pid);
        }
        return;
      }
      transport_stats_note_delta(&clients[slot].transport, delta.format);
      transport_stats_note_delta(global_transport, delta.format);
      if (!delta_seq_is_fresh(&clients[slot], delta.seq)) {
        clients[slot].transport.drop_stale_seq++;
        global_transport->drop_stale_seq++;
        if (debug) {
          printf("DROP DELTA slot=%d stale-seq=%u", slot,
                 (unsigned)delta.seq);
          for (size_t i = 0; i < len; i++) {
            printf("%s%02X", (i == 0) ? " bytes=[" : " ",
                   (unsigned)pkt[i]);
          }
          printf("%s\n", (len > 0) ? "]" : "");
        }
        return;
      }
      if (debug) {
        printf("transport accepted slot=%d format=%s seq=%u joy=%02X\n",
               slot, transport_delta_format_name(delta.format),
               (unsigned)delta.seq, (unsigned)delta.joy);
      }
      {
        struct client_slot *c = &clients[slot];
        uint8_t last = (uint8_t)((c->input_head + c->input_count +
                                  INPUT_QUEUE_MAX - 1) % INPUT_QUEUE_MAX);
        /* Idle keepalives fold; real intent never does.

           A neutral costs the sender no predicted cell, so collapsing a run of
           them into the entry already waiting loses no movement, and advancing
           that entry's sequence acks only inputs that were always going to
           move nothing. It also keeps a stream of keepalives from queueing
           ahead of a genuine turn.

           A directional repeat is the opposite: the client predicted a cell
           for it. Folding one used to advance the queued entry's sequence too,
           so applying that single entry acked every input folded into it. The
           client discards its pending ring up to the ack, so it threw away
           inputs it had already moved for and kept the cells -- a permanent
           one-cell divergence per fold, with nothing left pending to reveal
           it. On hardware that read as a one-cell perpendicular offset, the
           server turning a cell late, plus two to three cells of along-track
           lag: the corner-turn snap. */
        int foldable = (c->input_count > 0 && c->input_q[last].joy == delta.joy &&
                        (delta.joy & 0x1F) == 0x0F);
        if (foldable) {
          c->input_q[last].seq = delta.seq;
        } else if (c->input_count < INPUT_QUEUE_MAX) {
          uint8_t tail = (uint8_t)((c->input_head + c->input_count) %
                                   INPUT_QUEUE_MAX);
          c->input_q[tail].seq = delta.seq;
          c->input_q[tail].joy = delta.joy;
          c->input_q[tail].ready_at_ms = now + (uint64_t)lag_ms;
          c->input_count++;
        } else if (debug) {
          /* Not acked: the client keeps it pending and replays it. */
          printf("DROP DELTA slot=%d queue-full seq=%u\n", slot,
                 (unsigned)delta.seq);
        }
      }
      last_input_ms[pid] = now;
      clients[slot].transport.accepted_delta++;
      global_transport->accepted_delta++;
      if (debug) {
        printf("DELTA slot=%d pid=%u ", slot, pid);
        debug_joy(delta.joy);
      }
    }
    return;
  }

  if (len == 3 + NAME_LEN && pkt[0] == PKT_NAME) {
    /* The sender's slot is authoritative; pkt[2] is ignored so a client cannot
       rename anyone else. */
    sanitize_name(&pkt[3], clients[slot].name);
    uint8_t out[3 + NAME_LEN];
    build_name((*seq)++, (uint8_t)slot, clients[slot].name, out, sizeof(out));
    broadcast_packet(sock, clients, out, sizeof(out));
    if (debug) {
      printf("NAME slot=%d name=\"%.*s\"\n", slot, NAME_LEN,
             (const char *)clients[slot].name);
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
      queue_brick_echo(x, y);
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
                                 uint64_t now, uint64_t *last_input_ms,
                                 struct transport_counters *global_transport,
                                 int lag_ms) {
  struct client_slot *c = &clients[slot];
  for (size_t i = 0; i < n; i++) {
    uint8_t pkt[16]; /* NAME is the longest inbound packet at 11 bytes */
    size_t pkt_len = 0;
    enum transport_rx_result result =
        transport_rx_push_byte(&c->rx, (uint8_t)slot, buf[i], pkt,
                               sizeof(pkt), &pkt_len);
    uint32_t resyncs = transport_rx_take_resync_count(&c->rx);
    if (resyncs > 0) {
      c->transport.delta_resync += resyncs;
      global_transport->delta_resync += resyncs;
    }
    if (result == TRANSPORT_RX_PACKET && pkt_len > 0) {
      handle_client_packet(slot, pkt, pkt_len, players, brick_bits, sock,
                           clients, seq, debug, now, last_input_ms,
                           global_transport, lag_ms);
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
  players[idx].zombie_fire_pending = 0;

  // Shoot if any player is in same row
  for (int p = 0; p < MAX_PLAYERS; p++) {
    if (!human_mask[p] || p == idx || players[p].respawn_at_ms != 0) {
      continue;
    }
    if (players[p].y == zy &&
        clear_row_shot(bricks, (int)zy, (int)zx, (int)players[p].x)) {
      uint8_t dir = (players[p].x > zx) ? 0 : 2;
      players[idx].joy = stick_from_dir(dir);
      players[idx].zombie_fire_pending = 1;
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
      players[idx].joy = stick_from_dir(dir);
      players[idx].zombie_fire_pending = 1;
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
    players[idx].joy = stick_from_dir(dir_y);
    players[idx].zombie_fire_pending = 1;
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
  players[idx].joy = stick_from_dir(dir_x);
  players[idx].zombie_fire_pending = 1;
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
                       uint8_t joy, int debug) {
  if (players[shooter].respawn_at_ms != 0) {
    return;
  }
  if (shots[shooter].active) {
    return;
  }
  uint8_t stick = (uint8_t)(joy & 0x0F);
  int trig = (joy & 0x10) != 0;
  int dx = 0;
  int dy = 0;
  if (!trig || !stick_to_cardinal_delta(stick, &dx, &dy)) {
    return;
  }
  if (debug) {
    char detail[96];
    snprintf(detail, sizeof(detail), "pos=%u,%u joy=%02X dir=%d,%d",
             (unsigned)players[shooter].x, (unsigned)players[shooter].y,
             (unsigned)joy, dx, dy);
    debug_combat_order(debug, "fire-eval", shooter, detail);
  }
  int sx = (int)players[shooter].x + dx;
  int sy = (int)players[shooter].y + dy;
  if (is_brick(bricks, sx, sy)) {
    if (!is_outer_wall_cell(sx, sy)) {
      clear_brick(bricks, sx, sy);
      queue_brick_echo((uint8_t)sx, (uint8_t)sy);
      uint8_t pkt[4];
      build_brick_delta((*seq)++, (uint8_t)sx, (uint8_t)sy, pkt, sizeof(pkt));
      broadcast_packet(sock, clients, pkt, sizeof(pkt));
      if (debug) {
        printf("TX brick_delta x=%u y=%u\n", pkt[2], pkt[3]);
        {
          char detail[96];
          snprintf(detail, sizeof(detail), "phase=fire-eval x=%d y=%d", sx, sy);
          debug_combat_event(debug, "brick-break", shooter, detail);
        }
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
          {
            char detail[96];
            snprintf(detail, sizeof(detail), "victim=%d x=%d y=%d", p, sx, sy);
            debug_combat_event(debug, "immediate-hit", shooter, detail);
          }
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
  if (debug) {
    char detail[96];
    snprintf(detail, sizeof(detail), "x=%d y=%d dir=%d,%d", sx, sy, dx, dy);
    debug_combat_event(debug, "shot-spawn", shooter, detail);
  }
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
        queue_brick_echo((uint8_t)nx, (uint8_t)ny);
        uint8_t pkt[4];
        build_brick_delta((*seq)++, (uint8_t)nx, (uint8_t)ny, pkt, sizeof(pkt));
        broadcast_packet(sock, clients, pkt, sizeof(pkt));
        if (debug) {
          printf("TX brick_delta x=%u y=%u\n", pkt[2], pkt[3]);
          {
            char detail[96];
            snprintf(detail, sizeof(detail), "phase=shot-step x=%d y=%d", nx, ny);
            debug_combat_event(debug, "brick-break", i, detail);
          }
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
          {
            char detail[96];
            snprintf(detail, sizeof(detail), "victim=%d x=%d y=%d", p, nx, ny);
            debug_combat_event(debug, "moving-hit", i, detail);
          }
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
      if (debug) {
        char detail[96];
        snprintf(detail, sizeof(detail), "x=%d y=%d dir=%d,%d",
                 nx, ny, (int)shots[i].dx, (int)shots[i].dy);
        debug_combat_order(debug, "shot-step", i, detail);
      }
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

/* Take one queued input per client per tick, in the order the client sent it,
   and acknowledge exactly what was applied and nothing more. The ack is what
   the client's pending-input ring trusts when deciding what it may discard, so
   reporting an input as applied when it was not is what produced the snap-back
   on a fast corner turn. */
static void apply_queued_input(struct client_slot *clients,
                               struct player_state *players, int debug,
                               uint64_t now) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    if (clients[i].input_count > 0 &&
        now >= clients[i].input_q[clients[i].input_head].ready_at_ms) {
      uint8_t head = clients[i].input_head;
      players[i].joy = clients[i].input_q[head].joy;
      clients[i].applied_input_seq = clients[i].input_q[head].seq;
      clients[i].have_applied_input_seq = 1;
      clients[i].input_head = (uint8_t)((head + 1) % INPUT_QUEUE_MAX);
      clients[i].input_count--;
      if (debug) {
        printf("input apply slot=%d seq=%u joy=%02X queued=%u\n", i,
               (unsigned)clients[i].applied_input_seq,
               (unsigned)players[i].joy, (unsigned)clients[i].input_count);
      }
      continue;
    }
    /* Nothing queued: stand still. Repeating the last applied direction to
       cover a late packet walked the actor a cell the client never predicted,
       and the client had already been acked for everything it sent, so that
       cell was never reconciled -- it became a permanent offset along the
       direction of travel. On hardware that showed up as shots leaving from a
       row the player was not standing on, then a snap onto that row once
       firing stopped, because ACTFLAG's shot bits suppress the reconcile while
       the trigger is held. A missing input is a lost packet: it goes unacked,
       stays in the client's pending ring and is replayed. */
    players[i].joy = 0x0F;
  }
}

static void step_players(struct player_state *players, struct shot_state *shots,
                         uint8_t *bricks, int sock,
                         struct client_slot *clients, uint8_t *seq,
                         int debug, int zombies,
                         const uint64_t *last_input_ms) {
  /* Same-tick authoritative combat/world order:
   * 1. Finalize expired respawns.
   * 2. Select authoritative joy/facing for this tick.
   * 3. Evaluate fire from the current authoritative actor position.
   * 4. If trigger+directional fire is present, suppress same-tick movement.
   * 5. Otherwise apply one authoritative movement step.
   * 6. Step active shots and publish any hit/brick/clear outcomes.
   */
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
      players[i].joy = 0x0F;
      players[i].zombie_fire_pending = 0;
      players[i].zombie_think_next_ms = now;
      players[i].zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
      players[i].zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
      uint8_t pkt[6];
      build_respawn((*seq)++, (uint8_t)i, sx, sy, 0x03, pkt, sizeof(pkt));
      broadcast_packet(sock, clients, pkt, sizeof(pkt));
      if (debug) {
        printf("TX respawn pid=%u x=%u y=%u\n", (unsigned)i, sx, sy);
        {
          char detail[96];
          snprintf(detail, sizeof(detail), "x=%u y=%u",
                   (unsigned)sx, (unsigned)sy);
          debug_combat_order(debug, "respawn-finalize", i, detail);
        }
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
    uint8_t action_joy = players[i].joy;
    int can_move = can_act;
    if (zombie_mask[i] && can_act) {
      if (now >= players[i].zombie_think_next_ms) {
        zombie_ai(i, players, bricks, human_mask);
        players[i].zombie_think_next_ms = now + ZOMBIE_THINK_MS;
      }
      action_joy = players[i].joy;
      if (players[i].zombie_fire_pending &&
          now >= players[i].zombie_fire_next_ms) {
        action_joy |= 0x10;
        players[i].zombie_fire_pending = 0;
        players[i].zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
      }
      if (now < players[i].zombie_move_next_ms) {
        can_move = 0;
      }
    }
    uint8_t stick = (uint8_t)(action_joy & 0x0F);
    int trig = (action_joy & 0x10) != 0;
    if (can_act) {
      start_shot(i, players, shots, bricks, sock, clients, seq, action_joy,
                 debug);
      if (can_move && !(trig && stick != 0x0F)) {
        uint8_t before_x = players[i].x;
        uint8_t before_y = players[i].y;
        apply_move_if_free(&players[i], bricks, players, i);
        if (debug) {
          char detail[96];
          if (players[i].x != before_x || players[i].y != before_y) {
            snprintf(detail, sizeof(detail), "to=%u,%u",
                     (unsigned)players[i].x, (unsigned)players[i].y);
            debug_combat_order(debug, "move-apply", i, detail);
          } else {
            snprintf(detail, sizeof(detail), "at=%u,%u",
                     (unsigned)before_x, (unsigned)before_y);
            debug_combat_order(debug, "move-blocked", i, detail);
          }
        }
        if (zombie_mask[i]) {
          players[i].zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
        }
      } else if (debug && can_move && trig && stick != 0x0F) {
        char detail[96];
        snprintf(detail, sizeof(detail), "reason=directional-fire pos=%u,%u joy=%02X",
                 (unsigned)players[i].x, (unsigned)players[i].y,
                 (unsigned)action_joy);
        debug_combat_order(debug, "move-gate", i, detail);
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
          "Usage: %s [--port PORT] [--bind ADDR] [--tick-hz N] [--zombies N] [--brick PATH] [--lag-ms N] [--debug]\n"
          "  --bind ADDR  bind a specific address instead of all interfaces.\n"
          "               When FujiNet-PC runs on this host it wants the same\n"
          "               netstream port. Start this server first and it keeps\n"
          "               the port; otherwise bind a loopback alias the client\n"
          "               targets directly, e.g. --bind 127.0.0.2\n",
          argv0);
}

int main(int argc, char **argv) {
  int port = 9000;
  int tick_hz = 10;
  int debug = 0;
  int zombies = 1;
  int lag_ms = 0;
  const char *brick_path = "server/brick_layout.txt";
  const char *bind_addr = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      bind_addr = argv[++i];
    } else if (strcmp(argv[i], "--tick-hz") == 0 && i + 1 < argc) {
      tick_hz = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--zombies") == 0 && i + 1 < argc) {
      zombies = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--brick") == 0 && i + 1 < argc) {
      brick_path = argv[++i];
    } else if (strcmp(argv[i], "--lag-ms") == 0 && i + 1 < argc) {
      /* Test aid: hold each input this long before applying it, so a local
         run reproduces the pending-input backlog a real Atari always has.
         At zero latency the client's reposition-and-replay path barely runs,
         which hides bugs in it. */
      lag_ms = atoi(argv[++i]);
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

  /* Deliberately no SO_REUSEADDR: UDP has no TIME_WAIT to work around, and
     without it the kernel refuses a second bind to this address/port. That
     matters because FujiNet-PC's netstream also binds the destination port
     locally, and when both sockets are allowed to share it the client's
     datagrams are silently swallowed instead of reaching the game. Failing
     the bind is what turns that into a visible error. */

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);
  if (bind_addr != NULL) {
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
      fprintf(stderr, "Invalid --bind address: %s\n", bind_addr);
      close(sock);
      return 1;
    }
  }

  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    fprintf(stderr,
            "Could not bind %s:%d. Another process already holds that port -- "
            "on a host also running FujiNet-PC this is usually its netstream "
            "socket. Start the server before the client opens a stream, or "
            "bind a loopback alias the client targets directly "
            "(--bind 127.0.0.2).\n",
            bind_addr ? bind_addr : "0.0.0.0", port);
    close(sock);
    return 1;
  }

  struct client_slot clients[MAX_PLAYERS];
  struct player_state players[MAX_PLAYERS];
  struct shot_state shots[MAX_PLAYERS];
  uint64_t last_input_ms[MAX_PLAYERS];
  struct transport_counters global_transport;
  memset(clients, 0, sizeof(clients));
  memset(players, 0, sizeof(players));
  memset(shots, 0, sizeof(shots));
  memset(last_input_ms, 0, sizeof(last_input_ms));
  memset(&global_transport, 0, sizeof(global_transport));
  uint8_t brick_bits[48];
  if (load_brick_layout(brick_path, brick_bits, sizeof(brick_bits)) != 0) {
    memset(brick_bits, 0, sizeof(brick_bits));
    fprintf(stderr, "Warning: failed to load brick layout: %s\n", brick_path);
  }
  srand((unsigned int)time(NULL));
  uint64_t init_now = now_ms();
  for (int i = 0; i < MAX_PLAYERS; i++) {
    uint8_t sx = 0, sy = 0;
    pick_spawn(brick_bits, players, &sx, &sy);
    players[i].x = sx;
    players[i].y = sy;
    players[i].joy = 0x0F;
    players[i].zombie_fire_pending = 0;
    players[i].zombie_think_next_ms = init_now;
    players[i].zombie_move_next_ms = init_now + ZOMBIE_MOVE_MS;
    players[i].zombie_fire_next_ms = init_now + ZOMBIE_FIRE_MS;
  }

  uint8_t seq = 0;
  uint64_t next_tick = now_ms();
  uint64_t last_transport_summary_ms = now_ms();
  uint64_t last_brick_resync_ms = now_ms();
  uint64_t last_name_rotate_ms = now_ms();
  uint64_t last_seat_ms = 0;
  uint8_t last_seat_mask = 0xFF; /* never a valid mask: forces a first send */
  int name_rotate = 0;
  memset(g_brick_echo, 0, sizeof(g_brick_echo));
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
          /* Clear the slot before the newcomer is told about the world, so the
             brick/snapshot state it receives already describes its own actor
             and not the zombie it just displaced. */
          reset_slot_gameplay(slot, players, shots, last_input_ms, now);
          uint8_t bfull[51];
          build_brick_full(seq++, brick_bits, bfull, sizeof(bfull));
          send_checked(sock, (struct sockaddr *)&clients[slot].addr,
                       clients[slot].addr_len, bfull, sizeof(bfull));
          clients[slot].sent_bricks = 1;
          if (debug) {
            printf("TX brick_full -> slot %d\n", slot);
          }
        }
        if (slot >= 0) {
          transport_stats_note_raw_bytes(&clients[slot].transport, (size_t)n);
          transport_stats_note_raw_bytes(&global_transport, (size_t)n);
          process_client_bytes(slot, buf, (size_t)n, players, brick_bits, sock,
                               clients, &seq, debug, now,
                               last_input_ms, &global_transport, lag_ms);
        }
      }
    }

    if (now_ms() - last_name_rotate_ms >= NAME_ROTATE_MS) {
      last_name_rotate_ms = now_ms();
      broadcast_next_name(sock, clients, &seq, &name_rotate);
    }

    if (now_ms() - last_brick_resync_ms >= BRICK_RESYNC_MS) {
      last_brick_resync_ms = now_ms();
      uint8_t bfull[51];
      build_brick_full(seq++, brick_bits, bfull, sizeof(bfull));
      broadcast_packet(sock, clients, bfull, sizeof(bfull));
      if (debug) {
        printf("TX brick_full resync -> all clients\n");
      }
    }

    reap_timed_out_clients(clients, now_ms(), debug, players, shots,
                           last_input_ms);

    /* After the reap, so a timed-out seat is reported free on the same pass
       that frees it. */
    {
      uint8_t seat_mask = compute_seat_mask(clients);
      if (seat_mask != last_seat_mask ||
          now_ms() - last_seat_ms >= SEAT_REPEAT_MS) {
        last_seat_mask = seat_mask;
        last_seat_ms = now_ms();
        uint8_t pkt[3];
        build_seats(seq++, seat_mask, pkt, sizeof(pkt));
        broadcast_packet(sock, clients, pkt, sizeof(pkt));
        if (debug) {
          printf("TX seats mask=%X -> all clients\n", seat_mask);
        }
      }
    }
    if (debug && now_ms() - last_transport_summary_ms >= TRANSPORT_SUMMARY_MS) {
      log_transport_summaries(clients, &global_transport);
      last_transport_summary_ms = now_ms();
    }

    now = now_ms();
    if (now >= next_tick) {
      /* Before the step, so an echo never shares a tick with the break that
         produced it: one brick packet per tick, never two. */
      flush_brick_echo(sock, clients, &seq, debug);
      /* Sets this tick's authoritative joy and the ack that goes with it. */
      apply_queued_input(clients, players, debug, now);
      step_players(players, shots, brick_bits, sock, clients, &seq, debug,
                   zombies, last_input_ms);
      uint8_t zombie_mask[MAX_PLAYERS];
      uint8_t zombie_bits = 0;
      uint8_t snapshot_seq = seq++;
      compute_zombie_mask(clients, zombies, zombie_mask);
      for (int z = 0; z < MAX_PLAYERS; z++) {
        if (zombie_mask[z]) {
          zombie_bits |= (uint8_t)(1u << z);
        }
      }
      uint8_t pkt[20];
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!clients[i].in_use) {
          continue;
        }
        build_snapshot(snapshot_seq, players, clients[i].applied_input_seq, pkt,
                       sizeof(pkt));
        // flags: bit0 valid, bits1..2 recipient pid, bits3..6 zombie-slot mask.
        pkt[2] = (uint8_t)(0x01u | ((uint8_t)i << 1) |
                           ((uint8_t)(zombie_bits & 0x0Fu) << 3));
        if (clients[i].have_applied_input_seq) {
          pkt[2] |= 0x80u;
        }
        ssize_t wn = send_checked(sock, (struct sockaddr *)&clients[i].addr,
                                  clients[i].addr_len, pkt, sizeof(pkt));
        if (debug && wn == (ssize_t)sizeof(pkt)) {
          printf("TX snapshot -> slot %d\n", i);
        }
      }
      next_tick = now + tick_ms;
    }
  }

  if (debug) {
    log_transport_summaries(clients, &global_transport);
  }
  close(sock);
  if (debug) {
    puts("server stopped");
  }
  return 0;
}
