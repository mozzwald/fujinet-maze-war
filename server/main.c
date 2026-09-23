#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../net/tcp_stream.h"
#include "transport_stats.h"
#include "transport_normalize.h"

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
  PKT_ROUND_START = 0x55,
  PKT_LEAVE_ROOM = 0x56,
  PKT_LEAVE_ACK = 0x57
};

enum { MAX_PLAYERS = 4, MAX_ROOMS = 64, MAX_DEPARTING = MAX_PLAYERS };
/* Keep a silence timeout for a vanished SIO peer even when TCP stays open. */
enum {
  CLIENT_TIMEOUT_MS = 15000,
  CLIENT_HANDSHAKE_MS = 3000,
  DEFAULT_INTERMISSION_MS = 15000,
  DEFAULT_NO_HUMAN_GRACE_MS = 60000,
  LEAVE_DRAIN_MS = 1000
};
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
/* RESPAWN hides an actor (pending) and un-hides it somewhere else (final), and
   it was the one transition packet still sent exactly once. Every other
   once-only packet in this server was given repeats for the same reason: a
   SHOT clear bursts three times, a BRICK_DELTA echoes, NAME rotates, the map
   resyncs. Losing a pending RESPAWN leaves the victim's wizard standing at the
   cell it died on until the final spawn moves it two seconds later -- with a
   hole in it where the killing shot's own clear blanked the characters it had
   drawn over. Losing a final RESPAWN is worse: the client keeps that actor
   hidden until the next death, because only an explicit final spawn clears the
   hide. Neither shows up on loopback, which never drops a packet. */
enum { RESPAWN_ECHO_REPEATS = 2 };
/* Display name length. The Atari HUD gives each slot columns 4..11 of its
   20-column line before the score digit at column 15, so 8 is what fits. */
#define NAME_LEN 8
enum {
  PROTOCOL_VERSION = 1,
  SNAPSHOT_LEN = 21,
  DELTA_LEN = 5,
  SHOT_LEN = 7,
  BRICK_FULL_LEN = 52,
  BRICK_DELTA_LEN = 5,
  RESPAWN_LEN = 7,
  MATCH_END_LEN = 43,
  ROUND_START_LEN = 3,
  RELIABLE_QUEUE_MAX = 24,
  RELIABLE_EVENT_MAX = 52,
  RELIABLE_PKT_MAX = 4 + RELIABLE_EVENT_MAX,
  RELIABLE_RESEND_MS = 300,
  RELIABLE_FAST_MS = 75
};

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
  int fd; /* -1 when free, initialized explicitly at startup and reset */
  struct tcp_tx tx;
  struct tcp_frame_rx frame_rx;
  uint64_t connected_ms;
  int received_packet;
  int handshake_ok;
  /* All-zero means unnamed: the client never sent a NAME, or the slot changed
     hands. Clients fall back to their WIZARD/ZOMBIE label in that case. */
  uint8_t name[NAME_LEN];
  /* Kept for logging (the peer address accept() handed back) and for
     nothing else -- a TCP client's identity is its fd, not its address, so
     nothing here is ever matched against an incoming address the way the UDP
     server's addr_equal() used to. */
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
  uint16_t reliable_next_rev;
  uint16_t reliable_acked_rev;
  uint16_t reliable_sent_rev;
  uint64_t reliable_last_send_ms;
  uint64_t reliable_last_fast_ms;
  struct {
    uint16_t rev;
    uint8_t pkt[RELIABLE_PKT_MAX];
    uint8_t len;
    uint8_t retries;
  } reliable_q[RELIABLE_QUEUE_MAX];
  uint8_t reliable_head;
  uint8_t reliable_count;
  uint8_t leave_requested;
  uint8_t leave_seq;
};

/* A voluntary leave releases its gameplay seat before the TCP acknowledgement
   necessarily reaches the kernel. Keep only the transport pieces required to
   drain that ACK. This object has no seat number and can never mutate a room,
   so a retry from an old socket cannot be attributed to a new occupant. */
struct departing_connection {
  int in_use;
  int fd;
  struct tcp_tx tx;
  struct tcp_frame_rx frame_rx;
  uint8_t leave_seq;
  uint64_t deadline_ms;
};

struct room_config {
  int port;
  int zombies;
  int tick_hz;
  int lag_ms;
  int kill_limit;
  int intermission_ms;
  int no_human_grace_ms;
  const char *brick_path;
};

struct brick_echo {
  uint8_t x;
  uint8_t y;
  uint8_t left;
};

struct respawn_echo {
  uint8_t pkt[RESPAWN_LEN];
  uint8_t left;
};

/* Everything that can differ between listeners lives here. Keeping this
   allocation off the stack also gives later round/grace work one explicit
   ownership boundary instead of another set of process globals. */
struct room {
  struct room_config config;
  int listener_fd;
  struct client_slot clients[MAX_PLAYERS];
  struct departing_connection departing[MAX_DEPARTING];
  struct player_state players[MAX_PLAYERS];
  struct shot_state shots[MAX_PLAYERS];
  uint8_t brick_bits[48];
  uint8_t brick_reset_bits[48];
  uint64_t last_input_ms[MAX_PLAYERS];
  struct transport_counters global_transport;
  struct brick_echo brick_echo[BRICK_ECHO_MAX];
  struct respawn_echo respawn_echo[MAX_PLAYERS];
  uint8_t occupied_mask;
  uint8_t seq;
  uint64_t tick_ms;
  uint64_t next_tick;
  uint64_t last_transport_summary_ms;
  uint64_t last_brick_resync_ms;
  uint64_t last_name_rotate_ms;
  uint64_t last_seat_ms;
  uint8_t last_seat_mask;
  int name_rotate;
  uint8_t round_id;
  uint8_t round_state;
  uint8_t historical_zombie_mask;
  uint8_t final_active_mask;
  uint8_t final_zombie_mask;
  uint8_t winner_pid;
  uint8_t final_scores[MAX_PLAYERS];
  uint8_t frozen_names[MAX_PLAYERS][NAME_LEN];
  uint8_t frozen_match[MATCH_END_LEN];
  uint64_t intermission_deadline_ms;
  uint64_t no_human_deadline_ms;
};

enum { ROUND_PLAYING = 0, ROUND_OVER = 1, ROUND_DORMANT = 2 };

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
static void reset_slot_gameplay(struct room *room, int slot, uint64_t now);
static void build_brick_full(uint8_t seq, const uint8_t *bits,
                             uint8_t round_id, uint8_t *out, size_t out_len);
static void send_round_state(struct room *room, int slot, uint64_t now,
                             int debug);
static void enter_round_over(struct room *room, int winner, uint64_t now,
                             int debug);
static void reset_round(struct room *room, uint64_t now, int debug);
static void enter_dormant(struct room *room, uint64_t now, int debug,
                          const char *reason);
static void wake_dormant_room(struct room *room, uint64_t now);

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

/* Find a slot for a just-accepted TCP connection. Unlike the old UDP
   find_or_add_client(), there is no "is this the same client sending again"
   lookup here: a TCP accept() is unconditionally a new connection with its
   own fd, so every call to this function is the "allocate a fresh slot"
   case. Same two-pass preference as before -- a free non-zombie slot first,
   any free slot otherwise -- so --zombies N still shrinks only once the free
   slots genuinely run out. */
static int alloc_client_slot(const struct client_slot *clients, int zombies) {
  uint8_t zombie_mask[MAX_PLAYERS];
  compute_zombie_mask(clients, zombies, zombie_mask);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use && !zombie_mask[i]) {
      return i;
    }
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      return i;
    }
  }
  return -1;
}

/* Populate a freshly allocated slot for an accepted connection. Mirrors what
   the old UDP find_or_add_client() set up for a new address. */
static void init_client_slot(struct client_slot *client, int fd,
                             const struct sockaddr_in *addr,
                             socklen_t addr_len, uint64_t now) {
  client->in_use = 1;
  client->fd = fd;
  client->addr = *addr;
  client->addr_len = addr_len;
  client->last_seen_ms = now;
  client->connected_ms = now;
  client->sent_bricks = 0;
  client->handshake_ok = 0;
  client->have_delta_seq = 0;
  client->last_delta_seq = 0;
  client->have_applied_input_seq = 0;
  client->applied_input_seq = 0;
  client->input_head = 0;
  client->input_count = 0;
  client->reliable_next_rev = 1;
  client->reliable_acked_rev = 0;
  client->reliable_sent_rev = 0;
  client->reliable_last_send_ms = 0;
  client->reliable_last_fast_ms = 0;
  client->reliable_head = 0;
  client->reliable_count = 0;
  memset(client->reliable_q, 0, sizeof(client->reliable_q));
  memset(client->name, 0, NAME_LEN); /* the seat's previous occupant */
}

/* Closes the fd (if any) and zeroes everything else, leaving the slot ready
   for reuse. -1 is the only valid "no fd" value for this struct (see the
   field comment on client_slot.fd), so this is the one place that writes it
   outside of init_client_slot(). */
static void reset_client_slot(struct client_slot *client) {
  if (!client) {
    return;
  }
  if (client->fd >= 0) {
    close(client->fd);
  }
  memset(client, 0, sizeof(*client));
  client->fd = -1;
}

static int room_human_count(const struct room *room) {
  int count = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (room->clients[i].in_use && room->clients[i].handshake_ok) count++;
  }
  return count;
}

static void begin_no_human_grace(struct room *room, uint64_t now, int debug) {
  if (room->no_human_deadline_ms != 0 || room->round_state == ROUND_DORMANT) {
    return;
  }
  room->no_human_deadline_ms =
      now + (uint64_t)room->config.no_human_grace_ms;
  /* No human remains to own or be hit by an in-flight projectile. Clearing
     shots prevents a backfilled Zombie from inheriting pending human combat
     and prevents Zombie-versus-Zombie scoring during the grace window. */
  memset(room->shots, 0, sizeof(room->shots));
  if (room->round_state == ROUND_OVER) {
    /* Nobody can observe an intermission. Start the next canonical round now,
       while preserving the independently measured grace deadline. */
    reset_round(room, now, debug);
  }
  printf("room=%d no-human grace started grace_ms=%d\n", room->config.port,
         room->config.no_human_grace_ms);
}

