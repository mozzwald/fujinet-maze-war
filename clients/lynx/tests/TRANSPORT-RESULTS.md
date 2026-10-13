# Lynx transport processing and input response

2026-10-12. Recovery work committed as `fb91115`; user deployed that server
and confirmed Atari 8 operation. User reports remaining Lynx lag, especially
on real hardware, with authoritative position corrections and slightly
slower-feeling movement. This iteration changes only the Lynx client/tools.

## Changes

- Queue a complete outgoing COBS frame without receive parsing between bytes,
  following FujiRealm's transmit approach. Receive parsing used to interleave
  potentially several milliseconds of CRC work with one short TX frame.
- Defer gameplay TX while an incoming frame is incomplete, rechecking after
  each transmitted packet. A partial frame idle for two VBL ticks is discarded
  to prevent a truncated frame from blocking recovery indefinitely. CRC and
  framing failures remain visible in diagnostics; timeout is error 6.
- Replace bitwise CRC with two 256-byte lookup tables and an eight-bit inner
  loop. Wire protocol and checksum results are identical.
- Fresh taps and turns animate over 50 ms instead of 100 ms. Held repeats
  still use 100 ms cells, 250 ms initial repeat delay, and the same bounded
  window. No extra movement commands or cells are predicted.
- Bare fire-button presses are silent and no longer snap an unfinished walk.
  Directional fire retains muzzle alignment; shot audio starts when a server
  SHOT creates a projectile. Duplicate active-shot packets remain silent.
- Diagnostics add `C` for local corrections after pending-input replay and
  `I` for input retries. This permits live comparison of correction frequency,
  applied-ACK age (`D`), frame interval (`F`) and framing errors.
- Encode existing smoke artwork ahead of time, preserving Suzy zero guards.
  This removes runtime encoding, its string data and a redundant sprite
  buffer, allowing the CRC tables while retaining the 2 KB software stack.

## CRC CPU benchmark

A separate test cartridge runs the exact previous bitwise function and the
production lookup function on identical data, 128 times per packet size. The
existing Handy binary emulates the 65C02 and Mikey microsecond timers. Function
call, loop and volatile-result overhead are included in both measurements.

| CRC payload | Previous (ms/call) | Lookup (ms/call) | Speedup |
| --- | ---: | ---: | ---: |
| DELTA, 5 bytes | 2.015 | 0.648 | 3.11× |
| SNAPSHOT, 21 bytes | 8.282 | 2.411 | 3.43× |
| Largest reliable event, 56 bytes | 21.863 | 6.268 | 3.49× |

This measures checksum CPU work, not full packet processing or WAN latency.
The snapshot CRC saving is about 5.9 ms per snapshot; a large reliable frame
saves about 15.6 ms. Physical serial turnaround remains unmeasured here.

## Production ROM comparison in Handy

The same empty-corridor workload uses 10 Hz applied-input snapshots and
300 ms of added server input application delay. This is one run per ROM;
capture overhead and host scheduling affect wall-clock gaps.

| Measurement | Committed ROM | New ROM |
| --- | ---: | ---: |
| Right hold | 1208 ms | 1202 ms |
| Sent/applied movement steps | 10/10 | 10/10 |
| Largest repeat-send gap | 177.7 ms | 160.1 ms |
| Distinct sampled robe positions | 15 | 13 |

The unchanged applied-step count supports preserving held movement speed in
this workload. The single-run gap improvement is not proof of a live latency
improvement. The peer does not simulate electrical half-duplex arbitration.
Screenshot samples are affected by host timing and cannot establish whether
visual motion is smoother; the deterministic motion tests check its pacing.

Measurements and hashes:
[transport-processing.json](results/transport-processing.json).

## Verification and reproduction

The full host suite passed. The CRC test compares against the polynomial loop
for every supported length and all 256 first-byte values. Serial tests cover
contiguous enqueue, full-buffer retry/failure, RX completion, partial timeout,
and a new partial frame arriving between ACK and input TX.
The receive timeout uses a fresh VBL clock, with a test where it advances
beyond the input sampler's timestamp; unsigned clock age must not expire a
newly received partial frame immediately. Motion tests cover
50 ms taps/turns, 100 ms held steps, wall-safe corners, ACK replay, correction,
bare-fire behavior, muzzle alignment, render-count independence and clock wrap.
Generated smoke bytes are checked against the original art on every host run.
The final ROM passed Handy's 300 ms delayed tap/turn checks with SDL audio
capture: all three short taps moved one cell, the corner reached its expected
cell, bare A and idle intervals had zero amplitude, and actual local/remote
shots, movement, impact, death and spawn intervals produced audio.

```sh
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
make -C clients/lynx/bench crc
python3 clients/lynx/bench/run.py --crc-only --traffic-hz 0 --rom clients/lynx/build/crc-bench.lnx
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --sound-checks --input-lag-ms 300
```

Use `maze-war-lynx.lnx` for live testing via Lobby/default
`fujinet.online:5025`. `crc-bench.lnx` is only a private-peer test cartridge.

## Remaining live validation

