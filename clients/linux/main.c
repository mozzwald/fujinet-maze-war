#include <signal.h>
#include "../../net/tcp_stream.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#ifdef __linux__
#include <linux/input.h>
#endif
#include <ncurses.h>
#include <poll.h>
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
  PKT_ROUND_START = 0x55,
  PKT_LEAVE_ROOM = 0x56,
  PKT_LEAVE_ACK = 0x57
};

enum { MAX_PLAYERS = 4 };
enum { PROTOCOL_VERSION = 1, ROUND_PLAYING = 0, ROUND_OVER = 1 };
/* Matches the Atari HUD field; the server sanitizes and space-pads. */
enum { NAME_LEN = 8 };
enum { NAME_RESEND_MS = 2000 };

#ifdef __linux__
#define HAVE_EVDEV_INPUT 1
#else
#define HAVE_EVDEV_INPUT 0
#endif

struct player_state {
  uint8_t x;
  uint8_t y;
  uint8_t joy;
  uint8_t score;
};

struct shot_state {
  int active;
  uint8_t x;
  uint8_t y;
};

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Give the server a deterministic seat release while keeping quit bounded if
   the peer has vanished. Other frames are drained until our echoed sequence
   arrives; the socket is closed regardless after 750 ms. */
static void clean_leave(int sock, struct tcp_tx *tx, struct tcp_rx *rx,
                        uint8_t *seq) {
  uint8_t leave_seq = (*seq)++;
  uint8_t pkt[2] = {PKT_LEAVE_ROOM, leave_seq};
  if (tcp_tx_queue_frame(sock, tx, pkt, sizeof(pkt)) < 0) return;
  uint64_t deadline = now_ms() + 750;
  while (now_ms() < deadline) {
    if (tcp_tx_flush(sock, tx) < 0) return;
    for (int frames = 0; frames < 64; frames++) {
      uint8_t reply[64];
      int n = tcp_recv_frame(sock, rx, reply, sizeof(reply));
      if (n < 0) return;
      if (n == 0) break;
      if (n == 2 && reply[0] == PKT_LEAVE_ACK && reply[1] == leave_seq) {
        return;
      }
    }
    struct pollfd pfd = {.fd = sock, .events = POLLIN | POLLOUT};
    (void)poll(&pfd, 1, 20);
  }
}

static int round_is_newer(uint8_t candidate, uint8_t current) {
  uint8_t distance = (uint8_t)(candidate - current);
  return distance != 0 && distance < 0x80;
}

static uint8_t pack_joy(uint8_t stick, uint8_t trig) {
  uint8_t joy = stick & 0x0F;
  if (trig) {
    joy |= 0x10;
  }
  return joy;
}

static uint8_t compute_stick(int up, int down, int left, int right) {
  uint8_t stick = 0x0F;
  if (up) {
    stick &= (uint8_t)~0x01;
  }
  if (down) {
    stick &= (uint8_t)~0x02;
  }
  if (left) {
    stick &= (uint8_t)~0x04;
  }
  if (right) {
    stick &= (uint8_t)~0x08;
  }
  return stick;
}

/* A slot the server has no name for shows the role label instead, exactly as
   the Atari client does. */
static int name_is_set(const uint8_t *name) {
  for (int i = 0; i < NAME_LEN; i++) {
    if (name[i] != 0 && name[i] != ' ') {
      return 1;
    }
  }
  return 0;
}

/* A slot with neither a client nor a zombie in it is not in the game: it gets
   no scoreboard line and no marker on the board. Our own slot always counts, so
   the view is right before the first SEATS packet arrives. */
static int slot_in_play(uint8_t role_mask, uint8_t seat_mask, int local_pid,
                        int p) {
  return (role_mask & (1u << p)) || (seat_mask & (1u << p)) || p == local_pid;
}

