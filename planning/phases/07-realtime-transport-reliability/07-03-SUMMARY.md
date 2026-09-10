# 07-03 — CRC-16 framing

Implemented 2026-09-09 on `realm-net`.

All realtime traffic now uses COBS-delimited CRC-16/CCITT-FALSE frames. The CRC
uses polynomial `0x1021`, initial value `0xffff`, and a low-byte/high-byte
trailer. This is intentionally bidirectional: 07-01/07-02 left client input
as a raw TCP byte stream, which could not meet RTP-03 or recover a lost byte
at a known frame boundary.

`server/main.c` COBS-decodes and CRC-verifies every accepted connection before
the existing DELTA compatibility decoder sees the payload. Its output still
uses the established COBS framing with the wider CRC trailer. The shared Linux
stream helper encodes outgoing frames and validates incoming CRCs. The Atari
uses a bitwise 6502 CRC routine in both directions; the largest 51-byte
BRICK_FULL costs 408 shift iterations, and the framing state and staging
buffers remain below the checked memory-layout limits.

Validation:

- Forced rebuild: `make -B all`; source timestamps could otherwise have caused
  ordinary `make` to retain a pre-CRC binary.
- `make -B test` passes the full 30-script suite after its one stale debug-log
  assertion was replaced by the test's existing live acknowledgement checks.
- `cobs_resync_smoke.sh` proves byte deletion, insertion and flipping still
  cost one frame, and demonstrates CRC-16 rejecting a byte swap that preserves
  the former additive sum.
- `packet_checksum_smoke.sh`, `tcp_stream_smoke.sh`, and
  `tcp_transport_smoke.sh` validate wire CRCs, split/coalesced streams,
  malformed-frame recovery, short writes, EOF/RST, full seats, and restart.
- MCP-managed Atari800 XL + FujiNet-PC v1.6.2-dev+git-ed2d256bf connected to
  the rebuilt server at `127.0.0.2:9000`, reached `NET_GAME_SHOW=1`, sent
  CRC-framed DELTAs, and received CRC-framed snapshots. Server logs show
  accepted DELTAs; Atari `NET_NS_ERRS` and `NET_CK_BAD` were both zero. The
  managed artifact screenshot is `screenshot_1788984642.png`.

The user independently confirmed real hardware works against this emulation
path. RTP-03 is therefore complete. 07-04 remains the next transport plan.
