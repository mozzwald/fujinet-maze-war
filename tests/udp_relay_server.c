// UDP relay server for FujiNet Maze War (input relay + heartbeat)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT 9000
#define MAX_CLIENTS 4
#define TIMEOUT_MS 2000

enum pkt_type {
  PKT_HELLO = 0x10,
  PKT_WELCOME = 0x11,
  PKT_INPUT = 0x20,
  PKT_INPUT_RELAY = 0x21,
  PKT_HEARTBEAT = 0x30,
  PKT_TIMEOUT = 0x31,
};

struct client {
  bool active;
  bool host;
  unsigned char id;
  struct sockaddr_in addr;
  socklen_t addrlen;
  long long last_seen_ms;
  unsigned char parse_type;
  unsigned char parse_need;
  unsigned char parse_idx;
  unsigned char parse_buf[4];
};

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
  return a->sin_family == b->sin_family && a->sin_port == b->sin_port &&
         a->sin_addr.s_addr == b->sin_addr.s_addr;
}

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

static void send_welcome(int sock, const struct client *cl) {
  unsigned char pkt[3];
  pkt[0] = PKT_WELCOME;
  pkt[1] = cl->id;
  pkt[2] = cl->host ? 0x01 : 0x00;
  sendto(sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)&cl->addr,
         cl->addrlen);
  print_tx(pkt, sizeof(pkt));
}

static void send_timeout(int sock, const struct client *dst, unsigned char id,
                         unsigned char reason) {
  unsigned char pkt[3];
  pkt[0] = PKT_TIMEOUT;
  pkt[1] = id;
  pkt[2] = reason;
  sendto(sock, pkt, sizeof(pkt), 0, (const struct sockaddr *)&dst->addr,
         dst->addrlen);
  print_tx(pkt, sizeof(pkt));
}

static int find_client_by_addr(struct client *clients,
                               const struct sockaddr_in *addr) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].addrlen && addr_equal(&clients[i].addr, addr)) {
      return i;
    }
  }
  return -1;
}

static int find_free_slot(struct client *clients) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i].addrlen) {
      return i;
    }
  }
  return -1;
}

static int find_free_id(struct client *clients) {
  bool used[MAX_CLIENTS] = {false};
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active) {
      used[clients[i].id] = true;
    }
  }
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!used[i]) {
      return i;
    }
  }
  return -1;
}

static int pick_host(struct client *clients) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active) {
      return i;
    }
  }
  return -1;
}

static unsigned char map_id_for_recipient(struct client *clients, int sender_idx,
                                          int recv_idx) {
  if (sender_idx == recv_idx) {
    return 0;
  }
  unsigned char id = 1;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i].active || i == recv_idx) {
      continue;
    }
    if (i == sender_idx) {
      return id;
    }
    id++;
  }
  return 0xFF;
}

static bool handle_parsed_packet(int sock, struct client *clients, int idx,
                                 unsigned char type, const unsigned char *buf,
                                 long long now) {
  if (type == PKT_HELLO) {
    unsigned char proto = buf[1];
    if (proto != 1) {
      return false;
    }
    if (!clients[idx].active) {
      clients[idx].active = true;
      int free_id = find_free_id(clients);
      clients[idx].id =
          (free_id >= 0) ? (unsigned char)free_id : (unsigned char)idx;
    }
    clients[idx].last_seen_ms = now;

    int host_idx = pick_host(clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      clients[i].host = false;
    }
    if (host_idx >= 0) {
      clients[host_idx].host = true;
    }
    send_welcome(sock, &clients[idx]);
    return true;
  }

  if (!clients[idx].active) {
    clients[idx].active = true;
    int free_id = find_free_id(clients);
    clients[idx].id = (free_id >= 0) ? (unsigned char)free_id
                                     : (unsigned char)idx;
    clients[idx].last_seen_ms = now;
    int host_idx = pick_host(clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
      clients[i].host = false;
    }
    if (host_idx >= 0) {
      clients[host_idx].host = true;
    }
    send_welcome(sock, &clients[idx]);
  }
  clients[idx].last_seen_ms = now;

  if (type == PKT_INPUT) {
    unsigned char relay[5];
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i].active) {
        continue;
      }
      unsigned char mapped = map_id_for_recipient(clients, idx, i);
      if (mapped == 0xFF) {
        continue;
      }
      relay[0] = PKT_INPUT_RELAY;
      relay[1] = mapped;
      relay[2] = buf[0]; // seq
      relay[3] = buf[1]; // stick
      relay[4] = buf[2]; // trig
      sendto(sock, relay, sizeof(relay), 0,
             (const struct sockaddr *)&clients[i].addr,
             clients[i].addrlen);
    }
    print_tx(relay, sizeof(relay));
  } else if (type == PKT_HEARTBEAT) {
    // keepalive only
  }
  return true;
}

