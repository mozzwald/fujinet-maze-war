#ifndef NET_TCP_STREAM_H
#define NET_TCP_STREAM_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define TCP_TX_CAP 4096

/* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xffff.  The wire
 * trailer is little endian so the 6502 can append its low accumulator first. */
static inline uint16_t crc16_ccitt_false(const unsigned char *data, size_t len) {
    uint16_t crc = 0xffff;
    while (len--) {
        crc ^= (uint16_t)*data++ << 8;
        for (unsigned int bit = 0; bit != 8; bit++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                   : (uint16_t)(crc << 1);
    }
    return crc;
}

struct tcp_tx {
    unsigned char data[TCP_TX_CAP];
    size_t len;
    int failed;
};

static inline int tcp_configure(int fd) {
    int opt = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static inline int tcp_tx_flush(int fd, struct tcp_tx *tx) {
    if (tx->failed) {
        return -1;
    }
    if (tx->len == 0) {
        return 0;
    }

    size_t offset = 0;
    while (offset < tx->len) {
        ssize_t n = send(fd, tx->data + offset, tx->len - offset, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                memmove(tx->data, tx->data + offset, tx->len - offset);
                tx->len -= offset;
                return 0;
            }
            tx->failed = 1;
            return -1;
        }
        if (n == 0) {
            tx->failed = 1;
            return -1;
        }
        offset += n;
    }

    tx->len = 0;
    return 0;
}

static inline int tcp_tx_queue(int fd, struct tcp_tx *tx, const void *data, size_t len) {
    if (tx->failed) {
        return -1;
    }

    if (tcp_tx_flush(fd, tx) < 0) {
        return -1;
    }

    if (len > TCP_TX_CAP - tx->len) {
        tx->failed = 1;
        errno = ENOBUFS;
        return -1;
    }

    memcpy(tx->data + tx->len, data, len);
    tx->len += len;

    if (tcp_tx_flush(fd, tx) < 0) {
        return -1;
    }

    return 0;
}

static inline int tcp_tx_queue_frame(int fd, struct tcp_tx *tx,
                                     const unsigned char *payload, size_t len) {
    unsigned char raw[66], encoded[68];
    size_t rd = 0, wr = 1, code_at = 0;
    unsigned char code = 1;
    if (len > sizeof(raw) - 2) { errno = EMSGSIZE; return -1; }
    memcpy(raw, payload, len);
    uint16_t crc = crc16_ccitt_false(raw, len);
    raw[len++] = (unsigned char)crc;
    raw[len++] = (unsigned char)(crc >> 8);
    while (rd < len) {
        if (raw[rd] == 0) {
            encoded[code_at] = code;
            code_at = wr++;
            code = 1;
            rd++;
        } else {
            encoded[wr++] = raw[rd++];
            if (++code == 0xff) {
                encoded[code_at] = code;
                code_at = wr++;
                code = 1;
            }
        }
    }
    encoded[code_at] = code;
    encoded[wr++] = 0;
    return tcp_tx_queue(fd, tx, encoded, wr);
}

struct tcp_rx {
    unsigned char input[1024];
    size_t pos;
    size_t len;
    unsigned char frame[64];
    size_t used;
    int discarding;
};

struct tcp_frame_rx {
    unsigned char encoded[66];
    size_t used;
    int discarding;
};

/* Feed a COBS-delimited frame a byte at a time.  Returns a payload length,
 * zero while incomplete, or -1 for a complete invalid frame. */
static inline int tcp_frame_push_byte(struct tcp_frame_rx *state,
                                      unsigned char byte, unsigned char *out,
                                      size_t cap) {
    if (byte != 0) {
        if (!state->discarding) {
            if (state->used == sizeof(state->encoded)) state->discarding = 1;
            else state->encoded[state->used++] = byte;
        }
        return 0;
    }
    size_t len = state->used, rd = 0, wr = 0;
    int invalid = state->discarding || len == 0;
    state->used = 0;
    state->discarding = 0;
    while (!invalid && rd < len) {
        unsigned char code = state->encoded[rd++];
        if (!code || (size_t)(code - 1) > len - rd) { invalid = 1; break; }
        for (unsigned int i = 1; i < code; i++) state->encoded[wr++] = state->encoded[rd++];
        if (code != 0xff && rd < len) state->encoded[wr++] = 0;
    }
    if (invalid || wr < 3 || wr - 2 > cap) return -1;
    uint16_t crc = crc16_ccitt_false(state->encoded, wr - 2);
    if (state->encoded[wr - 2] != (unsigned char)crc ||
        state->encoded[wr - 1] != (unsigned char)(crc >> 8)) return -1;
    memcpy(out, state->encoded, wr - 2);
    return (int)(wr - 2);
}

/* Consume arbitrary stream fragments; reject a damaged frame as a whole. */
static inline int tcp_recv_frame(int fd, struct tcp_rx *rx,
                                 unsigned char *out, size_t cap) {
    int received = 0;
    for (;;) {
        if (rx->pos == rx->len) {
            if (received) return 0;
            ssize_t n;
            do {
                n = recv(fd, rx->input, sizeof(rx->input), 0);
            } while (n < 0 && errno == EINTR);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
                return -1;
            }
            if (n == 0) { errno = ECONNRESET; return -1; }
            rx->pos = 0;
            rx->len = (size_t)n;
            received = 1;
        }
        unsigned char b = rx->input[rx->pos++];
        if (b != 0) {
            if (!rx->discarding) {
                if (rx->used == sizeof(rx->frame)) rx->discarding = 1;
                else rx->frame[rx->used++] = b;
            }
            continue;
        }
        size_t len = rx->used;
        rx->used = 0;
        if (rx->discarding) { rx->discarding = 0; continue; }
        if (!len) continue;
        size_t rd = 0, wr = 0;
        int valid = 1;
        while (rd < len) {
            unsigned char code = rx->frame[rd++];
            if (!code || (size_t)(code - 1) > len - rd) {
                valid = 0;
                break;
            }
            for (unsigned int i = 1; i < code; i++)
                rx->frame[wr++] = rx->frame[rd++];
            if (code != 255 && rd < len) rx->frame[wr++] = 0;
        }
        if (!valid || wr < 3 || wr - 2 > cap) continue;
        uint16_t crc = crc16_ccitt_false(rx->frame, wr - 2);
        if (rx->frame[wr - 2] != (unsigned char)crc ||
            rx->frame[wr - 1] != (unsigned char)(crc >> 8)) continue;
        memcpy(out, rx->frame, wr - 2);
        return (int)(wr - 2);
    }
}

#endif
