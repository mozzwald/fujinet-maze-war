#include "transport_stats.h"

#include <inttypes.h>

void transport_stats_note_raw_bytes(struct transport_counters *counters,
                                    size_t raw_bytes) {
  if (!counters) {
    return;
  }
  counters->raw_datagrams++;
  counters->raw_bytes += (uint64_t)raw_bytes;
}

void transport_stats_note_delta(struct transport_counters *counters,
                                enum transport_delta_format format) {
  if (!counters) {
    return;
  }

  switch (format) {
    case TRANSPORT_DELTA_PRIMARY:
      counters->delta_primary++;
      break;
    case TRANSPORT_DELTA_SWAPPED:
      counters->delta_swapped++;
      break;
    case TRANSPORT_DELTA_EXTRA_41:
      counters->delta_extra_41++;
      break;
    default:
      break;
  }
}

void transport_stats_log_summary(FILE *stream, int slot,
                                 const struct transport_counters *counters) {
  if (!stream || !counters) {
    return;
  }

  fprintf(stream,
          "transport summary slot=%d raw_datagrams=%" PRIu64
          " raw_bytes=%" PRIu64
          " delta_primary=%" PRIu64
          " delta_swapped=%" PRIu64
          " delta_extra_41=%" PRIu64
          " delta_resync=%" PRIu64
          " drop_bad_joy=%" PRIu64
          " drop_stale_seq=%" PRIu64
          " accepted_delta=%" PRIu64 "\n",
          slot, counters->raw_datagrams, counters->raw_bytes,
          counters->delta_primary, counters->delta_swapped,
          counters->delta_extra_41, counters->delta_resync,
          counters->drop_bad_joy, counters->drop_stale_seq,
          counters->accepted_delta);
}
