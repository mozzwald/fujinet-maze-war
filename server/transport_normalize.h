#ifndef TRANSPORT_NORMALIZE_H
#define TRANSPORT_NORMALIZE_H

#include <stddef.h>
#include <stdint.h>

enum transport_rx_result {
  TRANSPORT_RX_NONE = 0,
  TRANSPORT_RX_PACKET = 1
};

enum transport_delta_format {
  TRANSPORT_DELTA_PRIMARY = 0,
  TRANSPORT_DELTA_SWAPPED = 1,
  TRANSPORT_DELTA_EXTRA_41 = 2
};

struct transport_rx_state {
  uint8_t need;
  uint8_t idx;
  uint8_t resync_count;
  uint8_t buf[16]; /* must hold the longest client->server packet (NAME, 11) */
};

struct transport_delta_packet {
  uint8_t seq;
  uint8_t pid;
  uint8_t joy;
  enum transport_delta_format format;
};

enum transport_rx_result transport_rx_push_byte(struct transport_rx_state *state,
                                                uint8_t slot,
                                                uint8_t byte,
                                                uint8_t *out_pkt,
                                                size_t out_pkt_cap,
                                                size_t *out_len);
uint32_t transport_rx_take_resync_count(struct transport_rx_state *state);

int transport_decode_delta_for_slot(const uint8_t *pkt, size_t len,
                                    uint8_t slot,
                                    struct transport_delta_packet *out);

#endif
