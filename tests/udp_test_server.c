// Dumb UDP test server for FujiNet Maze War
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#define PORT 9000

// Atari STICK0 encoding (active low)
#define STICK_NEUTRAL 0x0F
#define STICK_UP 0x0E
#define STICK_DOWN 0x0D
#define STICK_LEFT 0x0B
#define STICK_RIGHT 0x07

static void print_rx(const unsigned char *buf, ssize_t len) {
  printf("RX(%zd):", len);
  for (ssize_t i = 0; i < len; i++) {
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

  if (set_nonblocking(sock) < 0 || set_nonblocking(STDIN_FILENO) < 0) {
    perror("nonblocking");
    close(sock);
    return 1;
  }

  struct termios oldt;
  if (tcgetattr(STDIN_FILENO, &oldt) < 0) {
    perror("tcgetattr");
    close(sock);
    return 1;
  }
  struct termios raw = oldt;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) < 0) {
    perror("tcsetattr");
    close(sock);
    return 1;
  }

  printf("UDP test server listening on port %d\n", PORT);
  printf("Arrow keys = joystick, space = trigger toggle, c = center, q = quit\n");

  unsigned char rxbuf[2048];
  unsigned char keybuf[64];
  unsigned char escbuf[8];
  size_t esclen = 0;
  bool have_peer = false;
  struct sockaddr_in peer;
  socklen_t peerlen = sizeof(peer);
  unsigned char seq = 0;
  unsigned char stick = STICK_NEUTRAL;
  unsigned char trig = 1; // STRIG0: 0 pressed, 1 released

  for (;;) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    FD_SET(STDIN_FILENO, &rfds);
    int maxfd = sock > STDIN_FILENO ? sock : STDIN_FILENO;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000;

    int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (ready < 0 && errno != EINTR) {
      perror("select");
      break;
    }

    if (FD_ISSET(sock, &rfds)) {
      ssize_t n = recvfrom(sock, rxbuf, sizeof(rxbuf), 0,
                           (struct sockaddr *)&peer, &peerlen);
      if (n > 0) {
        have_peer = true;
        print_rx(rxbuf, n);
      }
    }

    bool changed = false;
    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      ssize_t n = read(STDIN_FILENO, keybuf, sizeof(keybuf));
      if (n > 0) {
        for (ssize_t i = 0; i < n; i++) {
          unsigned char c = keybuf[i];
          if (esclen > 0 || c == 0x1B) {
            if (esclen < sizeof(escbuf)) {
              escbuf[esclen++] = c;
            }
            if (esclen >= 3) {
              if (escbuf[0] == 0x1B && escbuf[1] == '[') {
                switch (escbuf[2]) {
                case 'A':
                  stick = STICK_UP;
                  changed = true;
                  break;
                case 'B':
                  stick = STICK_DOWN;
                  changed = true;
                  break;
                case 'C':
                  stick = STICK_RIGHT;
                  changed = true;
                  break;
                case 'D':
                  stick = STICK_LEFT;
                  changed = true;
                  break;
                default:
                  break;
                }
              }
              esclen = 0;
            }
            continue;
          }

          if (c == 'q' || c == 'Q') {
            goto out;
          }
          if (c == 'c' || c == 'C') {
            stick = STICK_NEUTRAL;
            changed = true;
            continue;
          }
          if (c == ' ') {
            trig = trig ? 0 : 1;
            changed = true;
            continue;
          }
        }
      }
    }

    if (changed && have_peer) {
      unsigned char pkt[4];
      pkt[0] = 0xA5;
      pkt[1] = seq;
      pkt[2] = stick;
      pkt[3] = trig;
      sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&peer, peerlen);
      seq = (unsigned char)(seq + 1);
    }
  }

out:
  tcsetattr(STDIN_FILENO, TCSADRAIN, &oldt);
  close(sock);
  return 0;
}
