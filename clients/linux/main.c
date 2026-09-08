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
  PKT_BRICK_FULL = 0x50,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52
};

enum { MAX_PLAYERS = 4 };
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

/* Server frames are COBS-encoded with a trailing zero delimiter, so a byte
   lost on the Atari's SIO link cannot desynchronise its parser. Datagrams keep
   frame boundaries for us here, so decoding is all that is needed; the trailing
   checksum byte is left in place and simply ignored by the length checks. */
static ssize_t cobs_decode_inplace(uint8_t *buf, ssize_t n) {
  if (n <= 0) {
    return n;
  }
  if (buf[n - 1] == 0) {
    n--; /* drop the delimiter */
  }
  ssize_t rd = 0, wr = 0;
  while (rd < n) {
    uint8_t code = buf[rd++];
    if (code == 0) {
      return -1;
    }
    for (uint8_t i = 1; i < code && rd < n; i++) {
      buf[wr++] = buf[rd++];
    }
    if (code != 0xFF && rd < n) {
      buf[wr++] = 0;
    }
  }
  return wr;
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
                        uint8_t role_mask, uint8_t seat_mask, int local_pid) {
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

static void send_respawn(int sock, const struct sockaddr_in *srv,
                         uint8_t *seq, int local_pid) {
  uint8_t pkt[6];
  pkt[0] = PKT_RESPAWN;
  pkt[1] = (*seq)++;
  pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
  pkt[3] = 0;
  pkt[4] = 0;
  pkt[5] = 0;
  sendto(sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)srv, sizeof(*srv));
}

static void handle_curses_key(int ch, int *up, int *down, int *left,
                              int *right, int *fire, int *running,
                              int sock, const struct sockaddr_in *srv,
                              uint8_t *seq, int local_pid) {
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
      send_respawn(sock, srv, seq, local_pid);
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
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
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
  uint64_t next_redraw = now_ms();

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

    if (pfds[0].revents & POLLIN) {
      uint8_t buf[256];
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
      n = cobs_decode_inplace(buf, n);
      if (n >= 3 && buf[0] == PKT_BRICK_FULL && n >= 51) {
        memcpy(bricks, &buf[3], 48);
      } else if (n >= 4 && buf[0] == PKT_BRICK_DELTA) {
        uint8_t x = buf[2];
        uint8_t y = buf[3];
        if (x < 20 && y < 19) {
          int idx = y * 20 + x;
          bricks[idx / 8] &= (uint8_t)~(1u << (idx % 8));
        }
      } else if (n >= 6 && buf[0] == PKT_RESPAWN) {
        uint8_t rp = buf[2];
        if (rp < MAX_PLAYERS) {
          if (buf[5] & 0x01) {
            players[rp].x = 255;
            players[rp].y = 255;
          } else {
            players[rp].x = buf[3];
            players[rp].y = buf[4];
          }
        }
      } else if (n >= 3 && buf[0] == PKT_SEATS) {
        seat_mask = (uint8_t)(buf[2] & 0x0F);
      } else if (n >= 3 + NAME_LEN && buf[0] == PKT_NAME) {
        uint8_t np = buf[2];
        if (np < MAX_PLAYERS) {
          memcpy(names[np], &buf[3], NAME_LEN);
        }
      } else if (n >= 6 && buf[0] == PKT_SHOT) {
        uint8_t sp = buf[2];
        if (sp < MAX_PLAYERS) {
          shots[sp].x = buf[3];
          shots[sp].y = buf[4];
          shots[sp].active = buf[5] ? 1 : 0;
        }
      } else if (n >= 19 && buf[0] == PKT_SNAPSHOT) {
        int snap_pid = (int)((buf[2] >> 1) & 0x03);
        role_mask = (uint8_t)((buf[2] >> 3) & 0x0F);
        int ack_valid = (buf[2] & 0x80) != 0;
        uint8_t ack_seq = 0;
        if (n >= 20) {
          ack_seq = buf[19];
        } else {
          ack_valid = 0;
        }
        if (local_pid != snap_pid) {
          local_pid = snap_pid;
          if (debug) {
            printf("local pid=%d (from snapshot flags)\n", local_pid);
          }
        }
        players[0].x = buf[3];
        players[0].y = buf[4];
        players[1].x = buf[5];
        players[1].y = buf[6];
        players[2].x = buf[7];
        players[2].y = buf[8];
        players[3].x = buf[9];
        players[3].y = buf[10];
        players[0].joy = buf[11];
        players[1].joy = buf[12];
        players[2].joy = buf[13];
        players[3].joy = buf[14];
        players[0].score = buf[15];
        players[1].score = buf[16];
        players[2].score = buf[17];
        players[3].score = buf[18];
        if (debug) {
          printf("snapshot ack pid=%d ack_valid=%d ack_seq=%u\n", snap_pid,
                 ack_valid, (unsigned)ack_seq);
        }
      }
    }

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
              send_respawn(sock, &srv, &seq, local_pid);
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
                          sock, &srv, &seq, local_pid);
      }
    }

    /* The server repeats names it knows, but it cannot repeat one it never
       received, so keep sending until our own slot comes back named. */
    if (name && now - last_name_send_ms >= NAME_RESEND_MS) {
      int known = (local_pid >= 0) && name_is_set(names[local_pid]);
      if (!known) {
        uint8_t pkt[3 + NAME_LEN];
        pkt[0] = PKT_NAME;
        pkt[1] = seq++;
        pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
        memcpy(&pkt[3], my_name, NAME_LEN);
        sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&srv, sizeof(srv));
      }
      last_name_send_ms = now;
    }

    uint8_t stick = compute_stick(up, down, left, right);
    uint8_t joy = pack_joy(stick, (uint8_t)fire);
    if (joy != last_joy || (joy != 0x0F && now - last_send_ms > 100)) {
      uint8_t pkt[4];
      pkt[0] = PKT_DELTA;
      pkt[1] = seq++;
      pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
      pkt[3] = joy;
      sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&srv, sizeof(srv));
      last_joy = joy;
      last_send_ms = now;
    }

    if (now >= next_redraw) {
      draw_screen(bricks, players, shots, names, role_mask, seat_mask,
                  local_pid);
      next_redraw = now + 33;
    }
  }

  endwin();
  if (evfd >= 0) {
    close(evfd);
  }
  close(sock);
  return 0;
}