static void draw_screen(const uint8_t *bricks, const struct player_state *ps,
                        const struct shot_state *shots,
                        const uint8_t names[MAX_PLAYERS][NAME_LEN],
                        uint8_t role_mask, uint8_t seat_mask, int local_pid,
                        int round_phase, uint8_t round_id,
                        uint8_t winner_pid, uint8_t kill_limit,
                        int ready) {
  for (int y = 0; y < 19; y++) {
    for (int x = 0; x < 20; x++) {
      int idx = y * 20 + x;
      int bit = (bricks[idx / 8] >> (idx % 8)) & 1;
      char ch = bit ? '#' : '.';
      for (int s = 0; s < MAX_PLAYERS; s++) {
        if (shots[s].active && shots[s].x == x && shots[s].y == y) {
          ch = '*';
        }
      }
      for (int p = 0; p < MAX_PLAYERS; p++) {
        if (ps[p].x == 255 && ps[p].y == 255) {
          continue;
        }
        if (!slot_in_play(role_mask, seat_mask, local_pid, p)) {
          continue;
        }
        if (ps[p].x == x && ps[p].y == y) {
          ch = (char)('0' + p);
        }
      }
      mvaddch(y, x, ch);
    }
  }
  for (int p = 0; p < MAX_PLAYERS; p++) {
    char label[NAME_LEN + 1];
    /* An empty seat has no name and no score: the zombie mask alone cannot
       tell one apart from a human who simply is not moving. */
    if (!slot_in_play(role_mask, seat_mask, local_pid, p)) {
      mvprintw(20 + p, 0, "%*s", NAME_LEN + 10, "");
      continue;
    }
    if (role_mask & (1u << p)) {
      snprintf(label, sizeof(label), "ZOMBIE");
    } else if (name_is_set(names[p])) {
      memcpy(label, names[p], NAME_LEN);
      label[NAME_LEN] = '\0';
      for (int i = NAME_LEN - 1; i >= 0 && label[i] == ' '; i--) {
        label[i] = '\0';
      }
    } else {
      snprintf(label, sizeof(label), "WIZARD");
    }
    mvprintw(20 + p, 0, "%c%d %-*s %3u   ", (p == local_pid) ? '>' : ' ', p,
             NAME_LEN, label, ps[p].score);
  }
  if (round_phase == ROUND_OVER) {
    mvprintw(0, 23, "ROUND %u OVER", (unsigned)round_id);
    mvprintw(1, 23, "PLAYER %u WINS", (unsigned)(winner_pid + 1));
    mvprintw(2, 23, "FIRST TO %u", (unsigned)kill_limit);
    mvprintw(3, 23, "NEXT ROUND SOON");
  } else if (!ready) {
    mvprintw(0, 23, "SYNCING ROUND %u", (unsigned)round_id);
  } else {
    for (int row = 0; row < 4; row++) mvprintw(row, 23, "%18s", "");
  }
  refresh();
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s [--host IP] [--port PORT] [--pid N] [--name NAME] "
          "[--input /dev/input/eventX] [--debug]\n"
          "  --name NAME  display name, up to 8 chars. The server folds it to\n"
          "               A-Z 0-9 space - . and pads it out.\n",
          argv0);
}

static void send_respawn(int sock, struct tcp_tx *tx,
                         uint8_t *seq, int local_pid, uint8_t round_id) {
  uint8_t pkt[7];
  pkt[0] = PKT_RESPAWN;
  pkt[1] = (*seq)++;
  pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
  pkt[3] = 0;
  pkt[4] = 0;
  pkt[5] = 0;
  pkt[6] = round_id;
  tcp_tx_queue_frame(sock, tx, pkt, sizeof(pkt));
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

static int reliable_inner_is_valid(const uint8_t *pkt, ssize_t len) {
  if (len == 5 && pkt[0] == PKT_BRICK_DELTA) {
    return 1;
  }
  if (len == 7 && pkt[0] == PKT_RESPAWN) {
    return 1;
  }
  if (len == 3 + NAME_LEN && pkt[0] == PKT_NAME) {
    return 1;
  }
  if (len == 52 && pkt[0] == PKT_BRICK_FULL) return 1;
  if (len == 43 && pkt[0] == PKT_MATCH_END) return 1;
  if (len == 3 && pkt[0] == PKT_ROUND_START) return 1;
  return 0;
}

static void handle_curses_key(int ch, int *up, int *down, int *left,
                              int *right, int *fire, int *running,
                              int sock, struct tcp_tx *tx,
                              uint8_t *seq, int local_pid, uint8_t round_id,
                              int gameplay_enabled) {
  switch (ch) {
    case KEY_UP:
    case 'w':
    case 'W':
      *up = 1;
      break;
    case KEY_DOWN:
    case 's':
    case 'S':
      *down = 1;
      break;
    case KEY_LEFT:
    case 'a':
    case 'A':
      *left = 1;
      break;
    case KEY_RIGHT:
    case 'd':
    case 'D':
      *right = 1;
      break;
    case ' ':
      *fire = 1;
      break;
    case 'r':
    case 'R':
      if (gameplay_enabled) send_respawn(sock, tx, seq, local_pid, round_id);
      break;
    case 27:
      *running = 0;
      break;
    default:
      break;
  }
}

int main(int argc, char **argv) {
  const char *host = "127.0.0.1";
  int port = 9000;
  int local_pid = -1;
  const char *input_path = NULL;
  const char *name = NULL;
  int debug = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      host = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
      local_pid = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_path = argv[++i];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[++i];
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

  if (local_pid < -1 || local_pid >= MAX_PLAYERS) {
    fprintf(stderr, "Invalid pid (0..3)\\n");
    return 1;
  }
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in srv;
  memset(&srv, 0, sizeof(srv));
  srv.sin_family = AF_INET;
  srv.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host, &srv.sin_addr) != 1) {
    fprintf(stderr, "Invalid host\\n");
    close(sock);
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);
  if (connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0 ||
      tcp_configure(sock) < 0) {
    perror("connect");
    close(sock);
    return 1;
  }
  struct tcp_tx tx = {0};
  struct tcp_rx rx = {0};
  int evfd = -1;
