# Lynx pause measurements, server burst reduction and remote smoothing

2026-10-12, after hardware-confirmed checkpoint `067c92e`.
Items 1–3 are implemented and tested together locally. Live hardware/WAN
acceptance remains pending. No firmware or emulator source was changed/built.

## 1. Measure pauses before changing bus timing

The normal client retains the two-VBL RX quiet guard and the four-input maximum.
New diagnostics expose `U`, distinct VBLs sampled waiting for serial quiet,
and `S`, distinct VBLs sampled with a due held repeat blocked by its window.
Repeated service calls in one VBL do not inflate `U`. Existing `W/D/Q/C/I`
report window, applied-ACK age, pending inputs, corrections and retries.
`U` includes idle receive traffic; neither counter is an electrical collision
measurement or a duration. Holding B changes rendering cost, so its `F` is
not the gameplay frame interval. The harness decodes the ROM's actual pixels.

An 1.8-second corridor hold with 300 ms synthetic input application delay:

| ROM/run | Moves sent/applied | Largest repeat gap | Sampled travel after release | Neutral received after release |
| --- | --- | --- | --- | --- |
| Hardware-confirmed baseline | 15/15 | 181.6 ms | 1.5 cells | 65.1 ms |
| Buffered client, two-VBL quiet | 14/14 | 154.7 ms | 1.25 cells | 80.8 ms |
| Isolated one-VBL quiet experiment | 15/15 | 196.0 ms | 1.375 cells | 1.0 ms |

These are single runs with screenshot/host scheduling variation, not a
controlled proof of a speedup. Two-VBL diagnostics recorded seven window-stall
samples, zero corrections/retries and filtered ACK age 23 ticks (about 383 ms).
The one-VBL experiment had 17 stall samples and the same ACK age. Its smaller
quiet count did not consistently improve repeat gaps, and Handy cannot test
physical bus contention. **Do not deploy the one-VBL experiment.** The normal
ROM retains two VBLs and its existing local stopping/prediction bounds.

Raw samples/hashes and experiment details are in
[pause-burst-smoothing.json](results/pause-burst-smoothing.json). The early
buffered runs precede the final shot-direction and observation-counter wrap
guards; their exact ROM hashes are recorded separately.

## 2. Server traffic changes, unchanged wire format

- A terminating shot sends one immediate clear and two on subsequent ticks.
  Previously a second clear was sent in the termination tick. Future repair
  opportunities remain at the same ticks. A slot handoff, with no immediate
  clear, still sends all three deferred clears. One 11-byte wire frame per
  ordinary termination is removed.
- Periodic unchanged NAME announcements remain once per second, rotating all
  seats, but are direct only. Actual name submissions/renames stay direct plus
  reliable. This avoids a 19-byte reliable frame and an 8-byte ACK per periodic
  announcement per peer, without changing the direct repair interval.
- Brick and respawn repair queues share one packet per tick, alternating
  classes when both are pending. Both retain their repeat counts. Repair-only
  delivery can be later under simultaneous load; initial direct/reliable
  state events and full-map repair remain intact. Repairs still precede the
  gameplay step, avoiding a same-tick initial event plus its echo.

Production-function/socket tests verify per-tick packet counts, future clear
repair, handoff behavior, reliable rename and the shared repair budget. Legacy
name, respawn echo, input queue, recovery, combat/world authority, round and TCP
smoke tests pass. The respawn test's source-order inspection was expanded to
cover the new budget-selection block; its behavioral assertions are retained.

A private actual-server traffic probe over 16 seconds of directional fire
recorded 160 snapshots and 16 direct names in each room, **zero periodic
reliable names**, 6,452 downstream bytes without zombies and 7,091 with two
zombies. Largest TCP reads, including bootstrap, were 101/125 bytes. Zombie
outcomes vary, so these totals are not a comparable before/after benchmark.
TCP read sizes are not physical serial burst measurements.

## 3. Buffered remote players and observed shot travel

Remote actors retain four real timestamped observations, including stationary
updates. Rendering samples 100 ms behind receipt time rather than repeatedly
retargeting an unfinished animation. Only short clear cardinal gaps interpolate;
diagonal/large gaps hold the previous observation until the new sample's time.
No extrapolation is performed. Updated walls invalidate unsafe old footprints.

Shots interpolate observed forward segments with 50 ms display delay. Duplicate
positions cannot shift the deadline; clear/death/respawn/round messages act
immediately. Direction changes, backward jumps or large gaps do not create a
cross-maze animation. The legacy protocol lacks shot-generation identifiers,
so some ambiguity after a lost clear remains; no hit or death is predicted.
Local collision, prediction, taps/turns and stopping limits are preserved.

Host tests cover intermediate positions, observation/counter/clock wrap,
wall-safe paths, immediate model authority, immediate death/clear, both video
pages, duplicate shots and restart behavior. Handy's irregular remote-update
workload sampled eleven distinct positions, monotonically reaching the correct
final cell. Sample gaps remain possible when data is late; a 100 ms buffer
cannot conceal every WAN or half-duplex pause.

## Memory and combined validation

Existing maze/player sprites are preencoded from unchanged source artwork;
host pixel-oracle/cache/round/death tests still pass. A compact immutable
`game_view` replaces copying unused networking/name/input-queue fields on every
render. A compile-time prefix-size check protects that copy. These changes
retain the full 2 KB stack, the hardware-tested TGI padding/alignment and the
post-link placement checker. BSS ends at `$B51D` exclusive, leaving **795 bytes**
before the stack at `$B838`.

Final normal ROM: **41,972 bytes**, SHA-256
`98a6f8c13c0b0bd407766d1f63065b46d9a96579505b9d160b0e21f4061dbb5b`.
It retains live HTTPS Lobby and `fujinet.online:5025`.

The final ROM and updated actual server ran together in unmodified Handy with
two zombies and a private command/raw forwarding bridge. The session received
132 snapshots, sent 38 inputs, handled projectiles, deaths/respawns and reliable
ACKs, and returned from diagnostics to the maze. Diagnostics ended with zero
window stalls, corrections or retries in this local run. This does not establish
WAN or physical half-duplex latency. Artifacts: `/tmp/maze-handy-server-ysjzmdwf`.
The diagnostic screenshot also shows one recovered COBS framing error (`B=1`,
`F=3`), with zero CRC, serial-error and overflow counters. Its origin was not
isolated; the bridge does not reproduce FujiNet's physical turnaround. A passing
session must not be reported as proof of error-free serial delivery.
Additional controlled tests passed burst-delayed taps/turns and noisy-ACK startup.

## Reproduce and deploy

```sh
make build/maze-war-server
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
bash tests/server_burst_smoke.sh
python3 tests/server_traffic_probe.py
python3 clients/lynx/tests/handy_server_smoke.py
python3 clients/lynx/tests/handy_render_smoke.py --remote-jitter-checks
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300 --snapshot-burst 3
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300 --hold-ms 1800
```

The new server binary is `build/maze-war-server`. **It has not been deployed.**
The ROM works with the current live server, but traffic reductions require
deploying the updated server. Atari 8 clients need no format/client update.
For one combined hardware test, deploy that server and copy the normal live
ROM; check boot/lobby, short taps, corridor release/corners, remote Atari motion
and projectile motion in the two-zombie room. If pauses persist, a B-screen
photo supplies `U/S/W/D/Q/C/I` plus serial error fields for the next diagnosis.