/* Unexpected EOF, error, and silence timeout all use this path. A connection
   which never completed HELLO owned no human seat and cannot start grace. */
static void drop_client(struct room *room, int i, int debug, uint64_t now) {
  struct client_slot *clients = room->clients;
  int was_human = clients[i].handshake_ok;
  log_client_event("disconnected", i, &clients[i].addr);
  if (debug) {
    log_transport_summary_if_nonzero(i, &clients[i].transport);
  }
  reset_client_slot(&clients[i]);
  reset_slot_gameplay(room, i, now);
  if (was_human && room_human_count(room) == 0) {
    begin_no_human_grace(room, now, debug);
  }
}

static void reset_departing(struct departing_connection *departing) {
  if (departing->fd >= 0) close(departing->fd);
  memset(departing, 0, sizeof(*departing));
  departing->fd = -1;
}

/* Move a cleanly leaving connection out of its reusable game seat. The ACK is
   already queued on client->tx. No gameplay state survives this handoff. */
static void detach_voluntary_client(struct room *room, int slot, int debug,
                                    uint64_t now) {
  struct client_slot *client = &room->clients[slot];
  int fd = client->fd;
  struct tcp_tx tx = client->tx;
  struct tcp_frame_rx frame_rx = client->frame_rx;
  uint8_t leave_seq = client->leave_seq;
  log_client_event("left", slot, &client->addr);
  if (debug) log_transport_summary_if_nonzero(slot, &client->transport);

  memset(client, 0, sizeof(*client));
  client->fd = -1;
  reset_slot_gameplay(room, slot, now);

  int departing_slot = -1;
  for (int i = 0; i < MAX_DEPARTING; i++) {
    if (!room->departing[i].in_use) {
      departing_slot = i;
      break;
    }
  }
  if (departing_slot >= 0) {
    struct departing_connection *departing = &room->departing[departing_slot];
    departing->in_use = 1;
    departing->fd = fd;
    departing->tx = tx;
    departing->frame_rx = frame_rx;
    departing->leave_seq = leave_seq;
    departing->deadline_ms = now + LEAVE_DRAIN_MS;
  } else {
    close(fd); /* bounded fd ownership even under a leave flood */
  }

  if (room_human_count(room) == 0) {
    enter_dormant(room, now, debug, "voluntary-final-leave");
  }
}

static void reap_timed_out_clients(struct room *room, uint64_t now, int debug) {
  struct client_slot *clients = room->clients;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use) {
      continue;
    }
    int handshake_alive = !clients[i].handshake_ok &&
                          now - clients[i].connected_ms < CLIENT_HANDSHAKE_MS;
    int session_alive = clients[i].handshake_ok &&
                        now - clients[i].last_seen_ms < CLIENT_TIMEOUT_MS;
    if (!clients[i].tx.failed && (handshake_alive || session_alive)) {
      continue;
    }
    drop_client(room, i, debug, now);
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
                           uint8_t ack_seq, uint8_t round_id,
                           uint8_t *out, size_t out_len) {
  if (out_len < SNAPSHOT_LEN) {
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
  out[20] = round_id;
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
    if (clients[i].in_use && clients[i].handshake_ok) {
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
                              uint8_t round_id,
                              uint8_t *out, size_t out_len);
static void broadcast_packet(int sock, struct client_slot *clients,
                             const uint8_t *pkt, size_t len);
static ssize_t send_checked(struct client_slot *client,
                            const uint8_t *pkt, size_t len);
static void broadcast_reliable_event(struct client_slot *clients,
                                     const uint8_t *event, size_t event_len,
                                     uint8_t *seq, uint64_t now, int debug);
static void reliable_tick(struct client_slot *clients, uint64_t now, int debug);

/* One pending echo per slot: a later RESPAWN for a slot supersedes an earlier
   one, so a final spawn cannot be trailed by a stale hide. */
static void queue_respawn_echo(struct room *room, const uint8_t *pkt) {
  uint8_t pid = pkt[2];
  if (pid >= MAX_PLAYERS) {
    return;
  }
  memcpy(room->respawn_echo[pid].pkt, pkt, RESPAWN_LEN);
  room->respawn_echo[pid].left = RESPAWN_ECHO_REPEATS;
}

/* At most one echo per tick, like the brick echo: a burst is what loses
   packets in the first place. The sequence byte is re-stamped so the repeat is
   a fresh frame rather than a duplicate of one the client may have dropped. */
static void flush_respawn_echo(struct room *room, int debug) {
  struct client_slot *clients = room->clients;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (room->respawn_echo[i].left == 0) {
      continue;
    }
    room->respawn_echo[i].left--;
    uint8_t pkt[RESPAWN_LEN];
    memcpy(pkt, room->respawn_echo[i].pkt, RESPAWN_LEN);
    pkt[1] = room->seq++;
    broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
    if (debug) {
      printf("TX respawn echo pid=%u flags=%02X\n", (unsigned)pkt[2],
             (unsigned)pkt[5]);
    }
    return;
  }
}

static void queue_brick_echo(struct room *room, uint8_t x, uint8_t y) {
  int spare = -1;
  for (int i = 0; i < BRICK_ECHO_MAX; i++) {
    if (room->brick_echo[i].left > 0 && room->brick_echo[i].x == x &&
        room->brick_echo[i].y == y) {
      room->brick_echo[i].left = BRICK_ECHO_REPEATS;
      return;
    }
    if (spare < 0 && room->brick_echo[i].left == 0) {
      spare = i;
    }
  }
  if (spare < 0) {
    spare = 0; /* full: the oldest loses its echo, the resync still covers it */
  }
  room->brick_echo[spare].x = x;
  room->brick_echo[spare].y = y;
  room->brick_echo[spare].left = BRICK_ECHO_REPEATS;
}

/* At most one echo per tick: several at once is the burst that loses packets. */
static void flush_brick_echo(struct room *room, int debug) {
  struct client_slot *clients = room->clients;
  for (int i = 0; i < BRICK_ECHO_MAX; i++) {
    if (room->brick_echo[i].left == 0) {
      continue;
    }
    room->brick_echo[i].left--;
    uint8_t pkt[BRICK_DELTA_LEN];
    build_brick_delta(room->seq++, room->brick_echo[i].x,
                      room->brick_echo[i].y, room->round_id, pkt,
                      sizeof(pkt));
    broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
    if (debug) {
      printf("TX brick_delta echo x=%u y=%u\n", room->brick_echo[i].x,
             room->brick_echo[i].y);
    }
    return;
  }
}

static void build_brick_delta(uint8_t seq, uint8_t x, uint8_t y,
                              uint8_t round_id,
                              uint8_t *out, size_t out_len) {
  if (out_len < BRICK_DELTA_LEN) {
    return;
  }
  out[0] = PKT_BRICK_DELTA;
  out[1] = seq;
  out[2] = x;
  out[3] = y;
  out[4] = round_id;
}

static void build_respawn(uint8_t seq, uint8_t pid, uint8_t x, uint8_t y,
                          uint8_t flags, uint8_t round_id,
                          uint8_t *out, size_t out_len) {
  if (out_len < RESPAWN_LEN) {
    return;
  }
  out[0] = PKT_RESPAWN;
  out[1] = seq;
  out[2] = pid;
  out[3] = x;
  out[4] = y;
  out[5] = flags;
  out[6] = round_id;
}

static void build_shot(uint8_t seq, uint8_t pid, uint8_t x, uint8_t y,
                       uint8_t active, uint8_t round_id,
                       uint8_t *out, size_t out_len) {
  if (out_len < SHOT_LEN) {
    return;
  }
  out[0] = PKT_SHOT;
  out[1] = seq;
  out[2] = pid;
  out[3] = x;
  out[4] = y;
  out[5] = active;
  out[6] = round_id;
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

/* True when slot i is a thing you can walk into or shoot.

   Two ways to be off the board. A player awaiting respawn: its coordinates
   still hold the cell it died in, and clients hide it, so counting it turned
   the death cell into an invisible wall for the whole respawn delay -- and you
   are usually walking straight at someone when you kill them. And an empty
   slot: with `--zombies` below 3 the server still keeps a spawn position for
   slots nobody holds, and those were solid too. Clients draw nothing there, so
   the client walked through while the server refused the move; the drift then
   crossed the reconcile threshold about three cells later and yanked the player
   back. That is the "snap back walking through nothing" report. */
static int slot_on_board(const struct room *room, int i) {
  if (!(room->occupied_mask & (1u << i))) {
    return 0;
  }
  return room->players[i].respawn_at_ms == 0;
}

static int is_player_at(const struct room *room, int x, int y, int ignore_idx) {
  const struct player_state *players = room->players;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == ignore_idx) {
      continue;
    }
    if (!slot_on_board(room, i)) {
      continue;
    }
    if (players[i].x == (uint8_t)x && players[i].y == (uint8_t)y) {
      return 1;
    }
  }
  return 0;
}