Real hardware must confirm the benefit of contiguous TX and RX boundary
deferral. Authoritative corrections remain necessary when prediction disagrees
with collision, remote occupancy or missing input; this change does not hide
them or establish that transport errors caused every reported correction.
Compare the new diagnostics during a quiet corridor and the two-zombie room.
Further traffic work can reduce redundant combat and metadata bursts while
preserving existing-client repair behavior. No firmware modification or new
server deployment is required for this client iteration.

## Release drift investigation

The user subsequently reported several cells of continued local movement
after releasing direction in a cleared corridor. Extended the Handy latency
test to record the authoritative cell and visible robe position through
release, time to first neutral input, and packets received after key-up.
The production ROM is unchanged for these measurements.

Two isolated 1.42-second right holds produced:

| Added application delay | Server cell at release → stop | Visible travel after release | First neutral received |
| --- | --- | --- | --- |
| 300 ms | 9 → 13 | 2 cells | 61 ms |
| 900 ms | 3 → 7 | 1 cell | 54.5 ms |

The first run reached its final visible position at the 155.7 ms screenshot;
the second reached it at the 207.5 ms screenshot. These are sampled wall-clock
observations, not precise input-to-display latencies. The last held screenshot
can predate actual key-up slightly; robe facing/pixel offsets are unchanged
during these straight runs. In both cases the local sprite was **ahead** of
the authoritative server cell at release. No directional packet arrived
after the first neutral. One directional packet arrived just after key-up in
the 300 ms run; this can include a command already in flight.

Thus pending server moves explain remote authoritative movement after release,
but do not by themselves explain a local sprite walking most of a corridor:
the client already predicts those moves. Local interpolation plus sampling,
transmission and presentation delay can still give one or two visible cells
of overrun in this test. The long reported drift has not been reproduced;
these tests do not model hardware half-duplex arbitration or live combat load.
Do not infer a confirmed hardware cause or a release fix from these results.

The harness now checks that directional transmission does not resume after
neutral and accounts for packets already in flight when comparing steps.
Raw screenshot-position samples and ROM hashes are retained in
[release-motion.json](results/release-motion.json).

```sh
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300 --hold-ms 1400
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 900 --hold-ms 1400
```

### Hardware comparison and diagnostic cartridge

The user confirmed at most two cells of overrun in Handy, but five to six
cells on real hardware. This establishes a hardware-dependent difference;
it does not yet distinguish delayed input sampling, presentation backlog,
serial loss or authoritative corrections.

`maze-war-lynx-stop-test.lnx` is a separate diagnostic build. It places only
the local sprite immediately at its existing predicted cell on each drawn
frame. Remote animation, movement generation, pending-input replay and the
serial path are unchanged. This deliberately makes local movement discrete;
it is an isolation experiment, not the proposed final movement style or a
confirmed fix. The normal `maze-war-lynx.lnx` retains interpolation.

Build both cartridges for the live Lobby/default room:

```sh
make -C clients/lynx all SERVER_HOST=fujinet.online SERVER_PORT=5025
make -C clients/lynx all SERVER_HOST=fujinet.online SERVER_PORT=5025 LOCAL_CELL_TEST=1 TARGET=build/maze-war-lynx-stop-test.lnx MAP=build/maze-war-lynx-stop-test.map
```

On hardware, repeat the cleared-corridor hold/release with the diagnostic
cartridge. Report the cells traveled after release and whether that travel
is continuous or includes pauses/jumps. If the overrun drops substantially,
presentation/interpolation is implicated. If it remains five to six cells,
bypassing interpolation alone is insufficient; input service, live prediction
updates and reconciliation require further measurement. This experiment does
not cancel already-sent server inputs or require server/firmware changes.

The diagnostic cartridge passed the Handy corridor workload with 300 ms
application delay: 12 movement sequences sent/applied, zero sampled visible
travel after release, first neutral received at 47.3 ms, no directional packet
after neutral. At release its sprite was already at cell 13 while the peer
was at cell 9; the peer subsequently reached 13. These are single-run results
and do not establish hardware behavior. SHA-256:
`7f7f6db8e29f81cc0ae6d2b3023f8911801310fd12c5f4cc5e1af95d8c4c477a`.
The normal cartridge remains byte-identical to the previously measured ROM
(`98dd4344289543ea587e754fce4490fab5df69993504b03d5b72cdf5d70aa86a`).

### Bounded animation after hardware diagnostic feedback

The user tested the diagnostic on hardware: excessive movement overrun
disappeared, but authoritative snapbacks became conspicuous. This implicates
presentation in the overrun; bypassing it is not a satisfactory final display.

The subsequent normal build replaces sequential route playback with one
bounded interpolation leg for each actor. If several retained cells are
behind the model, their penultimate recorded cell establishes the last legal
leg. Older legs cannot extend the animation deadline. Its original event
timestamp determines completion; taps/turns retain three-tick timing and
held movement retains six-tick timing. Corners may catch up to a recorded
boundary immediately rather than draw a diagonal shortcut. This also prevents
multi-cell remote snapshots from accumulating a long playback queue.

