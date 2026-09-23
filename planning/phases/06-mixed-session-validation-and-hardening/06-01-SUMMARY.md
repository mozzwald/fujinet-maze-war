# 06-01 — TCP baseline validation summary

Status: complete. The user accepted the real Atari XL/FujiNet and Linux/SDL
mixed-session test on 2026-09-11.

## Baseline

- Branch: `a8-net-fix`
- Executable baseline commit: `f1db8b1 Add no-DLI HUD color markers`
- Planning documents were already dirty before validation and remain uncommitted.
- Forced build: `make clean && make all`
- Build result: Atari XEX/NetStream XEX, TCP server, terminal client, and SDL
  client all built successfully. Atari source assembled in three passes,
  producing a 13,007-byte object file.

## Automated evidence

Two consecutive host-network runs of `make test` passed every one of the 32
smokes. This includes transport framing and checksum recovery, TCP
fragmentation/coalescing/short writes, server restart and handoff, authoritative
combat, input queue/stale-input behavior, prediction/reconciliation, rendering,
seat/Zombie lifecycle, and reliable NAME/BRICK_DELTA/RESPAWN delivery.

The initial sandboxed run could not create sockets (`PermissionError` at socket
creation), so it was not treated as a game failure. The two complete runs were
repeated with local host-network permission and exercised real loopback TCP.

`tests/rig/tcp_movement_probe.py` also completed at both current input cadences:

| Simulated cadence | Frames per send | Inputs | Snapshots | Applied inputs | Server application interval |
|---|---:|---:|---:|---:|---|
| 60 Hz | 6 | 98 | 99 | 97 | 96 intervals at 100 ms |
| 50 Hz | 5 | 99 | 100 | 99 | steady 100 ms cadence |

These loopback probes validate command pacing and acknowledgement flow. They do
not replace display-latency or real serial-link testing, and do not reopen the
deferred cloud/WAN remote-sample-playback work.

## Managed Atari/FujiNet-PC attempt

Used the MCP-managed FujiNet-PC v1.6.2-dev+git-ed2d256bf sidecar on NetSIO port
19997 with a headless XL session, `build/maze-war-net.xex`, and a local TCP
server on port 9000. NetSIO itself initialized and exchanged 30 receive / 33
transmit datagrams with no send errors. Artifact:

`/tmp/atari800-mcp/a8-20260911124507-5aadcf-z9OKfG/artifacts/screenshot_1789130745.png`

The game did not enter normal play. `netsio_status` showed `netstream.active:
false`, no requested stream flags, and two SIO sync timeouts. This reproduces
the documented limitation of loading the concatenated NetStream XEX with the
Atari800 BIN loader: the handler is not resident. The repository does not
currently provide a bootable ATR/container to mount through FujiNet, so this
attempt is inconclusive for Atari gameplay rather than a baseline regression.
Both MCP-owned processes were stopped cleanly.

## User checkpoint

The user tested one real Atari XL/FujiNet client and one Linux/SDL client with
two Zombies and reported the mixed session passed. This accepts VALD-01 through
VALD-03 and freezes the TCP baseline for Phase 8.

After your acceptance, next is **08-01 — `gpt-5.6-terra`, high reasoning** for
shared-code, fixed-address Atari memory reclamation. Pause for the user’s model
switch before beginning it.