static void pick_spawn(const struct room *room, uint8_t *out_x,
                       uint8_t *out_y) {
  const uint8_t *bricks = room->brick_bits;
  for (int tries = 0; tries < 200; tries++) {
    int x = rand() % 20;
    int y = rand() % 19;
    if (!is_brick(bricks, x, y) && !is_player_at(room, x, y, -1)) {
      *out_x = (uint8_t)x;
      *out_y = (uint8_t)y;
      return;
    }
  }
  for (int y = 0; y < 19; y++) {
    for (int x = 0; x < 20; x++) {
      if (!is_brick(bricks, x, y) && !is_player_at(room, x, y, -1)) {
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
static void reset_slot_gameplay(struct room *room, int slot, uint64_t now) {
  struct player_state *players = room->players;
  struct shot_state *shots = room->shots;
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
  room->last_input_ms[slot] = 0;
}

/* Announce one slot's name, cycling a slot per tick.
   Deliberately one packet at a time: sending all four in a burst right behind
   the 51-byte BRICK_FULL made the Atari lose the map every time, because that
   whole group leaves the server as five back-to-back datagrams and the FujiNet
   serial path does not absorb the burst. Spread out, nothing is dropped.
   Empty slots are announced as blank rather than skipped, so a client stops
   showing a name once that player leaves. */
static void broadcast_next_name(struct room *room, int debug) {
  struct client_slot *clients = room->clients;
  uint8_t *seq = &room->seq;
  int *rotate = &room->name_rotate;
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
    send_checked(&clients[t], pkt, sizeof(pkt));
  }
  broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now_ms(), debug);
}

/* Every frame carries a CRC-16/CCITT-FALSE trailer.
   The Atari receives over SIO as a byte stream, so a dropped or duplicated
   byte shifts framing and payload bytes start being read as packet type
   markers. Bounds checks alone let far too much of that through: corrupt
   positions landed actors on the border and erased it, corrupt scores
   flickered, a corrupt brick delta cleared a random cell, and a corrupt
   sequence number parked the client ~100 ticks in the future so every real
   snapshot was dropped as stale for seconds. A CRC makes a misframed
   packet fail closed instead. */
enum { PKT_CKSUM_MAX = 64 };

/* COBS: encode so no zero byte can appear inside a frame, then terminate with
   one. The CRC makes a corrupt frame fail closed, but it cannot realign a
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

static ssize_t send_frame(int fd, struct tcp_tx *tx,
                          const uint8_t *pkt, size_t len) {
  uint8_t raw[PKT_CKSUM_MAX];
  uint8_t buf[PKT_CKSUM_MAX + PKT_CKSUM_MAX / 254 + 2];
  size_t payload_len = len;
  if (len + 2 > sizeof(raw)) {
    return -1;
  }
  memcpy(raw, pkt, len);
  uint16_t crc = crc16_ccitt_false(raw, len);
  raw[len++] = (uint8_t)crc;
  raw[len++] = (uint8_t)(crc >> 8);
  size_t enc = cobs_encode(raw, len, buf);
  buf[enc++] = 0x00; /* frame delimiter */
  int result = tcp_tx_queue(fd, tx, buf, enc);
  /* Report the payload length callers passed in, not the wire length: framing
     is transport, and every call site checks the result against the size of the
     packet it built. */
  return result == 0 ? (ssize_t)payload_len : -1;
}

static ssize_t send_checked(struct client_slot *client,
                            const uint8_t *pkt, size_t len) {
  return send_frame(client->fd, &client->tx, pkt, len);
}

static void broadcast_packet(int sock, struct client_slot *clients,
                             const uint8_t *pkt, size_t len) {
  (void)sock;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use || !clients[i].handshake_ok) {
      continue;
    }
    send_checked(&clients[i], pkt, len);
  }
}

static void reliable_send_from(struct client_slot *client, uint64_t now,
                               const char *reason, int slot, int debug) {
  if (client->reliable_count == 0) {
    return;
  }
  uint8_t idx = client->reliable_head;
  send_checked(client, client->reliable_q[idx].pkt, client->reliable_q[idx].len);
  client->reliable_sent_rev = client->reliable_q[idx].rev;
  client->reliable_q[idx].retries++;
  if (debug) {
    uint16_t rev = client->reliable_q[idx].rev;
    printf("TX reliable %s slot=%d rev=%u type=%02X retry=%u\n", reason,
           slot, (unsigned)rev, (unsigned)client->reliable_q[idx].pkt[4],
           (unsigned)client->reliable_q[idx].retries);
  }
  client->reliable_last_send_ms = now;
}

static void reliable_tick(struct client_slot *clients, uint64_t now, int debug) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!clients[i].in_use || clients[i].reliable_count == 0) {
      continue;
    }
    if (now - clients[i].reliable_last_send_ms >= RELIABLE_RESEND_MS) {
      reliable_send_from(&clients[i], now, "timeout", i, debug);
    }
  }
}

static void reliable_ack(struct client_slot *client, int slot, uint16_t ack,
                         uint64_t now, int debug) {
  if ((uint16_t)(ack - client->reliable_sent_rev) < 0x8000u &&
      ack != client->reliable_sent_rev) {
    if (debug) {
      printf("DROP reliable ACK slot=%d unsent=%u sent=%u\n", slot,
             (unsigned)ack, (unsigned)client->reliable_sent_rev);
    }
    return;
  }
  if (ack == client->reliable_acked_rev &&
      client->reliable_count > 0 &&
      now - client->reliable_last_fast_ms >= RELIABLE_FAST_MS) {
    client->reliable_last_fast_ms = now;
    reliable_send_from(client, now, "fast", slot, debug);
    return;
  }
  while (client->reliable_count > 0) {
    uint8_t idx = client->reliable_head;
    uint16_t rev = client->reliable_q[idx].rev;
    if ((uint16_t)(ack - rev) >= 0x8000u) {
      break;
    }
    client->reliable_head = (uint8_t)((client->reliable_head + 1) %
                                      RELIABLE_QUEUE_MAX);
    client->reliable_count--;
    client->reliable_acked_rev = rev;
    if (debug) {
      printf("ACK reliable slot=%d rev=%u\n", slot, (unsigned)rev);
    }
  }
  if (client->reliable_count > 0) {
    reliable_send_from(client, now, "advance", slot, debug);
  }
}

static int reliable_enqueue_client(struct client_slot *client, int slot,
                                   const uint8_t *event, size_t event_len,
                                   uint8_t *seq, uint64_t now, int debug) {
  if (!client->in_use || !client->handshake_ok) {
    return 0;
  }
  if (event_len == 0 || event_len > RELIABLE_EVENT_MAX ||
      client->reliable_count >= RELIABLE_QUEUE_MAX) {
    client->tx.failed = 1;
    if (debug) {
      printf("DROP client slot=%d reliable queue saturated type=%02X\n", slot,
             event_len ? (unsigned)event[0] : 0u);
    }
    return -1;
  }
  uint8_t idx = (uint8_t)((client->reliable_head + client->reliable_count) %
                          RELIABLE_QUEUE_MAX);
  uint16_t rev = client->reliable_next_rev++;
  uint8_t *pkt = client->reliable_q[idx].pkt;
  pkt[0] = PKT_RELIABLE_EVENT;
  pkt[1] = (*seq)++;
  pkt[2] = (uint8_t)rev;
  pkt[3] = (uint8_t)(rev >> 8);
  memcpy(&pkt[4], event, event_len);
  client->reliable_q[idx].rev = rev;
  client->reliable_q[idx].len = (uint8_t)(4 + event_len);
  client->reliable_q[idx].retries = 0;
  client->reliable_count++;
  if (client->reliable_count == 1) {
    send_checked(client, pkt, client->reliable_q[idx].len);
    client->reliable_sent_rev = rev;
    client->reliable_last_send_ms = now;
  }
  if (debug) {
    printf("TX reliable new slot=%d rev=%u type=%02X\n", slot, (unsigned)rev,
           (unsigned)event[0]);
  }
  return 0;
}

static void broadcast_reliable_event(struct client_slot *clients,
                                     const uint8_t *event, size_t event_len,
                                     uint8_t *seq, uint64_t now, int debug) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    (void)reliable_enqueue_client(&clients[i], i, event, event_len, seq, now,
                                  debug);
  }
}

/* Complete the actor side of a successful gameplay join.

   A live Zombie is a real occupant, so taking over that seat keeps its current
   cell. A vacant seat is different: its stored coordinates are only history,
   and another actor may have walked there since the seat was released. Give
   that client a new collision-safe spawn before making the slot occupied.

   Every gameplay join publishes a final RESPAWN even for an in-place Zombie
   handoff. Besides making the discontinuity explicit on the wire, this makes
   clients redraw an actor that they had erased while the seat was vacant. */
static void activate_joining_client(struct room *room, int slot,
                                    int keep_position, uint64_t now,
                                    int debug) {
  if (slot < 0 || slot >= MAX_PLAYERS ||
      room->round_state != ROUND_PLAYING) {
    return;
  }

  struct player_state *player = &room->players[slot];
  if (!keep_position) {
    uint8_t sx = 0, sy = 0;
    pick_spawn(room, &sx, &sy);
    player->x = sx;
    player->y = sy;
  }
  player->respawn_at_ms = 0;
  room->occupied_mask |= (uint8_t)(1u << slot);

  uint8_t respawn[RESPAWN_LEN];
  build_respawn(room->seq++, (uint8_t)slot, player->x, player->y, 0x03,
                room->round_id, respawn, sizeof(respawn));
  broadcast_packet(room->listener_fd, room->clients, respawn, sizeof(respawn));
  broadcast_reliable_event(room->clients, respawn, sizeof(respawn), &room->seq,
                           now, debug);
  queue_respawn_echo(room, respawn);
  if (debug) {
    printf("room=%d TX join respawn pid=%d x=%u y=%u kept_position=%d\n",
           room->config.port, slot, (unsigned)player->x,
           (unsigned)player->y, keep_position);
  }
}

