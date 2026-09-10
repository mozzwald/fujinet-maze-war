# 07-04 Summary - Ordered Reliable Event Stream

Completed on `realm-net` on 2026-09-09.

## What changed

- Added `0x53 RELIABLE_EVENT` server-to-client frames carrying a per-client
  16-bit stream revision and an inner event payload.
- Added `0x45 RELIABLE_ACK` client-to-server frames carrying the highest
  applied reliable revision.
- Routed `NAME`, `BRICK_DELTA`, and `RESPAWN` payloads into one ordered
  reliable stream per TCP client.
- Added timeout retransmit and duplicate-ACK fast retransmit. Retransmits send
  the stored wrapper bytes unchanged and use stop-and-wait delivery so the
  Atari/FujiNet path is not flooded by a full-window retry burst.
- Updated Atari, terminal Linux, and SDL Linux clients to apply reliable events
  only in revision order and ACK the highest applied revision. The Atari client
  stores the applied revision from the reliable wrapper bytes after the inner
  event succeeds, avoiding scratch-byte clobber from inner handlers.
- Kept direct packets, brick/respawn echoes, name rotation, and `BRICK_FULL`
  resyncs as transition fallbacks while the new stream is measured.

## Validation

- `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl build/maze-war-net.xex`
- `bash tests/reliable_event_stream_smoke.sh && bash tests/tcp_transport_smoke.sh && bash tests/combat_client_parity_smoke.sh`
- `make test`
- MCP Atari800 + FujiNet-PC emulator against `build/maze-war-net.xex`, connected
  to `build/maze-war-server --bind 127.0.0.2 --port 9000 --zombies 2 --debug`.

The reliable smoke confirms a duplicate ACK for revision 0 triggers a prompt,
byte-identical retransmit of revision 1 before the normal timeout path. The
full smoke suite passed. Emulator validation reached the game screen with
`NET_GAME_SHOW=1`, `NET_NS_ERRS=0`, `NET_CK_BAD=0`, and reliable applied
revision advancing from 26 to 58 with no ACK backlog; server debug showed ACKs
advancing past revisions 1, 2, 33, 34, and 67 during the run.

## Regression review - 2026-09-10

After the first real-hardware/emulator comparison found forgotten symptoms, a
follow-up review found three regression risks from the phase 7 implementation:

- `send_checked()` returned the transport-framed byte count after appending CRC
  instead of the logical payload length expected by callers. That made successful
  sends look like size mismatches to debug/diagnostic code. The function now
  preserves and returns the original payload length on success.
- Linux clients advanced their reliable applied revision and ACKed before
  proving the wrapped inner payload was supported and well-formed. Terminal and
  SDL clients now validate reliable inner packet type and exact length before
  advancing the cumulative ACK.
- The Atari client called the direct inner apply routines from `NET_REL_INNER`
  and then always treated them as success. It now validates reliable NAME,
  BRICK_DELTA, and RESPAWN wrapper lengths and basic payload bounds before
  applying and ACKing them.

Validation rerun after those fixes:

- `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl build/maze-war-net.xex`
- `bash tests/reliable_event_stream_smoke.sh && bash tests/tcp_transport_smoke.sh && bash tests/combat_client_parity_smoke.sh`
- `make test`
- MCP Atari800 + FujiNet-PC emulator against `build/maze-war-net.xex` and
  `build/maze-war-server --bind 127.0.0.2 --port 9000 --zombies 2 --debug`;
  the client reached the game screen with `NET_GAME_SHOW=1`, `NET_NS_ERRS=0`,
  `NET_CK_BAD=0`, no ACK backlog, and reliable ACKs advancing in the server
  debug log.

## Notes

07-05 remains deferred and not recommended. Do not start it without an explicit
separate decision because it changes the authority model rather than transport
reliability.
