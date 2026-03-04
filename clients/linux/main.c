#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <linux/input.h>
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
  PKT_BRICK_FULL = 0x50,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52
};

enum { MAX_PLAYERS = 4 };

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

static void draw_screen(const uint8_t *bricks, const struct player_state *ps,
                        const struct shot_state *shots) {
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
        if (ps[p].x == x && ps[p].y == y) {
          ch = (char)('0' + p);
        }
      }
      mvaddch(y, x, ch);
    }
  }
  mvprintw(20, 0, "Scores: %u %u %u %u",
           ps[0].score, ps[1].score, ps[2].score, ps[3].score);
  refresh();
}

static void usage(const char *argv0) {
  fprintf(stderr,
          "Usage: %s [--host IP] [--port PORT] [--pid N] --input /dev/input/eventX [--debug]\n",
          argv0);
}

int main(int argc, char **argv) {
  const char *host = "127.0.0.1";
  int port = 9000;
  int local_pid = -1;
  const char *input_path = NULL;
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
  if (!input_path) {
    fprintf(stderr, "Missing --input /dev/input/eventX\n");
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

  int evfd = open(input_path, O_RDONLY | O_NONBLOCK);
  if (evfd < 0) {
    perror("open evdev");
    close(sock);
    return 1;
  }

  initscr();
  cbreak();
  noecho();
  nodelay(stdscr, TRUE);
  curs_set(0);

  uint8_t bricks[48];
  memset(bricks, 0, sizeof(bricks));
  struct player_state players[MAX_PLAYERS];
  memset(players, 0, sizeof(players));
  struct shot_state shots[MAX_PLAYERS];
  memset(shots, 0, sizeof(shots));

  uint8_t last_joy = 0xFF;
  int up = 0, down = 0, left = 0, right = 0, fire = 0;
  uint64_t last_send_ms = 0;
  uint8_t seq = 0;
  uint64_t next_redraw = now_ms();

  if (debug) {
    printf("evdev input: %s\n", input_path);
  }

  while (1) {
    struct pollfd pfds[2];
    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = evfd;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
    poll(pfds, 2, 10);

    if (pfds[0].revents & POLLIN) {
      uint8_t buf[256];
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
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
      } else if (n >= 6 && buf[0] == PKT_SHOT) {
        uint8_t sp = buf[2];
        if (sp < MAX_PLAYERS) {
          shots[sp].x = buf[3];
          shots[sp].y = buf[4];
          shots[sp].active = buf[5] ? 1 : 0;
        }
      } else if (n >= 19 && buf[0] == PKT_SNAPSHOT) {
        int snap_pid = (int)((buf[2] >> 1) & 0x03);
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
      }
    }

    if (pfds[1].revents & POLLIN) {
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
              endwin();
              close(evfd);
              close(sock);
              return 0;
            }
            break;
          case KEY_R:
            if (pressed) {
              uint8_t pkt[6];
              pkt[0] = PKT_RESPAWN;
              pkt[1] = seq++;
              pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
              pkt[3] = 0;
              pkt[4] = 0;
              pkt[5] = 0;
              sendto(sock, pkt, sizeof(pkt), 0,
                     (struct sockaddr *)&srv, sizeof(srv));
            }
            break;
          default:
            break;
        }
      }
    }

    uint64_t now = now_ms();

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
      draw_screen(bricks, players, shots);
      next_redraw = now + 33;
    }
  }
}