static void handle_client_packet(struct room *room, int slot,
                                 const uint8_t *pkt, size_t len, int debug,
                                 uint64_t now) {
  struct player_state *players = room->players;
  uint8_t *brick_bits = room->brick_bits;
  struct client_slot *clients = room->clients;
  uint8_t *seq = &room->seq;
  struct transport_counters *global_transport = &room->global_transport;
  struct client_slot *client = &clients[slot];

  if (!client->handshake_ok) {
    if (len == 2 && pkt[0] == PKT_HELLO && pkt[1] == PROTOCOL_VERSION) {
      uint8_t zombie_mask[MAX_PLAYERS];
      compute_zombie_mask(clients, room->config.zombies, zombie_mask);
      /* Capture the pre-HELLO role. Once handshake_ok is set this slot stops
         being a Zombie, so the information would otherwise be lost. A Zombie
         awaiting respawn is off-board and needs a fresh visible spawn. */
      int keep_join_position = room->round_state == ROUND_DORMANT ||
                               (zombie_mask[slot] &&
                                slot_on_board(room, slot));
      client->handshake_ok = 1;
      client->received_packet = 1;
      if (room->round_state == ROUND_DORMANT) {
        wake_dormant_room(room, now);
      }
      if (room->no_human_deadline_ms != 0) {
        room->no_human_deadline_ms = 0;
        printf("room=%d no-human grace canceled by slot=%d\n",
               room->config.port, slot);
      }
      uint8_t welcome[5] = {PKT_WELCOME, PROTOCOL_VERSION, room->round_id,
                            room->round_state,
                            (uint8_t)room->config.kill_limit};
      send_checked(client, welcome, sizeof(welcome));
      send_round_state(room, slot, now, debug);
      activate_joining_client(room, slot, keep_join_position, now, debug);
      if (debug) {
        printf("room=%d HELLO accepted slot=%d version=%u round=%u phase=%u\n",
               room->config.port, slot, (unsigned)PROTOCOL_VERSION,
               (unsigned)room->round_id, (unsigned)room->round_state);
      }
    } else if (len > 0 && pkt[0] == PKT_HELLO) {
      uint8_t reject[3] = {PKT_REJECT, PROTOCOL_VERSION, 1};
      send_checked(client, reject, sizeof(reject));
      (void)tcp_tx_flush(client->fd, &client->tx);
      client->tx.failed = 1;
      if (debug) {
        printf("room=%d HELLO rejected slot=%d\n", room->config.port, slot);
      }
    } else if (debug) {
      /* FujiNet-PC may reopen its TCP socket between the Atari's first HELLO
         and the next queued frame. Do not turn a harmless pre-handshake NAME
         or heartbeat into a reconnect loop: it owns no seat and mutates no
         game state, and the Atari retries HELLO until WELCOME arrives. */
      printf("room=%d waiting for HELLO slot=%d ignored type=%02X len=%zu\n",
             room->config.port, slot, len ? (unsigned)pkt[0] : 0u, len);
    }
    return;
  }

  if (len == 2 && pkt[0] == PKT_LEAVE_ROOM) {
    uint8_t ack[2] = {PKT_LEAVE_ACK, pkt[1]};
    (void)send_checked(client, ack, sizeof(ack));
    if (!client->leave_requested) {
      client->leave_requested = 1;
      client->leave_seq = pkt[1];
      if (debug) {
        printf("room=%d LEAVE_ROOM slot=%d seq=%u\n", room->config.port,
               slot, (unsigned)pkt[1]);
      }
    }
    return;
  }
  /* Once leave begins, later frames on the same socket cannot mutate this
     seat. Duplicate LEAVE_ROOM was handled above and merely repeats the ACK. */
  if (client->leave_requested) return;

  if (len == 4 && pkt[0] == PKT_RELIABLE_ACK) {
    uint16_t ack = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
    reliable_ack(&clients[slot], slot, ack, now, debug);
    return;
  }

  if (pkt[0] == PKT_DELTA) {
    uint8_t pid = (uint8_t)slot;
    if (pid < MAX_PLAYERS) {
      struct transport_delta_packet delta;
      if (len != DELTA_LEN || pkt[4] != room->round_id ||
          !transport_decode_delta_for_slot(pkt, DELTA_LEN - 1, pid, &delta)) {
        if (len == DELTA_LEN &&
            packet_has_bad_joy_for_slot(pkt, DELTA_LEN - 1, pid)) {
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
      if (room->round_state != ROUND_PLAYING) {
        /* A neutral DELTA is the session heartbeat during results. It keeps
           the peer alive but never enters the input queue or advances the
           applied-input acknowledgement. */
        if ((delta.joy & 0x1Fu) != 0x0Fu && debug) {
          printf("DROP DELTA slot=%d gameplay during intermission\n", slot);
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
          c->input_q[tail].ready_at_ms = now + (uint64_t)room->config.lag_ms;
          c->input_count++;
        } else if (debug) {
          /* Not acked: the client keeps it pending and replays it. */
          printf("DROP DELTA slot=%d queue-full seq=%u\n", slot,
                 (unsigned)delta.seq);
        }
      }
      room->last_input_ms[pid] = now;
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
    broadcast_packet(room->listener_fd, clients, out, sizeof(out));
    broadcast_reliable_event(clients, out, sizeof(out), seq, now, debug);
    if (debug) {
      printf("NAME slot=%d name=\"%.*s\"\n", slot, NAME_LEN,
             (const char *)clients[slot].name);
    }
    return;
  }

  if (room->round_state == ROUND_PLAYING && len == RESPAWN_LEN &&
      pkt[0] == PKT_RESPAWN && pkt[6] == room->round_id) {
    uint8_t pid = (uint8_t)slot;
    if (pid < MAX_PLAYERS) {
      uint8_t sx = 0, sy = 0;
      uint8_t out[RESPAWN_LEN];
      pick_spawn(room, &sx, &sy);
      players[pid].x = sx;
      players[pid].y = sy;
      build_respawn((*seq)++, pid, sx, sy, 0x03, room->round_id, out,
                    sizeof(out));
      broadcast_packet(room->listener_fd, clients, out, sizeof(out));
      broadcast_reliable_event(clients, out, sizeof(out), seq, now, debug);
      queue_respawn_echo(room, out);
      if (debug) {
        printf("TX respawn pid=%u x=%u y=%u\n", pid, sx, sy);
      }
    }
    return;
  }

  if (room->round_state == ROUND_PLAYING && len == BRICK_DELTA_LEN &&
      pkt[0] == PKT_BRICK_DELTA && pkt[4] == room->round_id) {
    uint8_t x = pkt[2];
    uint8_t y = pkt[3];
    if (x < 20 && y < 19 && !is_outer_wall_cell((int)x, (int)y)) {
      uint8_t out[BRICK_DELTA_LEN];
      clear_brick(brick_bits, x, y);
      queue_brick_echo(room, x, y);
      build_brick_delta((*seq)++, x, y, room->round_id, out, sizeof(out));
      broadcast_packet(room->listener_fd, clients, out, sizeof(out));
      broadcast_reliable_event(clients, out, sizeof(out), seq, now, debug);
      if (debug) {
        printf("TX brick_delta x=%u y=%u\n", x, y);
      }
    }
    return;
  }
}

static void process_client_bytes(struct room *room, int slot,
                                 const uint8_t *buf, size_t n, int debug,
                                 uint64_t now) {
  struct client_slot *clients = room->clients;
  struct transport_counters *global_transport = &room->global_transport;
  struct client_slot *c = &clients[slot];
  for (size_t i = 0; i < n; i++) {
    uint8_t pkt[16]; /* NAME is the longest inbound packet at 11 bytes */
    int pkt_len = tcp_frame_push_byte(&c->frame_rx, buf[i], pkt, sizeof(pkt));
    if (pkt_len < 0) {
      c->transport.delta_resync++;
      global_transport->delta_resync++;
      continue;
    }
    if (pkt_len == 0) {
      continue;
    }
    /* The CRC/COBS layer establishes frame boundaries.  Retain the legacy
       DELTA layouts only as payload compatibility, not as a raw stream scan. */
    c->received_packet = 1;
    handle_client_packet(room, slot, pkt, (size_t)pkt_len, debug, now);
  }
}

static void process_departing_bytes(struct departing_connection *departing,
                                    const uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t pkt[16];
    int pkt_len = tcp_frame_push_byte(&departing->frame_rx, buf[i], pkt,
                                      sizeof(pkt));
    if (pkt_len == 2 && pkt[0] == PKT_LEAVE_ROOM) {
      uint8_t ack[2] = {PKT_LEAVE_ACK, pkt[1]};
      (void)send_frame(departing->fd, &departing->tx, ack, sizeof(ack));
    }
  }
}

static void reap_departing_connections(struct room *room, uint64_t now) {
  for (int i = 0; i < MAX_DEPARTING; i++) {
    struct departing_connection *departing = &room->departing[i];
    if (!departing->in_use) continue;
    if (departing->tx.failed || departing->tx.len == 0 ||
        now >= departing->deadline_ms) {
      reset_departing(departing);
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
    if (clients[i].in_use && clients[i].handshake_ok) {
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

static int dir_free(uint8_t dir, const struct room *room, int idx) {
  const struct player_state *players = room->players;
  const uint8_t *bricks = room->brick_bits;
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
  if (is_player_at(room, nx, ny, idx)) {
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

static void zombie_ai(struct room *room, int idx,
                      const uint8_t *human_mask) {
  struct player_state *players = room->players;
  const uint8_t *bricks = room->brick_bits;
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
    if (dir_free(dir_y, room, idx)) {
      players[idx].joy = stick_from_dir(dir_y);
      return;
    }
    if (dir_free(dir_x, room, idx)) {
      players[idx].joy = stick_from_dir(dir_x);
      return;
    }
    players[idx].joy = stick_from_dir(dir_y);
    players[idx].zombie_fire_pending = 1;
    return;
  }
  if (dir_free(dir_x, room, idx)) {
    players[idx].joy = stick_from_dir(dir_x);
    return;
  }
  if (dir_free(dir_y, room, idx)) {
    players[idx].joy = stick_from_dir(dir_y);
    return;
  }
  players[idx].joy = stick_from_dir(dir_x);
  players[idx].zombie_fire_pending = 1;
}

static void apply_move_if_free(struct room *room, int idx) {
  struct player_state *p = &room->players[idx];
  const uint8_t *bricks = room->brick_bits;
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
  if (!is_brick(bricks, nx, ny) && !is_player_at(room, nx, ny, idx)) {
    p->x = (uint8_t)nx;
    p->y = (uint8_t)ny;
  }
}

static int award_score(struct room *room, int shooter, uint64_t now,
                       int debug) {
  if (room->round_state != ROUND_PLAYING || shooter < 0 ||
      shooter >= MAX_PLAYERS) {
    return 0;
  }
  if (room->players[shooter].score < (uint8_t)room->config.kill_limit) {
    room->players[shooter].score++;
  }
  if (room->players[shooter].score >= (uint8_t)room->config.kill_limit) {
    room->players[shooter].score = (uint8_t)room->config.kill_limit;
    enter_round_over(room, shooter, now, debug);
    return 1;
  }
  return 0;
}

static void start_shot(struct room *room, int shooter, uint8_t joy, int debug) {
  struct player_state *players = room->players;
  struct shot_state *shots = room->shots;
  uint8_t *bricks = room->brick_bits;
  struct client_slot *clients = room->clients;
  uint8_t *seq = &room->seq;
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
      queue_brick_echo(room, (uint8_t)sx, (uint8_t)sy);
      uint8_t pkt[BRICK_DELTA_LEN];
      build_brick_delta((*seq)++, (uint8_t)sx, (uint8_t)sy,
                        room->round_id, pkt, sizeof(pkt));
      broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
      broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now_ms(), debug);
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
  if (is_player_at(room, sx, sy, shooter)) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
      if (p == shooter) {
        continue;
      }
      if (!slot_on_board(room, p)) {
        continue;
      }
      if (players[p].x == (uint8_t)sx && players[p].y == (uint8_t)sy) {
        uint64_t now = now_ms();
        int round_ended = award_score(room, shooter, now, debug);
        if (round_ended) {
          return;
        }
        players[p].respawn_at_ms = now + 2000;
        {
          uint8_t rpkt[RESPAWN_LEN];
          build_respawn((*seq)++, (uint8_t)p, 0, 0, 0x01, room->round_id,
                        rpkt, sizeof(rpkt));
          broadcast_packet(room->listener_fd, clients, rpkt, sizeof(rpkt));
          broadcast_reliable_event(clients, rpkt, sizeof(rpkt), seq, now_ms(),
                                   debug);
          queue_respawn_echo(room, rpkt);
        }
        /* Defensive clear: ensure any stale client-side shot sprite is removed. */
        {
          uint8_t spkt[SHOT_LEN];
          build_shot((*seq)++, (uint8_t)shooter, 0, 0, 0, room->round_id,
                     spkt, sizeof(spkt));
          broadcast_packet(room->listener_fd, clients, spkt, sizeof(spkt));
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

static void step_shots(struct room *room, int debug) {
  struct player_state *players = room->players;
  struct shot_state *shots = room->shots;
  uint8_t *bricks = room->brick_bits;
  struct client_slot *clients = room->clients;
  uint8_t *seq = &room->seq;
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
      uint8_t pkt[SHOT_LEN];
      build_shot((*seq)++, (uint8_t)i, 0, 0, 0, room->round_id, pkt,
                 sizeof(pkt));
      broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
      continue;
    }
    if (is_brick(bricks, nx, ny)) {
      if (!is_outer_wall_cell(nx, ny)) {
        clear_brick(bricks, nx, ny);
        queue_brick_echo(room, (uint8_t)nx, (uint8_t)ny);
        uint8_t pkt[BRICK_DELTA_LEN];
        build_brick_delta((*seq)++, (uint8_t)nx, (uint8_t)ny,
                          room->round_id, pkt, sizeof(pkt));
        broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
        broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now, debug);
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
      uint8_t spkt[SHOT_LEN];
      build_shot((*seq)++, (uint8_t)i, 0, 0, 0, room->round_id, spkt,
                 sizeof(spkt));
      broadcast_packet(room->listener_fd, clients, spkt, sizeof(spkt));
      continue;
    }
    for (int p = 0; p < MAX_PLAYERS; p++) {
      if (p == i) {
        continue;
      }
      if (!slot_on_board(room, p)) {
        continue;
      }
      if (players[p].x == (uint8_t)nx && players[p].y == (uint8_t)ny) {
        int round_ended = award_score(room, i, now, debug);
        if (round_ended) {
          return;
        }
        players[p].respawn_at_ms = now + 2000;
        uint8_t pkt[RESPAWN_LEN];
        build_respawn((*seq)++, (uint8_t)p, 0, 0, 0x01, room->round_id, pkt,
                      sizeof(pkt));
        broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
        broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now, debug);
        queue_respawn_echo(room, pkt);
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
        uint8_t spkt[SHOT_LEN];
        build_shot((*seq)++, (uint8_t)i, 0, 0, 0, room->round_id, spkt,
                   sizeof(spkt));
        broadcast_packet(room->listener_fd, clients, spkt, sizeof(spkt));
        goto next_shot;
      }
    }
    shots[i].x = (uint8_t)nx;
    shots[i].y = (uint8_t)ny;
    {
      uint8_t spkt[SHOT_LEN];
      build_shot((*seq)++, (uint8_t)i, (uint8_t)nx, (uint8_t)ny,
                 shot_active_flags(&shots[i]), room->round_id,
                 spkt, sizeof(spkt));
      broadcast_packet(room->listener_fd, clients, spkt, sizeof(spkt));
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
    uint8_t spkt[SHOT_LEN];
    build_shot((*seq)++, (uint8_t)i, 0, 0, 0, room->round_id, spkt,
               sizeof(spkt));
    broadcast_packet(room->listener_fd, clients, spkt, sizeof(spkt));
  }
}

/* Take one queued input per client per tick, in the order the client sent it,
   and acknowledge exactly what was applied and nothing more. The ack is what
   the client's pending-input ring trusts when deciding what it may discard, so
   reporting an input as applied when it was not is what produced the snap-back
   on a fast corner turn. */
static void apply_queued_input(struct room *room, int debug, uint64_t now) {
  struct client_slot *clients = room->clients;
  struct player_state *players = room->players;
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

static void step_players(struct room *room, int debug) {
  struct player_state *players = room->players;
  struct client_slot *clients = room->clients;
  const uint64_t *last_input_ms = room->last_input_ms;
  uint8_t *seq = &room->seq;
  int zombies = room->config.zombies;
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
    if (clients[i].in_use && clients[i].handshake_ok) {
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
  /* Everything downstream this tick -- collision, fire evaluation, shot hits,
     respawn placement -- asks slot_on_board() rather than the two masks. */
  room->occupied_mask = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (zombie_mask[i] || human_mask[i]) {
      room->occupied_mask |= (uint8_t)(1u << i);
    }
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (players[i].respawn_at_ms != 0 &&
        now >= players[i].respawn_at_ms) {
      uint8_t sx = 0, sy = 0;
      pick_spawn(room, &sx, &sy);
      players[i].x = sx;
      players[i].y = sy;
      players[i].respawn_at_ms = 0;
      players[i].joy = 0x0F;
      players[i].zombie_fire_pending = 0;
      players[i].zombie_think_next_ms = now;
      players[i].zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
      players[i].zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
      uint8_t pkt[RESPAWN_LEN];
      build_respawn((*seq)++, (uint8_t)i, sx, sy, 0x03, room->round_id, pkt,
                    sizeof(pkt));
      broadcast_packet(room->listener_fd, clients, pkt, sizeof(pkt));
      broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now, debug);
      queue_respawn_echo(room, pkt);
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
    /* Only for a slot with no client left in it. While a client is connected
       apply_queued_input() is the sole authority on this slot's joy: it sets
       the queued direction, and neutral when the queue is empty. Applying a
       wall-clock staleness test on top of that raced with the queue drain and
       silently threw away real inputs.

       `last_input_ms` is stamped when a DELTA *arrives*, but an input is
       applied one per tick, so an entry that waits two ticks is already
       INPUT_STALE_MS old when its turn comes: the direction was set and then
       neutralised again a few statements later, in the same tick, before the
       move. The client had been acked for it, so it dropped the input from its
       pending ring and never replayed it -- an acked-but-discarded input, which
       is the snap-back signature. At the 4 Hz the combat smokes use, two ticks
       is exactly 500 ms, which is why combat_world_authority_smoke failed
       roughly four runs in ten. At the 10 Hz the Atari runs it needs a burst to
       bite, but nothing prevented it.

       Kept for a slot whose client is gone: it is not in apply_queued_input()'s
       loop at all, so without this its actor would keep walking on the last joy
       it was given for the whole reap grace window. */
    if (!zombie_mask[i] && !clients[i].in_use) {
      if (last_input_ms[i] == 0 || (now - last_input_ms[i]) > INPUT_STALE_MS) {
        players[i].joy = 0x0F;
      }
    }
    int can_act = (players[i].respawn_at_ms == 0);
    uint8_t action_joy = players[i].joy;
    int can_move = can_act;
    if (zombie_mask[i] && can_act) {
      if (now >= players[i].zombie_think_next_ms) {
        zombie_ai(room, i, human_mask);
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
      start_shot(room, i, action_joy, debug);
      if (room->round_state != ROUND_PLAYING) {
        return;
      }
      if (can_move && !(trig && stick != 0x0F)) {
        uint8_t before_x = players[i].x;
        uint8_t before_y = players[i].y;
        apply_move_if_free(room, i);
        /* Only when a direction was actually asked for. A neutral stick moves
           nobody, and reporting that as "blocked" made every idle actor log a
           blocked move on every tick -- the log said four players were pinned
           against a wall when nothing was happening at all. */
        if (debug && stick != 0x0F) {
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
  step_shots(room, debug);
}

static void build_brick_full(uint8_t seq, const uint8_t *bits, uint8_t round_id,
                             uint8_t *out, size_t out_len) {
  if (out_len < BRICK_FULL_LEN) {
    return;
  }
  out[0] = PKT_BRICK_FULL;
  out[1] = seq;
  out[2] = 0x01;
  memcpy(&out[3], bits, 48);
  out[51] = round_id;
}

static void build_fallback_name(uint8_t *out, int slot, int zombie) {
  char text[NAME_LEN + 1];
  snprintf(text, sizeof(text), zombie ? "ZOMBIE %d" : "WIZARD %d", slot + 1);
  memcpy(out, text, NAME_LEN);
}

static void freeze_match_result(struct room *room, int winner) {
  uint8_t zombie_mask[MAX_PLAYERS];
  uint8_t zombie_bits = 0;
  compute_zombie_mask(room->clients, room->config.zombies, zombie_mask);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (zombie_mask[i]) zombie_bits |= (uint8_t)(1u << i);
  }
  room->historical_zombie_mask |= zombie_bits;
  room->winner_pid = (uint8_t)winner;
  room->final_active_mask = room->occupied_mask;
  room->final_zombie_mask = zombie_bits;
  memset(room->frozen_names, ' ', sizeof(room->frozen_names));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    room->final_scores[i] = room->players[i].score;
    if (!(room->final_active_mask & (1u << i))) continue;
    if (!zombie_mask[i] && room->clients[i].in_use &&
        room->clients[i].handshake_ok && name_is_set(room->clients[i].name)) {
      memcpy(room->frozen_names[i], room->clients[i].name, NAME_LEN);
    } else {
      build_fallback_name(room->frozen_names[i], i, zombie_mask[i]);
    }
  }

  uint8_t *pkt = room->frozen_match;
  pkt[0] = PKT_MATCH_END;
  pkt[1] = room->round_id;
  pkt[2] = room->winner_pid;
  pkt[3] = room->final_active_mask;
  pkt[4] = room->final_zombie_mask;
  pkt[5] = (uint8_t)room->config.kill_limit;
  memcpy(&pkt[6], room->final_scores, MAX_PLAYERS);
  pkt[10] = room->historical_zombie_mask;
  memcpy(&pkt[11], room->frozen_names, sizeof(room->frozen_names));
}

static void enter_round_over(struct room *room, int winner, uint64_t now,
                             int debug) {
  if (room->round_state != ROUND_PLAYING) return;
  room->round_state = ROUND_OVER;
  freeze_match_result(room, winner);
  room->intermission_deadline_ms = now + (uint64_t)room->config.intermission_ms;
  memset(room->shots, 0, sizeof(room->shots));
  memset(room->brick_echo, 0, sizeof(room->brick_echo));
  memset(room->respawn_echo, 0, sizeof(room->respawn_echo));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    room->players[i].joy = 0x0F;
    room->clients[i].input_head = 0;
    room->clients[i].input_count = 0;
  }
  broadcast_reliable_event(room->clients, room->frozen_match, MATCH_END_LEN,
                           &room->seq, now, debug);
  printf("room=%d round=%u winner=%u score=%u intermission_ms=%d\n",
         room->config.port, (unsigned)room->round_id, (unsigned)winner,
         (unsigned)room->final_scores[winner], room->config.intermission_ms);
}

static void send_round_state(struct room *room, int slot, uint64_t now,
                             int debug) {
  struct client_slot *client = &room->clients[slot];
  if (!client->in_use || !client->handshake_ok) return;
  if (room->round_state == ROUND_OVER) {
    (void)reliable_enqueue_client(client, slot, room->frozen_match,
                                  MATCH_END_LEN, &room->seq, now, debug);
    return;
  }
  uint8_t start[ROUND_START_LEN] = {PKT_ROUND_START, room->round_id,
                                    (uint8_t)room->config.kill_limit};
  uint8_t map[BRICK_FULL_LEN];
  build_brick_full(room->seq++, room->brick_bits, room->round_id, map,
                   sizeof(map));
  if (reliable_enqueue_client(client, slot, start, sizeof(start), &room->seq,
                              now, debug) == 0) {
    (void)reliable_enqueue_client(client, slot, map, sizeof(map), &room->seq,
                                  now, debug);
  }
}

static void reset_round(struct room *room, uint64_t now, int debug) {
  room->round_id++;
  room->round_state = ROUND_PLAYING;
  room->intermission_deadline_ms = 0;
  memcpy(room->brick_bits, room->brick_reset_bits, sizeof(room->brick_bits));
  memset(room->shots, 0, sizeof(room->shots));
  memset(room->brick_echo, 0, sizeof(room->brick_echo));
  memset(room->respawn_echo, 0, sizeof(room->respawn_echo));

  uint8_t zombie_mask[MAX_PLAYERS];
  uint8_t zombie_bits = 0;
  compute_zombie_mask(room->clients, room->config.zombies, zombie_mask);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (zombie_mask[i]) zombie_bits |= (uint8_t)(1u << i);
  }
  room->historical_zombie_mask = zombie_bits;
  room->occupied_mask = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    struct player_state *player = &room->players[i];
    player->joy = 0x0F;
    player->score = 0;
    player->respawn_at_ms = 0;
    player->zombie_fire_pending = 0;
    player->zombie_think_next_ms = now;
    player->zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
    player->zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
    room->last_input_ms[i] = 0;
    room->clients[i].have_delta_seq = 0;
    room->clients[i].have_applied_input_seq = 0;
    room->clients[i].input_head = 0;
    room->clients[i].input_count = 0;
    if ((room->clients[i].in_use && room->clients[i].handshake_ok) ||
        zombie_mask[i]) {
      uint8_t sx = 0, sy = 0;
      pick_spawn(room, &sx, &sy);
      player->x = sx;
      player->y = sy;
      room->occupied_mask |= (uint8_t)(1u << i);
    }
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    send_round_state(room, i, now, debug);
  }
  room->last_brick_resync_ms = now;
  room->next_tick = now;
  printf("room=%d round=%u started kill_limit=%d\n", room->config.port,
         (unsigned)room->round_id, room->config.kill_limit);
}

static void enter_dormant(struct room *room, uint64_t now, int debug,
                          const char *reason) {
  /* Build a complete clean next-round baseline while no client can observe the
     transition, then park simulation until another HELLO authenticates. */
  reset_round(room, now, debug);
  room->round_state = ROUND_DORMANT;
  room->occupied_mask = 0;
  room->intermission_deadline_ms = 0;
  room->no_human_deadline_ms = 0;
  printf("room=%d dormant reason=%s round=%u\n", room->config.port, reason,
         (unsigned)room->round_id);
}

static void wake_dormant_room(struct room *room, uint64_t now) {
  uint8_t zombie_mask[MAX_PLAYERS];
  room->round_state = ROUND_PLAYING;
  room->occupied_mask = 0;
  room->historical_zombie_mask = 0;
  compute_zombie_mask(room->clients, room->config.zombies, zombie_mask);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!(room->clients[i].in_use && room->clients[i].handshake_ok) &&
        !zombie_mask[i]) {
      continue;
    }
    uint8_t sx = 0, sy = 0;
    pick_spawn(room, &sx, &sy);
    room->players[i].x = sx;
    room->players[i].y = sy;
    reset_slot_gameplay(room, i, now);
    room->players[i].respawn_at_ms = 0;
    room->occupied_mask |= (uint8_t)(1u << i);
    if (zombie_mask[i]) {
      room->historical_zombie_mask |= (uint8_t)(1u << i);
    }
  }
  /* Give the newly welcomed client one full server interval to deliver its
     first input before the first snapshot, matching established join timing.
     Rebase dormant maintenance timers so wake-up does not inject an immediate
     stale NAME rotation ahead of the client's own name. */
  room->next_tick = now + room->tick_ms;
  /* Avoid a blank-name safety rotation in the HELLO service pass, but keep the
     first repair comfortably inside one second for clients waiting on the
     next reliable revision. */
  room->last_name_rotate_ms = now - NAME_ROTATE_MS / 2;
  room->last_brick_resync_ms = now;
  room->last_seat_ms = now;
  room->last_seat_mask = 0xFF;
  printf("room=%d activated round=%u\n", room->config.port,
         (unsigned)room->round_id);
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
          "Usage: %s [--port PORT | --port-base PORT] [--room-count N]\n"
          "          [--zombies N | --room-zombies LIST] [--bind ADDR]\n"
          "          [--tick-hz N] [--brick PATH] [--lag-ms N]\n"
          "          [--kill-limit N] [--intermission-ms N]\n"
          "          [--no-human-grace-ms N] [--debug]\n"
          "  --port PORT       one-room compatibility form (default 9000).\n"
          "  --port-base PORT  first listener; later rooms use consecutive ports.\n"
          "  --room-count N    number of isolated rooms (1-%d, default 1).\n"
          "  --zombies N       zombie count for every room (0-3, default 1).\n"
          "  --room-zombies L  comma-separated zombie count for each room.\n"
          "  --kill-limit N    kills needed to win a round (1-10, default 5).\n"
          "  --intermission-ms round-end sequence duration (default 15000).\n"
          "  --no-human-grace-ms unexpected final-player grace (default 60000).\n"
          "  --bind ADDR       bind a specific IPv4 address.\n",
          argv0, MAX_ROOMS);
}

static int parse_int_arg(const char *text, int *out) {
  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < 0 ||
      value > 2147483647L) {
    return -1;
  }
  *out = (int)value;
  return 0;
}

static int parse_room_zombies(const char *text, int room_count, int *out) {
  const char *p = text;
  for (int room_index = 0; room_index < room_count; room_index++) {
    char *end = NULL;
    errno = 0;
    long value = strtol(p, &end, 10);
    if (errno != 0 || end == p || value < 0 || value >= MAX_PLAYERS) {
      return -1;
    }
    out[room_index] = (int)value;
    if (room_index + 1 == room_count) {
      return *end == '\0' ? 0 : -1;
    }
    if (*end != ',') {
      return -1;
    }
    p = end + 1;
  }
  return -1;
}

static int create_listener(int port, const struct in_addr *bind_ip) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    close(fd);
    return -1;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr = *bind_ip;
  addr.sin_port = htons((uint16_t)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(fd, MAX_PLAYERS) < 0 ||
      fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int init_room(struct room *room, const struct room_config *config,
                     const struct in_addr *bind_ip, uint64_t now) {
  memset(room, 0, sizeof(*room));
  room->config = *config;
  room->listener_fd = -1;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    room->clients[i].fd = -1;
    room->departing[i].fd = -1;
  }
  if (load_brick_layout(config->brick_path, room->brick_reset_bits,
                        sizeof(room->brick_reset_bits)) != 0) {
    memset(room->brick_reset_bits, 0, sizeof(room->brick_reset_bits));
    fprintf(stderr, "Warning: failed to load brick layout: %s\n",
            config->brick_path);
  }
  memcpy(room->brick_bits, room->brick_reset_bits, sizeof(room->brick_bits));
  room->occupied_mask = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    uint8_t sx = 0, sy = 0;
    pick_spawn(room, &sx, &sy);
    room->players[i].x = sx;
    room->players[i].y = sy;
    room->players[i].joy = 0x0F;
    room->players[i].zombie_think_next_ms = now;
    room->players[i].zombie_move_next_ms = now + ZOMBIE_MOVE_MS;
    room->players[i].zombie_fire_next_ms = now + ZOMBIE_FIRE_MS;
  }
  room->tick_ms = 1000ULL / (uint64_t)config->tick_hz;
  if (room->tick_ms == 0) {
    room->tick_ms = 1;
  }
  room->next_tick = now;
  room->last_transport_summary_ms = now;
  room->last_brick_resync_ms = now;
  room->last_name_rotate_ms = now;
  room->last_seat_mask = 0xFF;
  room->round_id = 1;
  room->round_state = ROUND_DORMANT;
  {
    uint8_t zombie_mask[MAX_PLAYERS];
    compute_zombie_mask(room->clients, room->config.zombies, zombie_mask);
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (zombie_mask[i]) room->historical_zombie_mask |= (uint8_t)(1u << i);
    }
  }
  room->listener_fd = create_listener(config->port, bind_ip);
  return room->listener_fd >= 0 ? 0 : -1;
}

static void destroy_room(struct room *room, int debug) {
  if (debug) {
    log_transport_summaries(room->clients, &room->global_transport);
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    reset_client_slot(&room->clients[i]);
    reset_departing(&room->departing[i]);
  }
  if (room->listener_fd >= 0) {
    close(room->listener_fd);
    room->listener_fd = -1;
  }
}

static void accept_room_client(struct room *room, int debug, uint64_t now) {
  struct sockaddr_in src;
  socklen_t src_len = sizeof(src);
  int fd = accept(room->listener_fd, (struct sockaddr *)&src, &src_len);
  if (fd < 0) {
    if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      perror("accept");
    }
    return;
  }
  int slot = alloc_client_slot(room->clients, room->config.zombies);
  if (slot < 0 || tcp_configure(fd) < 0) {
    close(fd);
    return;
  }
  init_client_slot(&room->clients[slot], fd, &src, src_len, now);
  log_client_event("connected", slot, &room->clients[slot].addr);
  reset_slot_gameplay(room, slot, now);
  if (debug) {
    printf("room=%d waiting for HELLO from slot %d\n", room->config.port,
           slot);
  }
}

static void service_room_timers(struct room *room, int debug, uint64_t now) {
  reap_timed_out_clients(room, now, debug);
  reap_departing_connections(room, now);
  if (room->no_human_deadline_ms != 0 &&
      now >= room->no_human_deadline_ms) {
    enter_dormant(room, now, debug, "no-human-grace-expired");
  }
  if (room->round_state == ROUND_OVER &&
      now >= room->intermission_deadline_ms) {
    reset_round(room, now, debug);
  }
  if (room->round_state == ROUND_DORMANT) return;
  if (now - room->last_name_rotate_ms >= NAME_ROTATE_MS) {
    room->last_name_rotate_ms = now;
    broadcast_next_name(room, debug);
  }
  reliable_tick(room->clients, now, debug);
  if (now - room->last_brick_resync_ms >= BRICK_RESYNC_MS) {
    room->last_brick_resync_ms = now;
    uint8_t bfull[BRICK_FULL_LEN];
    build_brick_full(room->seq++, room->brick_bits, room->round_id, bfull,
                     sizeof(bfull));
    broadcast_packet(room->listener_fd, room->clients, bfull, sizeof(bfull));
    if (debug) {
      printf("room=%d TX brick_full resync -> all clients\n",
             room->config.port);
    }
  }
  uint8_t seat_mask = compute_seat_mask(room->clients);
  if (seat_mask != room->last_seat_mask ||
      now - room->last_seat_ms >= SEAT_REPEAT_MS) {
    room->last_seat_mask = seat_mask;
    room->last_seat_ms = now;
    uint8_t pkt[3];
    build_seats(room->seq++, seat_mask, pkt, sizeof(pkt));
    broadcast_packet(room->listener_fd, room->clients, pkt, sizeof(pkt));
    if (debug) {
      printf("room=%d TX seats mask=%X -> all clients\n",
             room->config.port, seat_mask);
    }
  }
  if (debug &&
      now - room->last_transport_summary_ms >= TRANSPORT_SUMMARY_MS) {
    log_transport_summaries(room->clients, &room->global_transport);
    room->last_transport_summary_ms = now;
  }
}

static void tick_room(struct room *room, int debug, uint64_t now) {
  uint64_t tick_deadline = room->next_tick;
  if (room->round_state == ROUND_PLAYING) {
    flush_brick_echo(room, debug);
    flush_respawn_echo(room, debug);
    apply_queued_input(room, debug, now);
    step_players(room, debug);
  }
  uint8_t zombie_mask[MAX_PLAYERS];
  uint8_t zombie_bits = 0;
  uint8_t snapshot_seq = room->seq++;
  compute_zombie_mask(room->clients, room->config.zombies, zombie_mask);
  for (int z = 0; z < MAX_PLAYERS; z++) {
    if (zombie_mask[z]) {
      zombie_bits |= (uint8_t)(1u << z);
    }
  }
  if (room->round_state == ROUND_PLAYING) {
    room->historical_zombie_mask |= zombie_bits;
  }
  uint8_t pkt[SNAPSHOT_LEN];
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!room->clients[i].in_use || !room->clients[i].handshake_ok) {
      continue;
    }
    build_snapshot(snapshot_seq, room->players,
                   room->clients[i].applied_input_seq, room->round_id, pkt,
                   sizeof(pkt));
    pkt[2] = (uint8_t)(0x01u | ((uint8_t)i << 1) |
                       ((uint8_t)(zombie_bits & 0x0Fu) << 3));
    if (room->clients[i].have_applied_input_seq) {
      pkt[2] |= 0x80u;
    }
    ssize_t wn = send_checked(&room->clients[i], pkt, sizeof(pkt));
    if (debug && wn == (ssize_t)sizeof(pkt)) {
      printf("room=%d TX snapshot -> slot %d\n", room->config.port, i);
    }
  }
  /* Keep the exact deadline progression used by the one-room server. */
  uint64_t tick_ms = room->tick_ms;
  uint64_t next_tick = tick_deadline + tick_ms;
  if (next_tick <= now) {
    unsigned skipped = 0;
    while (next_tick <= now && skipped < 4) {
      next_tick += tick_ms;
      skipped++;
    }
    if (next_tick <= now) {
      next_tick = now + tick_ms;
    }
  }
  room->next_tick = next_tick;
}