A correction within one clear cardinal cell eases over three ticks, while
the actual model changes immediately. Repeated snapshots of the same target
do not restart the deadline. Larger/diagonal corrections, teleport placements
and directional firing still rejoin immediately. The full sprite footprint
remains checked against walls and occupied cells. This change does not reduce
the number of underlying authoritative disagreements or hide large ones.

Full host checks passed, including updated motion tests for last-leg corner
catch-up, six retained cells settling within one leg, correction easing,
unchanged-target deadlines, render-rate independence and wraparound. Both
cached pages continue to match the settled CPU reference. Handy with 300 ms
application delay passed 60/120/160 ms single-cell taps and the narrow turn.
A 1.434-second corridor hold produced 11 sent/applied moves and half a cell
of sampled visible travel after release, with no directional packet after
neutral. This single-run result is not a hardware guarantee; its 91.1 ms
neutral reception time includes host scheduling and test capture overhead.
Runs were made in independent concurrent Handy sessions.

[bounded-motion.json](results/bounded-motion.json) retains screenshots' position
samples, movement outcomes and the normal live-server ROM hash:
`78e8c44083876f63a4a7b92e5f47d53ff684fc8985e18f2cf28efdb529e1fd9b`.
Test `build/maze-war-lynx.lnx` for the animated version. The separately built
stop-test cartridge remains the historical no-interpolation diagnostic.
No server deployment or firmware change is required. Animated hardware
release feel and correction severity still need user validation.

### Remote jitter and receive-burst handoff

User feedback: stopping is better, but local corrections remain jumpy, held
movement sometimes pauses, and an Atari player's movement also jumps on the
Lynx hardware. The four-step input window can pause when feedback is delayed;
the actual hardware ACK age and error counters are not yet available.

Client changes address separate code-level contributors:

- The one-cell retarget limit forced a remote sprite to jump to its previous
  destination when a new snapshot arrived midway through interpolation.
  Remote interpolation now retargets from its current drawn position across
  at most two clear cardinal cells. The middle footprint is checked to reject
  an intervening wall. The deterministic test delivers a new destination
  halfway through a cell and verifies no initial position jump.
- Local corrections across the same clear two-cell gap ease over a fixed
  three ticks. Local forward prediction retains its one-cell animation bound.
  Corners, larger gaps and blocked paths still rejoin immediately. This does
  not reduce the number of server corrections.
- TX now waits for two VBL boundaries after **any** received byte, including
  complete COBS frames. This guarantees at least one whole VBL of observed
  quiet, roughly 17–33 ms, before the driver disables RX for TX. A complete
  frame can be followed by more bytes in FujiNet's burst; the old partial-only
  guard did not protect that handoff. The condition is rechecked between
  outgoing packets. A separate seen-RX flag prevents diagnostic counter wrap
  from bypassing it. Tests cover complete frames followed by another frame
  on the next tick, partial frames, partial completion and truncated input.

Read-only review confirms the firmware disables Nagle for its TCP socket;
the server already sets TCP_NODELAY through `net/tcp_stream.h:tcp_configure`.
Neither server nor firmware is changed in this iteration. Firmware's physical
half-duplex handoff remains outside the Handy model; the quiet guard is a
client mitigation whose effect on hardware loss is unmeasured.

The final ROM passed the full host suite and three independent concurrent
Handy runs using immutable private cartridge copies:

| Workload | Result |
| --- | --- |
| Irregular remote destinations | Ten distinct sampled positions, monotonic motion, final cell reached; largest sampled gap 10 px |
| 300 ms input delay, three snapshots delivered together | 60/120/160 ms taps each one cell; narrow overlapping turn reaches (7,15) |
| 300 ms input delay, 1.415 s hold | 11 unique moves applied; 1.375 cells sampled travel after release; one directional packet received just after key-up, none after neutral |

The final corridor position was first sampled at 155.9 ms after release.
Neutral was received at 8.9 ms. Largest held repeat-send gap was 209.6 ms:
these checks do not establish that continuous-motion pauses are fixed.
An earlier pre-seen-RX-flag run had 0.375 cells of overrun; that is not the
final-cartridge measurement. Snapshot and capture scheduling vary between
runs, and screenshot gaps do not establish absence of visible jumps. Remote
retarget correctness is established by the deterministic halfway-update test;
live smoothness still needs hardware validation.

[receive-quiet-jitter.json](results/receive-quiet-jitter.json) retains samples,
actual update times and SHA-256
`d18322efe1d594112c9b68a61ba0e3e0db718b0dc797a53dc343a8428e61444a`.
The link map ends BSS at `$B820`, preserving the existing 2 KB stack and video
pages; further features require memory planning.

```sh
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
python3 clients/lynx/tests/handy_render_smoke.py --remote-jitter-checks
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300 --snapshot-burst 3
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300 --hold-ms 1400
```

Requested hardware diagnostic photo after a pause: `W/D/Q` describe the
input window and ACK age, `C/I` corrections/retries, and `B/E/O` frame/serial
errors. Holding diagnostics changes rendering work, so `F` there must not be
treated as the gameplay frame interval. Compare movement and errors with the
new live-server ROM before attributing pauses solely to the serial bus.
