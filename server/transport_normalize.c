#include "transport_normalize.h"

#include <string.h>

enum {
  PKT_DELTA = 0x41,
  PKT_BRICK_DELTA = 0x51,
  PKT_RESPAWN = 0x52,
  MAX_PLAYERS = 4
};

static int is_valid_stick_nibble(uint8_t stick) {
  switch (stick) {
    case 0x0F:
    case 0x07:
    case 0x0D:
    case 0x0B:
    case 0x0E:
      return 1;
    default:
      return 0;
  }
}

static int sanitize_client_joy(uint8_t raw, uint8_t *out) {
  if ((raw & 0xE0) != 0) {
    return 0;
  }
  if (!is_valid_stick_nibble((uint8_t)(raw & 0x0F))) {
    return 0;
  }
  *out = (uint8_t)(raw & 0x1F);
  return 1;
}

enum transport_rx_result transport_rx_push_byte(struct transport_rx_state *state,
                                                uint8_t slot,
                                                uint8_t byte,
                                                uint8_t *out_pkt,
                                                size_t out_pkt_cap,
                                                size_t *out_len) {
  if (out_len) {
    *out_len = 0;
  }
  if (state->need == 0) {
    if (byte == PKT_DELTA || byte == PKT_BRICK_DELTA) {
      state->need = 4;
    } else if (byte == PKT_RESPAWN) {
      state->need = 6;
    } else {
      return TRANSPORT_RX_NONE;
    }
    state->idx = 0;
  }

  if (state->idx < sizeof(state->buf)) {
    state->buf[state->idx] = byte;
  }
  state->idx++;

  if (state->need == 4 && state->idx == 4 && state->buf[0] == PKT_DELTA &&
      state->buf[1] == PKT_DELTA && state->buf[3] < MAX_PLAYERS) {
    state->need = 5;
    return TRANSPORT_RX_NONE;
  }

  if (state->need != 0 && state->idx >= state->need) {
    if (state->need == 4 && state->buf[0] == PKT_DELTA &&
        state->buf[2] != slot && state->buf[1] != slot) {
      memmove(&state->buf[0], &state->buf[1], state->idx - 1);
      state->idx--;
      state->need = 4;
      state->resync_count++;
      return TRANSPORT_RX_NONE;
    }

    if (out_pkt && out_len && state->need <= out_pkt_cap) {
      memcpy(out_pkt, state->buf, state->need);
      *out_len = state->need;
    }
    state->need = 0;
    state->idx = 0;
    return TRANSPORT_RX_PACKET;
  }

  return TRANSPORT_RX_NONE;
}

uint32_t transport_rx_take_resync_count(struct transport_rx_state *state) {
  uint32_t count = 0;

  if (!state) {
    return 0;
  }

  count = state->resync_count;
  state->resync_count = 0;
  return count;
}

int transport_decode_delta_for_slot(const uint8_t *pkt, size_t len,
                                    uint8_t slot,
                                    struct transport_delta_packet *out) {
  uint8_t joy = 0;

  if (!pkt || !out || pkt[0] != PKT_DELTA) {
    return 0;
  }

  if (len == 4 && pkt[2] == slot && sanitize_client_joy(pkt[3], &joy)) {
    out->seq = pkt[1];
    out->pid = pkt[2];
    out->joy = joy;
    out->format = TRANSPORT_DELTA_PRIMARY;
    return 1;
  }

  if (len == 4 && pkt[1] == slot && sanitize_client_joy(pkt[3], &joy)) {
    out->seq = pkt[2];
    out->pid = pkt[1];
    out->joy = joy;
    out->format = TRANSPORT_DELTA_SWAPPED;
    return 1;
  }

  if (len == 5 && pkt[0] == PKT_DELTA && pkt[1] == PKT_DELTA &&
      pkt[3] == slot && sanitize_client_joy(pkt[4], &joy)) {
    out->seq = pkt[2];
    out->pid = pkt[3];
    out->joy = joy;
    out->format = TRANSPORT_DELTA_EXTRA_41;
    return 1;
  }

  return 0;
}