struct poll_ref {
  int room_index;
  int kind;
  int index;
  int fd;
};

enum { POLL_LISTENER = 0, POLL_CLIENT = 1, POLL_DEPARTING = 2 };

int main(int argc, char **argv) {
  int port_base = 9000;
  int room_count = 1;
  int tick_hz = 10;
  int debug = 0;
  int zombies = 1;
  int lag_ms = 0;
  int kill_limit = 5;
  int intermission_ms = DEFAULT_INTERMISSION_MS;
  int no_human_grace_ms = DEFAULT_NO_HUMAN_GRACE_MS;
  const char *brick_path = "server/brick_layout.txt";
  const char *bind_addr = NULL;
  const char *room_zombies_arg = NULL;
  int saw_port = 0;
  int saw_port_base = 0;
  int saw_zombies = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      saw_port = 1;
      if (parse_int_arg(argv[++i], &port_base) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--port-base") == 0 && i + 1 < argc) {
      saw_port_base = 1;
      if (parse_int_arg(argv[++i], &port_base) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--room-count") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &room_count) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      bind_addr = argv[++i];
    } else if (strcmp(argv[i], "--tick-hz") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &tick_hz) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--zombies") == 0 && i + 1 < argc) {
      saw_zombies = 1;
      if (parse_int_arg(argv[++i], &zombies) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--room-zombies") == 0 && i + 1 < argc) {
      room_zombies_arg = argv[++i];
    } else if (strcmp(argv[i], "--brick") == 0 && i + 1 < argc) {
      brick_path = argv[++i];
    } else if (strcmp(argv[i], "--lag-ms") == 0 && i + 1 < argc) {
      /* Test aid: hold each input this long before applying it, so a local
         run reproduces the pending-input backlog a real Atari always has.
         At zero latency the client's reposition-and-replay path barely runs,
         which hides bugs in it. */
      if (parse_int_arg(argv[++i], &lag_ms) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--kill-limit") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &kill_limit) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--intermission-ms") == 0 && i + 1 < argc) {
      if (parse_int_arg(argv[++i], &intermission_ms) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--no-human-grace-ms") == 0 &&
               i + 1 < argc) {
      if (parse_int_arg(argv[++i], &no_human_grace_ms) != 0) goto bad_args;
    } else if (strcmp(argv[i], "--debug") == 0) {
      debug = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      goto bad_args;
    }
  }

  if (room_count < 1 || room_count > MAX_ROOMS || port_base < 1 ||
      port_base > 65535 || room_count - 1 > 65535 - port_base ||
      tick_hz < 1 || tick_hz > 1000 || zombies < 0 ||
      zombies >= MAX_PLAYERS || kill_limit < 1 || kill_limit > 10 ||
      intermission_ms < 100 || intermission_ms > 600000 ||
      no_human_grace_ms < 100 || no_human_grace_ms > 600000 ||
      (saw_port && (saw_port_base || room_count != 1)) ||
      (saw_zombies && room_zombies_arg != NULL)) {
    goto bad_args;
  }

  int *room_zombies = calloc((size_t)room_count, sizeof(*room_zombies));
  struct room *rooms = calloc((size_t)room_count, sizeof(*rooms));
  size_t poll_count =
      (size_t)room_count * (MAX_PLAYERS + MAX_DEPARTING + 1u);
  struct pollfd *pfds = calloc(poll_count, sizeof(*pfds));
  struct poll_ref *refs = calloc(poll_count, sizeof(*refs));
  if (!room_zombies || !rooms || !pfds || !refs) {
    fprintf(stderr, "Out of memory creating %d rooms.\n", room_count);
    free(room_zombies);
    free(rooms);
    free(pfds);
    free(refs);
    return 1;
  }
  for (int i = 0; i < room_count; i++) room_zombies[i] = zombies;
  if (room_zombies_arg != NULL &&
      parse_room_zombies(room_zombies_arg, room_count, room_zombies) != 0) {
    fprintf(stderr, "Invalid --room-zombies list for %d rooms.\n", room_count);
    free(room_zombies);
    free(rooms);
    free(pfds);
    free(refs);
    return 1;
  }

  struct in_addr bind_ip;
  bind_ip.s_addr = htonl(INADDR_ANY);
  if (bind_addr != NULL && inet_pton(AF_INET, bind_addr, &bind_ip) != 1) {
    fprintf(stderr, "Invalid --bind address: %s\n", bind_addr);
    free(room_zombies);
    free(rooms);
    free(pfds);
    free(refs);
    return 1;
  }
  signal(SIGINT, on_sigint);
  signal(SIGTERM, on_sigint);
  signal(SIGPIPE, SIG_IGN);
  srand((unsigned int)time(NULL));
  uint64_t init_now = now_ms();
  setvbuf(stdout, NULL, _IOLBF, 0);
  int rooms_started = 0;
  for (int i = 0; i < room_count; i++) {
    struct room_config config = {
        .port = port_base + i,
        .zombies = room_zombies[i],
        .tick_hz = tick_hz,
        .lag_ms = lag_ms,
        .kill_limit = kill_limit,
        .intermission_ms = intermission_ms,
        .no_human_grace_ms = no_human_grace_ms,
        .brick_path = brick_path,
    };
    if (init_room(&rooms[i], &config, &bind_ip, init_now) != 0) {
      fprintf(stderr, "Failed to bind/start room %d on TCP port %d: %s\n", i,
              config.port, strerror(errno));
      for (int j = 0; j < rooms_started; j++) destroy_room(&rooms[j], debug);
      free(room_zombies);
      free(rooms);
      free(pfds);
      free(refs);
      return 1;
    }
    rooms_started++;
    printf("maze-war room %d listening on TCP port %d, %d Hz, zombies=%d\n",
           i, config.port, config.tick_hz, config.zombies);
  }

  while (g_running) {
    uint64_t now = now_ms();
    uint64_t earliest_tick = rooms[0].next_tick;
    size_t poll_index = 0;
    for (int r = 0; r < room_count; r++) {
      reap_timed_out_clients(&rooms[r], now, debug);
      if (rooms[r].next_tick < earliest_tick) earliest_tick = rooms[r].next_tick;
      pfds[poll_index] = (struct pollfd){.fd = rooms[r].listener_fd,
                                        .events = POLLIN};
      refs[poll_index++] = (struct poll_ref){r, POLL_LISTENER, -1,
                                             rooms[r].listener_fd};
      for (int i = 0; i < MAX_PLAYERS; i++) {
        int fd = rooms[r].clients[i].in_use ? rooms[r].clients[i].fd : -1;
        pfds[poll_index] = (struct pollfd){
            .fd = fd,
            .events = POLLIN | (rooms[r].clients[i].tx.len ? POLLOUT : 0)};
        refs[poll_index++] = (struct poll_ref){r, POLL_CLIENT, i, fd};
      }
      for (int i = 0; i < MAX_DEPARTING; i++) {
        struct departing_connection *departing = &rooms[r].departing[i];
        int fd = departing->in_use ? departing->fd : -1;
        pfds[poll_index] = (struct pollfd){
            .fd = fd,
            .events = POLLIN | (departing->tx.len ? POLLOUT : 0)};
        refs[poll_index++] = (struct poll_ref){r, POLL_DEPARTING, i, fd};
      }
    }
    uint64_t wait_ms = earliest_tick > now ? earliest_tick - now : 0;
    int timeout_ms = wait_ms > 1000 ? 1000 : (int)wait_ms;
    int pr = poll(pfds, poll_count, timeout_ms);
    now = now_ms();
    if (pr < 0 && errno != EINTR) {
      perror("poll");
      break;
    }
    /* Existing peers go first. The recorded fd check prevents an event for a
       closed descriptor from being applied to a new occupant after fd reuse. */
    for (size_t p = 0; pr > 0 && p < poll_count; p++) {
      if (refs[p].kind != POLL_CLIENT || pfds[p].revents == 0) continue;
      struct room *room = &rooms[refs[p].room_index];
      int i = refs[p].index;
      struct client_slot *client = &room->clients[i];
      if (!client->in_use || client->fd != refs[p].fd) continue;
      short events = pfds[p].revents;
      int failed = (events & (POLLERR | POLLNVAL)) != 0;
      if (!failed && (events & (POLLIN | POLLHUP))) {
        uint8_t buf[256];
        ssize_t n = recv(client->fd, buf, sizeof(buf), 0);
        if (n > 0) {
          client->last_seen_ms = now;
          if (debug) printf("room=%d RX(%zd) from slot %d\n",
                            room->config.port, n, i);
          transport_stats_note_raw_bytes(&client->transport, (size_t)n);
          transport_stats_note_raw_bytes(&room->global_transport, (size_t)n);
          process_client_bytes(room, i, buf, (size_t)n, debug, now);
        } else if (n == 0 || (errno != EINTR && errno != EAGAIN &&
                              errno != EWOULDBLOCK)) {
          failed = 1;
        }
      }
      if (client->leave_requested) {
        detach_voluntary_client(room, i, debug, now);
        continue;
      }
      if (!failed && (events & POLLOUT)) {
        failed = tcp_tx_flush(client->fd, &client->tx) < 0;
      }
      if (failed || client->tx.failed) {
        drop_client(room, i, debug, now);
      }
    }
    /* Departing sockets have no gameplay identity. They can only drain the
       queued ACK, accept an idempotent retry, close, or hit their deadline. */
    for (size_t p = 0; pr > 0 && p < poll_count; p++) {
      if (refs[p].kind != POLL_DEPARTING || pfds[p].revents == 0) continue;
      struct room *room = &rooms[refs[p].room_index];
      int i = refs[p].index;
      struct departing_connection *departing = &room->departing[i];
      if (!departing->in_use || departing->fd != refs[p].fd) continue;
      short events = pfds[p].revents;
      int failed = (events & (POLLERR | POLLNVAL)) != 0;
      if (!failed && (events & (POLLIN | POLLHUP))) {
        uint8_t buf[64];
        ssize_t n = recv(departing->fd, buf, sizeof(buf), 0);
        if (n > 0) {
          process_departing_bytes(departing, buf, (size_t)n);
        } else if (n == 0 || (errno != EINTR && errno != EAGAIN &&
                              errno != EWOULDBLOCK)) {
          failed = 1;
        }
      }
      if (!failed && (events & POLLOUT)) {
        failed = tcp_tx_flush(departing->fd, &departing->tx) < 0;
      }
      if (failed || departing->tx.failed || departing->tx.len == 0) {
        reset_departing(departing);
      }
    }
    /* Accept at most one peer per room per pass, keeping accept floods bounded. */
    for (size_t p = 0; pr > 0 && p < poll_count; p++) {
      if (refs[p].kind == POLL_LISTENER && (pfds[p].revents & POLLIN)) {
        accept_room_client(&rooms[refs[p].room_index], debug, now);
      }
    }
    now = now_ms();
    for (int r = 0; r < room_count; r++) {
      service_room_timers(&rooms[r], debug, now);
    }
    /* Every due room advances once before any room can advance again. */
    for (int r = 0; r < room_count; r++) {
      if (now >= rooms[r].next_tick) tick_room(&rooms[r], debug, now);
    }
  }

  for (int i = 0; i < room_count; i++) destroy_room(&rooms[i], debug);
  free(room_zombies);
  free(rooms);
  free(pfds);
  free(refs);
  if (debug) {
    puts("server stopped");
  }
  return 0;

bad_args:
  usage(argv[0]);
  return 1;
}
