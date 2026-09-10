/* Deterministic short-write/error injection plus real fragmented RX streams. */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/socket.h>
#include <string.h>

static int send_mode, send_calls;
static unsigned char sent[8192];
static size_t sent_len;
static ssize_t test_send(int fd, const void *data, size_t n, int flags) {
  (void)fd; (void)flags;
  send_calls++;
  if (send_mode == 1 && send_calls == 1) { errno = EINTR; return -1; }
  if (send_mode == 1 && send_calls == 3) { errno = EAGAIN; return -1; }
  if (send_mode == 2) { errno = EAGAIN; return -1; }
  if (send_mode == 3) { errno = EPIPE; return -1; }
  if (send_mode == 1 && n > 2) n = 2;
  assert(sent_len + n <= sizeof(sent));
  memcpy(sent + sent_len, data, n);
  sent_len += n;
  return (ssize_t)n;
}
#define send test_send
#include "../net/tcp_stream.h"
#undef send

static void expect_packet(int fd, struct tcp_rx *rx, const unsigned char *p, int n) {
  unsigned char out[64];
  assert(tcp_recv_frame(fd, rx, out, sizeof(out)) == n);
  assert(memcmp(out, p, (size_t)n) == 0);
}

static size_t encode_frame(const unsigned char *payload, size_t len,
                           unsigned char *out) {
  unsigned char raw[64];
  size_t rd = 0, wr = 1, code_at = 0;
  unsigned char code = 1;
  uint16_t crc = crc16_ccitt_false(payload, len);
  memcpy(raw, payload, len);
  raw[len++] = (unsigned char)crc;
  raw[len++] = (unsigned char)(crc >> 8);
  while (rd < len) {
    if (raw[rd] == 0) {
      out[code_at] = code;
      code_at = wr++;
      code = 1;
      rd++;
    } else {
      out[wr++] = raw[rd++];
      code++;
    }
  }
  out[code_at] = code;
  out[wr++] = 0;
  return wr;
}

int main(void) {
  struct tcp_tx tx = {0};
  send_mode = 1;
  assert(tcp_tx_queue(-1, &tx, "abcdef", 6) == 0);
  assert(sent_len == 2 && tx.len == 4);
  assert(memcmp(tx.data, "cdef", 4) == 0);
  assert(tcp_tx_queue(-1, &tx, "gh", 2) == 0);
  assert(sent_len == 8 && memcmp(sent, "abcdefgh", 8) == 0 && tx.len == 0);
  send_mode = 2;
  unsigned char full[TCP_TX_CAP] = {0};
  assert(tcp_tx_queue(-1, &tx, full, sizeof(full)) == 0);
  assert(tx.len == sizeof(full));
  assert(tcp_tx_queue(-1, &tx, "x", 1) == -1 && tx.failed);
  tx = (struct tcp_tx){0};
  send_mode = 3;
  assert(tcp_tx_queue(-1, &tx, "x", 1) == -1 && tx.failed);

  /* Outbound production frames include a CRC and can round-trip through the
   * same byte-at-a-time parser used by the server. */
  tx = (struct tcp_tx){0};
  sent_len = 0;
  send_calls = 0;
  send_mode = 0;
  const unsigned char pc[] = {0x41, 0x15, 0, 0x0f};
  assert(tcp_tx_queue_frame(-1, &tx, pc, sizeof(pc)) == 0);
  struct tcp_frame_rx frame_rx = {0};
  unsigned char frame_out[64];
  int frame_len = 0;
  for (size_t i = 0; i < sent_len; i++) {
    int n = tcp_frame_push_byte(&frame_rx, sent[i], frame_out, sizeof(frame_out));
    if (n) frame_len = n;
  }
  assert(frame_len == (int)sizeof(pc));
  assert(memcmp(frame_out, pc, sizeof(pc)) == 0);

  int fds[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  assert(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);
  struct tcp_rx rx = {0};
  unsigned char out[64];
  const unsigned char pa[] = {0x44, 0, 1};
  const unsigned char pb[] = {0x42, 1, 0, 2, 3, 0};
  unsigned char a[16], b[16];
  size_t a_len = encode_frame(pa, sizeof(pa), a);
  size_t b_len = encode_frame(pb, sizeof(pb), b);
  for (size_t i = 0; i < a_len - 1; i++) {
    assert(send(fds[0], a+i, 1, 0) == 1);
    assert(tcp_recv_frame(fds[1], &rx, out, sizeof(out)) == 0);
  }
  assert(send(fds[0], a+a_len-1, 1, 0) == 1);
  expect_packet(fds[1], &rx, pa, sizeof(pa));
  unsigned char both[32];
  memcpy(both, a, a_len); memcpy(both+a_len, b, b_len);
  assert(send(fds[0], both, a_len+b_len, 0) == (ssize_t)(a_len+b_len));
  expect_packet(fds[1], &rx, pa, sizeof(pa));
  expect_packet(fds[1], &rx, pb, sizeof(pb));
  const unsigned char bad[] = {0, 9, 1, 0, 3, 0x44, 1, 0};
  assert(send(fds[0], bad, sizeof(bad), 0) == sizeof(bad));
  assert(send(fds[0], a, a_len, 0) == (ssize_t)a_len);
  expect_packet(fds[1], &rx, pa, sizeof(pa));
  unsigned char oversized[80]; memset(oversized, 1, sizeof(oversized));
  oversized[79] = 0;
  assert(send(fds[0], oversized, sizeof(oversized), 0) == sizeof(oversized));
  assert(send(fds[0], a, a_len, 0) == (ssize_t)a_len);
  expect_packet(fds[1], &rx, pa, sizeof(pa));
  assert(send(fds[0], both, a_len+b_len, 0) == (ssize_t)(a_len+b_len));
  assert(tcp_recv_frame(fds[1], &rx, out, 2) == 0);
  close(fds[0]);
  assert(tcp_recv_frame(fds[1], &rx, out, sizeof(out)) == -1);
  close(fds[1]);
  return 0;
}
