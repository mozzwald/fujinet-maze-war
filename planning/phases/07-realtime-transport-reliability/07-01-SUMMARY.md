# 07-01 — TCP server implementation

Implemented 2026-09-09 on `realm-net`.

The prior handoff contained incomplete slot helpers and did not link. The
server now uses one nonblocking TCP listener and up to four accepted sockets,
with socket identity, TCP_NODELAY, bounded output queues, partial-write retry,
SIGPIPE protection, and immediate EOF/error handoff. Zombie allocation, world
simulation, input application, tick cadence and packet payloads are preserved.
A connection that never completes an inbound packet expires after 3 seconds;
established silent peers retain the 15-second timeout. SO_REUSEADDR permits
restart, while a second live listener still fails to bind.

Plan correction: client-to-server ingress already had a persistent type/length
parser, not a COBS decoder. It remains unchanged. Server-to-client COBS and the
additive checksum also remain unchanged. `raw_datagrams` remains the historical
log field name but now counts nonempty TCP reads rather than datagrams.

Validation:

- `make all` builds server, terminal client, SDL client and Atari artifacts
  with no compiler warnings.
- `make test` passes the entire suite, including all original gameplay checks
  migrated to TCP and the two new TCP smoke scripts.
- `tcp_stream_smoke.sh`: strict COBS/checksum decoding, byte-at-a-time and
  combined frames, malformed/oversize recovery, short writes, EINTR, EAGAIN,
  bounded queue overflow and fatal send handling.
- `tcp_transport_smoke.sh`: real-server fragmented and combined DELTAs,
  prompt input acknowledgment, FIN/RST slot reuse and zombie backfill,
  fifth-peer rejection, incomplete-handshake timeout, duplicate bind and
  immediate server restart.

Test adaptations preserve gameplay assertions. TCP tests use `sendall` and a
persistent delimiter buffer. Long combat scenarios keep their idle companion
seats alive with neutral inputs, because an expired TCP socket cannot silently
be reused as a UDP endpoint could. The 15-second timeout is still tested by
`slot_lifecycle_smoke.sh`. The old source guard forbidding UDP SO_REUSEADDR was
replaced with live TCP duplicate-listener/restart coverage.
