#ifndef TRANSPORT_STATS_H
#define TRANSPORT_STATS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "transport_normalize.h"

struct transport_counters {
  uint64_t raw_datagrams;
  uint64_t raw_bytes;
  uint64_t delta_primary;
  uint64_t delta_swapped;
  uint64_t delta_extra_41;
  uint64_t delta_resync;
  uint64_t drop_bad_joy;
  uint64_t drop_stale_seq;
  uint64_t accepted_delta;
};

void transport_stats_note_raw_bytes(struct transport_counters *counters,
                                    size_t raw_bytes);
void transport_stats_note_delta(struct transport_counters *counters,
                                enum transport_delta_format format);
void transport_stats_log_summary(FILE *stream, int slot,
                                 const struct transport_counters *counters);

#endif
