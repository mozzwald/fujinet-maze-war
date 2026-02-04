// UDP relay client for FujiNet Maze War (evdev input)
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 9000

// Atari STICK0 encoding (active low)
#define STICK_NEUTRAL 0x0F
#define STICK_UP 0x0E
#define STICK_DOWN 0x0D
#define STICK_LEFT 0x0B
#define STICK_RIGHT 0x07

enum pkt_type {
  PKT_HELLO = 0x10,
  PKT_WELCOME = 0x11,
  PKT_INPUT = 0x20,
  PKT_INPUT_RELAY = 0x21,
  PKT_HEARTBEAT = 0x30,
  PKT_TIMEOUT = 0x31,
};

struct key_config {
  int up;
  int down;
  int left;
  int right;
  int fire;
};

struct input_state {
  bool up;
  bool down;
  bool left;
  bool right;
  bool fire;
  unsigned int up_ts;
  unsigned int down_ts;
  unsigned int left_ts;
  unsigned int right_ts;
};


static void print_rx(const unsigned char *buf, ssize_t len) {
  printf("RX(%zd):", len);
  for (ssize_t i = 0; i < len; i++) {
    printf(" %02X", buf[i]);
  }
  printf("\n");
  fflush(stdout);
}

static void print_tx(const unsigned char *buf, size_t len) {
  printf("TX(%zu):", len);
  for (size_t i = 0; i < len; i++) {
    printf(" %02X", buf[i]);
  }
  printf("\n");
  fflush(stdout);
}

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return -1;
  }
  return 0;
}

static int test_bit(const unsigned long *bits, int bit) {
  return (bits[bit / (int)(8 * sizeof(unsigned long))] >>
          (bit % (int)(8 * sizeof(unsigned long)))) &
         1UL;
}

static bool is_keyboard_device(int fd) {
  unsigned long ev_bits[(EV_MAX + 1 + 8 * sizeof(unsigned long) - 1) /
                        (8 * sizeof(unsigned long))];
  unsigned long key_bits[(KEY_MAX + 1 + 8 * sizeof(unsigned long) - 1) /
                         (8 * sizeof(unsigned long))];
  memset(ev_bits, 0, sizeof(ev_bits));
  memset(key_bits, 0, sizeof(key_bits));

  if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
    return false;
  }
  if (!test_bit(ev_bits, EV_KEY)) {
    return false;
  }
  if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
    return false;
  }

  return test_bit(key_bits, KEY_A) && test_bit(key_bits, KEY_SPACE) &&
         test_bit(key_bits, KEY_ENTER);
}

static int open_evdev_device(const char *path, char *name_buf, size_t name_len) {
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0) {
    return -1;
  }
  if (!is_keyboard_device(fd)) {
    close(fd);
    return -1;
  }
  if (ioctl(fd, EVIOCGNAME((int)name_len), name_buf) < 0) {
    strncpy(name_buf, "(unknown)", name_len - 1);
    name_buf[name_len - 1] = '\0';
  }
  return fd;
}

static int pick_keyboard(char *path_buf, size_t path_len, char *name_buf,
                         size_t name_len) {
  DIR *dir = opendir("/dev/input");
  if (!dir) {
    return -1;
  }
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strncmp(ent->d_name, "event", 5) != 0) {
      continue;
    }
    char path[256];
    int plen = snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
    if (plen < 0 || (size_t)plen >= sizeof(path)) {
      continue;
    }
    int fd = open_evdev_device(path, name_buf, name_len);
    if (fd >= 0) {
      if (path_len > 0) {
        size_t copy_len = strlen(path);
        if (copy_len >= path_len) {
          copy_len = path_len - 1;
        }
        memcpy(path_buf, path, copy_len);
        path_buf[copy_len] = '\0';
      }
      closedir(dir);
      return fd;
    }
  }
  closedir(dir);
  return -1;
}

static int config_to_evdev_keycode(int key) {
  return key;
}