#if HAVE_EVDEV_INPUT
  if (input_path) {
    evfd = open(input_path, O_RDONLY | O_NONBLOCK);
    if (evfd < 0) {
      perror("open evdev");
      close(sock);
      return 1;
    }
  }
#endif

  initscr();
  cbreak();
  noecho();
  nodelay(stdscr, TRUE);
  keypad(stdscr, TRUE);
  curs_set(0);

  uint8_t bricks[48];
  memset(bricks, 0, sizeof(bricks));
  struct player_state players[MAX_PLAYERS];
  memset(players, 0, sizeof(players));
  struct shot_state shots[MAX_PLAYERS];
  memset(shots, 0, sizeof(shots));
  uint8_t names[MAX_PLAYERS][NAME_LEN];
  memset(names, 0, sizeof(names));
  uint8_t role_mask = 0;
  uint8_t seat_mask = 0;

  /* Our own name, space-padded the way the wire format wants it. */
  uint8_t my_name[NAME_LEN];
  memset(my_name, ' ', sizeof(my_name));
  if (name) {
    for (size_t i = 0; i < NAME_LEN && name[i]; i++) {
      my_name[i] = (uint8_t)name[i];
    }
  }
  uint64_t last_name_send_ms = 0;

  uint8_t last_joy = 0xFF;
  int up = 0, down = 0, left = 0, right = 0, fire = 0;
  uint64_t last_send_ms = 0;
  uint8_t seq = 0;
  uint16_t reliable_applied_rev = 0;
  int have_welcome = 0;
  uint8_t round_id = 0;
  int round_phase = ROUND_OVER;
  uint8_t kill_limit = 0;
  uint8_t winner_pid = 0;
  int round_authorized = 0;
  int round_map_ready = 0;
  int round_snapshot_ready = 0;
  uint64_t next_redraw = now_ms();

  {
    uint8_t hello[2] = {PKT_HELLO, PROTOCOL_VERSION};
    tcp_tx_queue_frame(sock, &tx, hello, sizeof(hello));
  }

  if (debug) {
    if (evfd >= 0) {
      printf("evdev input: %s\n", input_path);
    } else {
      printf("terminal input\n");
    }
  }

  int running = 1;
  while (running) {
    struct pollfd pfds[2];
    nfds_t nfds = 1;
    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    if (evfd >= 0) {
      pfds[1].fd = evfd;
      pfds[1].events = POLLIN;
      pfds[1].revents = 0;
      nfds = 2;
    }
    poll(pfds, nfds, 10);

    if (tcp_tx_flush(sock, &tx) < 0) break;
    /* Drain buffered frames even when poll has no new socket bytes. */
    for (int frames = 0; frames < 64; frames++) {
      uint8_t buf[64];
      int n = tcp_recv_frame(sock, &rx, buf, sizeof(buf));
      if (n < 0) { running = 0; break; }
      if (n == 0) break;
      const uint8_t *pkt = buf;
      ssize_t pkt_len = n;
      int reliable_inner = 0;
      if (n == 5 && buf[0] == PKT_WELCOME &&
          buf[1] == PROTOCOL_VERSION) {
        have_welcome = 1;
        round_id = buf[2];
        round_phase = buf[3];
        kill_limit = buf[4];
        round_authorized = 0;
        round_map_ready = 0;
        round_snapshot_ready = 0;
        continue;
      }
      if (n >= 2 && buf[0] == PKT_REJECT) {
        running = 0;
        break;
      }
      if (!have_welcome) continue;
      if (n >= 7 && buf[0] == PKT_RELIABLE_EVENT) {
        uint16_t rev = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
        pkt = &buf[4];
        pkt_len = n - 4;
        if (rev != (uint16_t)(reliable_applied_rev + 1) ||
            !reliable_inner_is_valid(pkt, pkt_len)) {
          send_reliable_ack(sock, &tx, &seq, reliable_applied_rev);
          continue;
        }
        reliable_applied_rev = rev;
        send_reliable_ack(sock, &tx, &seq, reliable_applied_rev);
        reliable_inner = 1;
      }
      if (pkt_len == 3 && pkt[0] == PKT_ROUND_START) {
        uint8_t incoming = pkt[1];
        if (incoming == round_id || round_is_newer(incoming, round_id)) {
          if (incoming != round_id || !round_authorized) {
            memset(shots, 0, sizeof(shots));
            round_map_ready = 0;
            round_snapshot_ready = 0;
          }
          round_id = incoming;
          kill_limit = pkt[2];
          round_phase = ROUND_PLAYING;
          round_authorized = 1;
          last_joy = 0xFF;
        }
      } else if (pkt_len == 43 && pkt[0] == PKT_MATCH_END) {
        if (pkt[1] == round_id) {
          round_phase = ROUND_OVER;
          winner_pid = pkt[2];
          role_mask = pkt[4];
          seat_mask = (uint8_t)(pkt[3] & (uint8_t)~pkt[4]);
          kill_limit = pkt[5];
          memcpy(&players[0].score, &pkt[6], 1);
          memcpy(&players[1].score, &pkt[7], 1);
          memcpy(&players[2].score, &pkt[8], 1);
          memcpy(&players[3].score, &pkt[9], 1);
          memcpy(names, &pkt[11], MAX_PLAYERS * NAME_LEN);
          memset(shots, 0, sizeof(shots));
          last_joy = 0xFF;
        }
      } else if (pkt_len == 52 && pkt[0] == PKT_BRICK_FULL &&
                 pkt[51] == round_id) {
        memcpy(bricks, &pkt[3], 48);
        if (reliable_inner) round_map_ready = 1;
      } else if (pkt_len == 5 && pkt[0] == PKT_BRICK_DELTA &&
                 pkt[4] == round_id && round_phase == ROUND_PLAYING) {
        uint8_t x = pkt[2];
        uint8_t y = pkt[3];
        if (x < 20 && y < 19) {
          int idx = y * 20 + x;
          bricks[idx / 8] &= (uint8_t)~(1u << (idx % 8));
        }
      } else if (pkt_len == 7 && pkt[0] == PKT_RESPAWN &&
                 pkt[6] == round_id && round_phase == ROUND_PLAYING) {
        uint8_t rp = pkt[2];
        if (rp < MAX_PLAYERS) {
          if (pkt[5] & 0x01) {
            players[rp].x = 255;
            players[rp].y = 255;
          } else {
            players[rp].x = pkt[3];
            players[rp].y = pkt[4];
          }
        }
      } else if (pkt_len >= 3 && pkt[0] == PKT_SEATS) {
        if (round_phase != ROUND_OVER)
          seat_mask = (uint8_t)(pkt[2] & 0x0F);
      } else if (pkt_len >= 3 + NAME_LEN && pkt[0] == PKT_NAME) {
        uint8_t np = pkt[2];
        if (np < MAX_PLAYERS && round_phase != ROUND_OVER) {
          memcpy(names[np], &pkt[3], NAME_LEN);
        }
      } else if (pkt_len == 7 && pkt[0] == PKT_SHOT &&
                 pkt[6] == round_id && round_phase == ROUND_PLAYING) {
        uint8_t sp = pkt[2];
        if (sp < MAX_PLAYERS) {
          shots[sp].x = pkt[3];
          shots[sp].y = pkt[4];
          shots[sp].active = pkt[5] ? 1 : 0;
        }
      } else if (pkt_len == 21 && pkt[0] == PKT_SNAPSHOT &&
                 pkt[20] == round_id && round_phase == ROUND_PLAYING) {
        round_snapshot_ready = 1;
        int snap_pid = (int)((pkt[2] >> 1) & 0x03);
        role_mask = (uint8_t)((pkt[2] >> 3) & 0x0F);
        int ack_valid = (pkt[2] & 0x80) != 0;
        uint8_t ack_seq = 0;
        if (pkt_len >= 20) {
          ack_seq = pkt[19];
        } else {
          ack_valid = 0;
        }
        if (local_pid != snap_pid) {
          local_pid = snap_pid;
          if (debug) {
            printf("local pid=%d (from snapshot flags)\n", local_pid);
          }
        }
        players[0].x = pkt[3];
        players[0].y = pkt[4];
        players[1].x = pkt[5];
        players[1].y = pkt[6];
        players[2].x = pkt[7];
        players[2].y = pkt[8];
        players[3].x = pkt[9];
        players[3].y = pkt[10];
        players[0].joy = pkt[11];
        players[1].joy = pkt[12];
        players[2].joy = pkt[13];
        players[3].joy = pkt[14];
        players[0].score = pkt[15];
        players[1].score = pkt[16];
        players[2].score = pkt[17];
        players[3].score = pkt[18];
        if (debug) {
          printf("snapshot ack pid=%d ack_valid=%d ack_seq=%u\n", snap_pid,
                 ack_valid, (unsigned)ack_seq);
        }
      }
    }

    if (!running) break;

    if (evfd >= 0 && (pfds[1].revents & POLLIN)) {
#if HAVE_EVDEV_INPUT
      struct input_event ev;
      ssize_t rd;
      while ((rd = read(evfd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type != EV_KEY) {
          continue;
        }
        if (ev.value == 2) {
          continue;
        }
        int pressed = (ev.value == 1);
        if (debug) {
          printf("EV key code=%u value=%d\n", ev.code, ev.value);
        }
        switch (ev.code) {
          case KEY_UP:
          case KEY_W:
          case KEY_KP8:
          case 103:
            up = pressed;
            break;
          case KEY_DOWN:
          case KEY_S:
          case KEY_KP2:
          case 108:
            down = pressed;
            break;
          case KEY_LEFT:
          case KEY_A:
          case KEY_KP4:
          case 105:
            left = pressed;
            break;
          case KEY_RIGHT:
          case 106:
          case KEY_D:
          case KEY_KP6:
            right = pressed;
            break;
          case KEY_SPACE:
            fire = pressed;
            break;
          case KEY_ESC:
            if (pressed) {
              running = 0;
            }
            break;
          case KEY_R:
            if (pressed) {
              if (round_authorized && round_map_ready && round_snapshot_ready &&
                  round_phase == ROUND_PLAYING)
                send_respawn(sock, &tx, &seq, local_pid, round_id);
            }
            break;
          default:
            break;
        }
      }
#endif
    }

    uint64_t now = now_ms();

    if (evfd < 0) {
      up = down = left = right = fire = 0;
      while (1) {
        int ch = getch();
        if (ch == ERR) {
          break;
        }
        handle_curses_key(ch, &up, &down, &left, &right, &fire, &running,
                          sock, &tx, &seq, local_pid, round_id,
                          round_authorized && round_map_ready &&
                              round_snapshot_ready &&
                              round_phase == ROUND_PLAYING);
      }
    }

    /* The server repeats names it knows, but it cannot repeat one it never
       received, so keep sending until our own slot comes back named. */
    if (have_welcome && name && now - last_name_send_ms >= NAME_RESEND_MS) {
      int known = (local_pid >= 0) && name_is_set(names[local_pid]);
      if (!known) {
        uint8_t pkt[3 + NAME_LEN];
        pkt[0] = PKT_NAME;
        pkt[1] = seq++;
        pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
        memcpy(&pkt[3], my_name, NAME_LEN);
        tcp_tx_queue_frame(sock, &tx, pkt, sizeof(pkt));
      }
      last_name_send_ms = now;
    }

    int gameplay_enabled = have_welcome && round_authorized &&
                           round_map_ready && round_snapshot_ready &&
                           round_phase == ROUND_PLAYING;
    uint8_t stick = gameplay_enabled ? compute_stick(up, down, left, right)
                                     : 0x0F;
    uint8_t joy = gameplay_enabled ? pack_joy(stick, (uint8_t)fire) : 0x0F;
    if (joy != last_joy || (joy != 0x0F && now - last_send_ms > 100) ||
        now - last_send_ms >= 1000) {
      uint8_t pkt[5];
      pkt[0] = PKT_DELTA;
      pkt[1] = seq++;
      pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
      pkt[3] = joy;
      pkt[4] = round_id;
      tcp_tx_queue_frame(sock, &tx, pkt, sizeof(pkt));
      last_joy = joy;
      last_send_ms = now;
    }

    if (now >= next_redraw) {
      draw_screen(bricks, players, shots, names, role_mask, seat_mask,
                  local_pid, round_phase, round_id, winner_pid, kill_limit,
                  gameplay_enabled);
      next_redraw = now + 33;
    }
  }

  endwin();
  if (evfd >= 0) {
    close(evfd);
  }
  if (have_welcome) clean_leave(sock, &tx, &rx, &seq);
  close(sock);
  return 0;
}