static void parse_stream(int sock, struct client *clients, int idx,
                         const unsigned char *data, size_t len,
                         long long now) {
  for (size_t i = 0; i < len; i++) {
    unsigned char b = data[i];
    if (clients[idx].parse_need == 0) {
      if (b == PKT_HELLO) {
        clients[idx].parse_type = PKT_HELLO;
        clients[idx].parse_need = 2;
        clients[idx].parse_idx = 0;
      } else if (b == PKT_INPUT) {
        clients[idx].parse_type = PKT_INPUT;
        clients[idx].parse_need = 3;
        clients[idx].parse_idx = 0;
      } else if (b == PKT_HEARTBEAT) {
        clients[idx].parse_type = PKT_HEARTBEAT;
        clients[idx].parse_need = 1;
        clients[idx].parse_idx = 0;
      } else {
        continue;
      }
      continue;
    }

    clients[idx].parse_buf[clients[idx].parse_idx++] = b;
    if (clients[idx].parse_idx >= clients[idx].parse_need) {
      bool ok = handle_parsed_packet(sock, clients, idx, clients[idx].parse_type,
                                     clients[idx].parse_buf, now);
      clients[idx].parse_need = 0;
      clients[idx].parse_idx = 0;
      if (!ok) {
        unsigned char b0 = clients[idx].parse_buf[1];
        if (b0 == PKT_HELLO || b0 == PKT_INPUT || b0 == PKT_HEARTBEAT) {
          clients[idx].parse_type = b0;
          clients[idx].parse_need = (b0 == PKT_HELLO) ? 2 : (b0 == PKT_INPUT ? 3 : 1);
          clients[idx].parse_idx = 0;
        }
      }
    }
  }
}

int main(void) {
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
  addr.sin_port = htons(PORT);
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(sock);
    return 1;
  }

  printf("UDP relay server listening on port %d\n", PORT);

  struct client clients[MAX_CLIENTS];
  memset(clients, 0, sizeof(clients));

  unsigned char buf[2048];
  for (;;) {
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sock;
    pfd.events = POLLIN;
    int ready = poll(&pfd, 1, 100);
    if (ready < 0 && errno != EINTR) {
      perror("poll");
      break;
    }

    if (pfd.revents & POLLIN) {
      struct sockaddr_in src;
      socklen_t srclen = sizeof(src);
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src,
                           &srclen);
      if (n > 0) {
        print_rx(buf, n);
        long long now = now_ms();
        int idx = find_client_by_addr(clients, &src);
        if (idx < 0) {
          int free = find_free_slot(clients);
          if (free < 0) {
            continue;
          }
          idx = free;
          clients[idx].addr = src;
          clients[idx].addrlen = srclen;
        }
        parse_stream(sock, clients, idx, buf, (size_t)n, now);
      }
    }

    long long now = now_ms();
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i].active) {
        continue;
      }
      if (now - clients[i].last_seen_ms > TIMEOUT_MS) {
        unsigned char dead_id = clients[i].id;
        clients[i].active = false;
        clients[i].host = false;
        clients[i].addrlen = 0;
        for (int j = 0; j < MAX_CLIENTS; j++) {
          if (clients[j].active) {
            send_timeout(sock, &clients[j], dead_id, 1);
          }
        }
      }
    }
  }

  close(sock);
  return 0;
}