static unsigned char compute_stick(const struct input_state *st) {
  unsigned int best_ts = 0;
  unsigned char stick = STICK_NEUTRAL;

  if (st->up && st->up_ts >= best_ts) {
    best_ts = st->up_ts;
    stick = STICK_UP;
  }
  if (st->down && st->down_ts >= best_ts) {
    best_ts = st->down_ts;
    stick = STICK_DOWN;
  }
  if (st->left && st->left_ts >= best_ts) {
    best_ts = st->left_ts;
    stick = STICK_LEFT;
  }
  if (st->right && st->right_ts >= best_ts) {
    best_ts = st->right_ts;
    stick = STICK_RIGHT;
  }

  return stick;
}


int main(int argc, char **argv) {
  const struct key_config cfg = {
      .up = KEY_UP,
      .down = KEY_DOWN,
      .left = KEY_LEFT,
      .right = KEY_RIGHT,
      .fire = KEY_SPACE,
  };

  int key_up = config_to_evdev_keycode(cfg.up);
  int key_down = config_to_evdev_keycode(cfg.down);
  int key_left = config_to_evdev_keycode(cfg.left);
  int key_right = config_to_evdev_keycode(cfg.right);
  int key_fire = config_to_evdev_keycode(cfg.fire);

  if (key_up < 0 || key_down < 0 || key_left < 0 || key_right < 0 ||
      key_fire < 0) {
    fprintf(stderr, "Invalid key mapping in config.\n");
    return 1;
  }

  const char *input_path = NULL;
  const char *server_host = "127.0.0.1";
  bool grab_device = false;
  bool debug_raw = false;
  int client_id = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_path = argv[i + 1];
      i++;
    } else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
      server_host = argv[i + 1];
      i++;
    } else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
      client_id = atoi(argv[i + 1]);
      i++;
    } else if (strcmp(argv[i], "--grab") == 0) {
      grab_device = true;
    } else if (strcmp(argv[i], "--debug") == 0) {
      debug_raw = true;
    }
  }

  char dev_path[256] = {0};
  char dev_name[256] = {0};
  int evfd = -1;
  if (input_path) {
    evfd = open_evdev_device(input_path, dev_name, sizeof(dev_name));
    if (evfd < 0) {
      if (errno == EACCES) {
        fprintf(stderr,
                "Cannot open %s (EACCES). Try sudo or add your user to the "
                "input group.\n",
                input_path);
      } else {
        perror("open evdev");
      }
      return 1;
    }
    strncpy(dev_path, input_path, sizeof(dev_path) - 1);
  } else {
    evfd = pick_keyboard(dev_path, sizeof(dev_path), dev_name,
                         sizeof(dev_name));
    if (evfd < 0) {
      fprintf(stderr,
              "Failed to auto-pick a keyboard device in /dev/input. Use "
              "--input /dev/input/eventX\n");
      return 1;
    }
  }

  printf("EVDEV input: %s (%s)\n", dev_path, dev_name);
  if (grab_device) {
    if (ioctl(evfd, EVIOCGRAB, 1) < 0) {
      perror("EVIOCGRAB");
    } else {
      printf("EVDEV grab: enabled\n");
    }
  }

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("socket");
    close(evfd);
    return 1;
  }

  if (set_nonblocking(sock) < 0) {
    perror("nonblocking");
    close(sock);
    close(evfd);
    return 1;
  }

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  if (inet_pton(AF_INET, server_host, &server.sin_addr) != 1) {
    fprintf(stderr, "Invalid server address: %s\n", server_host);
    close(sock);
    close(evfd);
    return 1;
  }
  if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("connect");
    close(sock);
    close(evfd);
    return 1;
  }

  printf("UDP relay client to %s:%d\n", server_host, PORT);

  unsigned char seq = 0;
  unsigned char stick = STICK_NEUTRAL;
  unsigned char trig = 1; // STRIG0: 0 pressed, 1 released
  struct input_state st = {0};
  unsigned int press_clock = 1;

  unsigned char hello[3] = {PKT_HELLO, (unsigned char)client_id, 1};
  send(sock, hello, sizeof(hello), 0);
  print_tx(hello, sizeof(hello));

  unsigned char rxbuf[2048];
  for (;;) {
    struct pollfd fds[2];
    memset(fds, 0, sizeof(fds));
    fds[0].fd = sock;
    fds[0].events = POLLIN;
    fds[1].fd = evfd;
    fds[1].events = POLLIN;

    int ready = poll(fds, 2, 50);
    if (ready < 0 && errno != EINTR) {
      perror("poll");
      break;
    }

    if (fds[0].revents & POLLIN) {
      ssize_t n = recv(sock, rxbuf, sizeof(rxbuf), 0);
      if (n > 0) {
        print_rx(rxbuf, n);
      }
    }

    if (fds[1].revents & POLLIN) {
      struct input_event ev;
      ssize_t n;
      while ((n = read(evfd, &ev, sizeof(ev))) == sizeof(ev)) {
        if (ev.type != EV_KEY) {
          continue;
        }
        if (ev.value == 2) {
          continue; // ignore autorepeat
        }

        bool changed = false;
        bool pressed = (ev.value == 1);
        if (debug_raw) {
          printf("EVDEV raw: code=%u value=%d\n", ev.code, ev.value);
        }
        if (ev.code == key_up) {
          st.up = pressed;
          if (pressed) {
            st.up_ts = press_clock++;
          }
          printf("EVDEV %s: KEY_UP -> UP=%d\n", pressed ? "press" : "release",
                 st.up ? 1 : 0);
          changed = true;
        } else if (ev.code == key_down) {
          st.down = pressed;
          if (pressed) {
            st.down_ts = press_clock++;
          }
          printf("EVDEV %s: KEY_DOWN -> DOWN=%d\n",
                 pressed ? "press" : "release", st.down ? 1 : 0);
          changed = true;
        } else if (ev.code == key_left) {
          st.left = pressed;
          if (pressed) {
            st.left_ts = press_clock++;
          }
          printf("EVDEV %s: KEY_LEFT -> LEFT=%d\n",
                 pressed ? "press" : "release", st.left ? 1 : 0);
          changed = true;
        } else if (ev.code == key_right) {
          st.right = pressed;
          if (pressed) {
            st.right_ts = press_clock++;
          }
          printf("EVDEV %s: KEY_RIGHT -> RIGHT=%d\n",
                 pressed ? "press" : "release", st.right ? 1 : 0);
          changed = true;
        } else if (ev.code == key_fire) {
          st.fire = pressed;
          printf("EVDEV %s: KEY_SPACE -> FIRE=%d\n",
                 pressed ? "press" : "release", st.fire ? 1 : 0);
          changed = true;
        } else if (debug_raw) {
          printf("EVDEV unmapped: code=%u value=%d\n", ev.code, ev.value);
        }

        if (changed) {
          unsigned char new_stick = compute_stick(&st);
          unsigned char new_trig = st.fire ? 0 : 1;
          if (new_stick != stick || new_trig != trig) {
            stick = new_stick;
            trig = new_trig;
            unsigned char pkt[4];
            pkt[0] = PKT_INPUT;
            pkt[1] = seq;
            pkt[2] = stick;
            pkt[3] = trig;
            send(sock, pkt, sizeof(pkt), 0);
            print_tx(pkt, sizeof(pkt));
            seq = (unsigned char)(seq + 1);
          }
        }
      }
    }

    if ((press_clock & 0x1F) == 0) {
      unsigned char hb[2] = {PKT_HEARTBEAT, (unsigned char)press_clock};
      send(sock, hb, sizeof(hb), 0);
      print_tx(hb, sizeof(hb));
    }
  }

  close(sock);
  if (grab_device) {
    ioctl(evfd, EVIOCGRAB, 0);
  }
  close(evfd);
  return 0;
}
