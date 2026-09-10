# 04 Summary - Render-State Separation

Initial implementation on `realm-net` on 2026-09-10; reopened after failed
user smoothness testing. Further code work is required before approval.

## Acceptance update — 2026-09-10

The user reports persistent remote lag and jumpiness on hardware/emulation and
emulation/emulation. Smoothness acceptance failed; this work is reopened.
See [04-LAG-REVIEW.md](04-LAG-REVIEW.md) for confirmed animation/recovery defects,
real-server TCP timing results, and the staged repair plan. Earlier implementation
and successful smoke checks below do not establish smoothness or timing correctness.

## Repair pass - 2026-09-10

- Fixed `REMOTE_FOLLOW` choosing left when the authoritative target was to the
  right of the rendered actor.
- Preserved the remote recovery distance across look-ahead collision checks, so
  two-cell trailing recovery increments the desync counter instead of being
  misread as a successful one-cell recovery.
- Added source diagnostics for remote follower recovery failure, two-cell
  trailing recovery, and hard snaps.
- Raised actor animation capacity to one visual phase per VBI, and changed NTSC
  client DELTA pacing to six frames so clean 10 Hz server ticks no longer see
  routine empty input ticks.
- Restored the reachable local pending-input replay loop and kept its empty-ring
  guard.
- Changed server tick scheduling to fixed deadlines with bounded overrun
  recovery instead of `next_tick = now + tick_ms`.
- Added `tests/remote_follow_lag_smoke.sh` and updated the cadence/probe docs.

Validation: `make test` passed with local TCP socket permission. User testing on
2026-09-10 found emulator-to-emulator play across two computers almost flawless.
Real Atari XL with hardware FujiNet was greatly improved, with occasional
one-to-two-cell remote jumps remaining.

## What changed

- Locked local movement starts to the transmit cadence with `NET_MOVE_DUE`, so
  one transmitted DELTA licenses at most one predicted cell.
- Split simulation cell state (`LOCX/LOCY`) from render-facing cell state
  (`RNDX/RNDY`). Gameplay decisions, collision, occupancy, and shot origins stay
  on simulation truth; draw/erase paths use render state.
- Replaced local correction walking with render chase: authority updates
  simulation immediately, and only the picture walks into place.
- Moved remote actor and zombie smoothing onto render state. Staged snapshots
  update remote `LOCX/LOCY` immediately, while `REMOTE_FOLLOW` interpolates
  `RNDX/RNDY` toward that authoritative cell without feeding the smoothed value
  back into gameplay.
- Treated respawn/join as a real discontinuity: final respawn snaps simulation
  and render state to the spawn cell instead of sliding across the maze.
- Cleared player-missile memory before enabling PM output, and stopped enabling
  unused missile DMA.
- Completed embedded-font coverage for user-entered names and status text,
  including the previously broken `F`, `H`, `J`, `Q`, `V`, `X`, `-`, and `.`
  reachable cases.

## Validation

- `make build/maze-war-net.xex`
- `bash tests/input_send_cadence_smoke.sh`
- `bash tests/render_state_separation_smoke.sh`
- `bash tests/pm_init_smoke.sh`
- `bash tests/font_coverage_smoke.sh`
- `bash tests/respawn_echo_smoke.sh && bash tests/death_render_smoke.sh`
- `make test`
- MCP Atari800 + FujiNet-PC boot sanity against an existing local server on
  port 9000: reached gameplay with clean netstream and CRC counters.

## Open checkpoint

Treat this as a good Phase 4 checkpoint, not a full closeout. The remaining
real-hardware jumps are much smaller and less frequent, but bounded timed
remote-sample playback remains the next option if Phase 6 validation needs the
last bit of smoothness.
