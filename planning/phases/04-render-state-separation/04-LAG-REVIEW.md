# Persistent remote movement lag: review and path forward

Date: 2026-09-10. Reviewed `1909f56` plus the uncommitted Phase 4 changes.
Trigger: user reports persistent remote jumpiness on real Atari versus emulator
and emulator versus emulator on separate computers. Phase 4 smoothness acceptance
has failed; the previous boot and source-guard checks were insufficient.

## Conclusion

There are confirmed client defects and a reproducible timing mismatch even on
clean host TCP. Separating render coordinates from simulation coordinates was
necessary, but the current follower is not time-based snapshot interpolation.
It cannot sustain the incoming human movement rate, loses intermediate targets,
and has broken fallback logic. Hardware-specific trouble is not required to
explain these problems. We have not measured what fraction of the user's visible
stalls each problem causes.

This review changes diagnostics and documentation, not gameplay code. Existing
uncommitted Phase 4 code is retained for comparison. No commit is authorized
until the user accepts a subsequent gameplay test.

## Implementation update - 2026-09-10

The first repair pass has now changed gameplay code. It fixes the rightward
fallback branch, preserves the recovery distance across `NET_AHEAD_FREE_RND`,
counts the remote follower's bounded-recovery and hard-snap paths in the
diagnostic source byte, restores the reachable local replay loop, gives actor
animation one phase per VBI, sends NTSC DELTAs every six frames, and advances
server ticks from fixed scheduled deadlines with bounded overrun recovery.

The original findings below are retained as the evidence that motivated the
patch. They describe the pre-fix state. Bounded timed playback with snapshot
history is still not implemented; Phase 4 should remain open until the user
tests the new build and confirms whether the smaller timing/recovery repair is
enough or whether the protocol extension is still needed.

## User test update - 2026-09-10

The repair pass was tested with two different computers running emulation and
one real Atari XL using a hardware FujiNet. The two emulation machines were
almost flawless. The real hardware path was greatly improved: remote players
still occasionally jumped by one or two cells, but the result was significantly
better than the failed baseline that triggered this review.

Interpretation: the renderer/cadence/recovery repair addressed the dominant
clean-network lag symptom and is a good checkpoint. The remaining real-hardware
jumps are the residual case this review expected from latest-cell chasing,
FujiNet/SIO timing, or occasional stream stalls. If that residual jumpiness
needs further improvement before Phase 6, the next planned step is still the
bounded timed remote-sample playback/protocol extension described below, not
another snap-threshold tweak.

## Phase 08-06 hardware regression revisit - 2026-09-12

Later real-hardware testing again showed frequent remote snap-forward while an
unrelated body-rendering fault was also present. The earlier Phase 4 fixes were
still in the source: six-frame DELTA pacing, one animation phase per VBI, fixed
rightward fallback, preserved recovery distance, reachable local replay, and
fixed-deadline server ticks all passed their guards.

The remaining follower policy had become internally inconsistent after the
animation speedup. A legal gap of two cells incremented `NET_DESYNC_CNT`, so
three successful catch-up steps forced a snap. A gap of three or more was sent
to `RF_FAIL` and immediately snapped because `NET_RECOVER_P1` was also three.
That policy was originally meant to stop an equal-speed renderer from trailing
forever; the current renderer has about 15-cell/s capacity and can close a
10-cell/s authoritative backlog. Hardware batching can produce two or three
cells of gap without representing a discontinuity.

`REMOTE_FOLLOW` now walks every collision-valid gap below the existing
ten-cell catastrophic guard, and a successful step clears the failed-recovery
counter. Three genuinely blocked recovery attempts still snap, as does a gap
at or above the catastrophic guard. This changes the meaning of recovery from
failure counting to progress tracking rather than merely increasing a numeric
threshold.

This remains a latest-target follower. It cannot preserve an intermediate
corner discarded before a VBI. Keep bounded timed sample playback prominent
for a future cloud-server test if added latency and jitter make that lost
history visible again.

## Stationary actor body-loss follow-up - 2026-09-12

The follow-up physical test accepts the remote-follower repair above: the
reported movement lagginess is fixed. It also proves that the separate
shirt-only actor fault remains. A player can stop with only the player-missile
shirt visible after an ordinary stop, a respawn, stopping at a corner, or a
shot; starting to move redraws the complete character-cell body. No stable
reproduction sequence is known.

That observation separates this issue from remote-sample timing and from PM
sprite loss. It is an intermittent stationary playfield/body write or erase
problem. The prior `ERASMAN` Y/mask repair fixed a documented sliver fault, and
the Phase 08-06 VBI/foreground scratch separation removed a real data race, but
neither explains this remaining behavior. A prior `SETSTIL` trailing-cell
cleanup experiment also had no measurable effect and was reverted; do not
reintroduce it without new evidence.

Next investigation: add a low-overhead, real-hardware-safe trace or watchpoint
for the affected `GAMESCR` body cells and correlate it with `LOCX/LOCY`,
`RNDX/RNDY`, `MOVEST`, and direction. Attribute each change to `SETSTIL`,
`SETMOVE`, `ERASMAN`, `ERASHOT`, shot/brick application, or respawn redraw.
`ERASMAN`'s simulation-cell pointer versus render-cell ownership is a specific
hypothesis to measure during a stop transition, not an established cause. Do
not hide the fault by redrawing every stationary actor in the VBI: that would
obscure the writer and add unbudgeted VBI work.

### Deferred new-round spawn observation - 2026-09-13

After the stale-shot and brick-delta ownership repair in `bdf4bbc`, the user
played four mixed real Atari/FujiNet and emulator rounds without Zombies. Only
one shirt-only actor occurred, immediately after a new round began. The
evidence is `ref/screenshots/Screenshot from 2026-09-13
08-30-57_new-round-spawn.png`.

This confirms a remaining round-start/forced-redraw path outside the two newly
guarded playfield clears. It is intentionally deferred until a later render
pass: retain the screenshot and the exact scenario, but do not add a broad
redraw workaround without a repeatable capture of the writer.

## Confirmed client findings

### 1. Animation throughput is lower than input throughput

`SETALLP` initializes `MOVRATE` and `MOVCLOK` to 2. `CHKTIME` decrements the
clock once per VBI. `SETIME` either starts a move or advances its animation;
finishing a move does not start the next one in the same call.

`INITMOVE_STEP` immediately calls `MOVEIM`, but a cell still requires four
phases and another scheduled visit to start the next cell. With a target always
available, tracing those branches in every direction gives:

| Event | Display frames |
| --- | --- |
| Cell starts | 2, 10, 18, 26, 34 |
| Cell finishes | 8, 16, 24, 32, 40 |

Thus it takes **eight frames between cell starts**, not six. The comment saying
this is approximately 10 Hz confuses first-start-to-finish time with sustainable
throughput.

| Nominal video rate | DELTA interval (`NET_FRAME_DIV=7`) | Maximum render speed | Input-command rate |
| --- | --- | --- | --- |
| 60 Hz | 116.7 ms | 7.5 cells/s | 8.57 commands/s |
| 50 Hz | 140 ms | 6.25 cells/s | 7.14 commands/s |

These are source-derived scheduling bounds, not emulator measurements. A remote
human walking freely can outrun the picture even with perfect delivery. At
60 Hz the idealized deficit is about 1.07 cells per second before stops, turns,
or recovery intervene. A 10-command/s Linux sender is an even harder workload.

The local actor shares this animation limit. `NET_MOVE_DUE` is a single bit:
multiple grants during animation collapse. It guarantees **at most** one start
per grant, not one predicted move for every sent directional command. Sending
seven-frame commands while starting cells every eight frames violates the
stronger 1:1 assumption used by prediction comments and pending-input reasoning.

### 2. Rightward fallback takes the leftward branch

In `RF_SYNCCHK`, when the target X is greater than render X:

```asm
    LDA #0
    BNE RF_1SET
RF_1L
    LDA #2
```

`LDA #0` sets Z, so `BNE` is never taken. Direction 0 (right) becomes direction
2 (left). Example: render `(5,5)`, target `(7,5)`, open cells. The exact-one-cell
fast path fails, fallback chooses left, and the picture can move away from the
target until it reaches the snap threshold. A neutral snapshot with a one-cell
rightward gap also reaches the defective fallback.

This defect already exists in committed `1909f56`; the latest changes retained
it when moving the follower onto render coordinates.

### 3. The collision helper destroys the recovery distance

`RF_SYNCCHK` stores Manhattan distance in `NET_RX_TMP`, calls
`NET_AHEAD_FREE_RND`, and then compares `NET_RX_TMP` against 2. The helper uses
the same byte for its return result: 0 for free, 1 for blocked. Consequently a
successful two-cell recovery reads zero, resets the desync counter, and never
takes `RF_STEPFAR`. A blocked recovery reads 1 instead of the saved distance.
The original helper had the same scratch contract, so this also predates the
latest Phase 4 pass.

Preserve distance in dedicated scratch with an explicit clobber contract.
Test the behavior, not the presence of a store or label.

### 4. Latest-target chasing loses movement history

`NET_SNAP_APPLY` publishes one latest-wins staging slot; `NET_STAGE_COMMIT`
copies it into live authority at VBI. `REMOTE_FOLLOW` sees only that target and
the latest joy. Two snapshots parsed before a VBI can collapse to one target.
Even separately committed targets disappear if animation has not consumed them.

For a turn, an old render cell plus the newest target does not identify the
route the actor took. The fallback tries X before Y, checks the current map,
and eventually snaps. Fixing its direction branch cannot reconstruct a lost
corner. Increasing snap thresholds alone can prolong visible lag.

### 5. Current diagnostics under-report remote snapping

`CKMVAP` records a remote hard snap with `NET_DIAG_BUMP`; `RF_SNAP` calls
`NET_AUTH_REPOS` directly without that diagnostic. Also, after Phase 4 immediate
remote authority commits, remote `LOCX/LOCY` usually already equal `NET_PX_*`,
so the older simulation-gap diagnostic is particularly uninformative.

Measure render motion including `MOVEST`, `DIR`, and subcell phase. Raw
`RNDX/RNDY` alone are not pixel positions: left/up commit their render cell at
animation start, right/down at completion. Zero simulation gap proves neither
smooth motion nor low display latency.

## Real-server TCP measurement

Added `tests/rig/tcp_movement_probe.py`. It launches its own temporary server
on an ephemeral loopback port, loads an open interior map, disables zombies,
and sends directional commands every seven nominal video frames. It validates
COBS/CRC and records every snapshot plus its application ACK. It reverses
before the border, so a wall does not explain a pause. Only its own server is
terminated; the user's existing server is untouched.

Reproduction (run twice per video rate):

```sh
make build/maze-war-server
python3 tests/rig/tcp_movement_probe.py --fps 60 --seconds 10 > /tmp/cadence-60.json
python3 tests/rig/tcp_movement_probe.py --fps 50 --seconds 10 > /tmp/cadence-50.json
```

Observed on 2026-09-10, no injected lag:

| Nominal sender | Run | Inputs sent/applied | Snapshots | ~100 ms application intervals | ~200 ms application intervals |
| --- | --- | --- | --- | --- | --- |
| 60 Hz / 7 | A | 85 / 85 | 99 | 71 | 13 |
| 60 Hz / 7 | B | 85 / 85 | 100 | 70 | 14 |
| 50 Hz / 7 | A | 71 / 70 | 99 | 42 | 27 |
| 50 Hz / 7 | B | 71 / 71 | 100 | 42 | 28 |

Application intervals are rounded to the nearest 100 ms for grouping. Snapshot
arrival intervals across all runs were 99.17–101.07 ms. Every observed newly
applied command moved exactly one cell. The one outstanding command at the end
of PAL run A is an observation-window boundary, not demonstrated packet loss.

The server was publishing steadily while movement paused. This is expected
from `apply_queued_input`: it applies one waiting command each 100 ms tick,
and sets neutral when no command is waiting. Seven-frame send pacing supplies
fewer than ten commands per second, necessarily creating empty ticks. The
result is 100/200 ms movement spacing instead of uniform 116.7/140 ms spacing.

This is host TCP evidence only. It does not measure FujiNet forwarding, SIO,
VBI execution time, actual PAL machines, or pixels. The 50 Hz run simulates a
PAL sender's command interval. No new emulator or real-hardware playback test
was performed during this review.

## Protocol and server implications

- The protocol document was stale: directional repeats are not coalesced,
  only neutral keepalives are; empty queues immediately produce neutral, with
  no `INPUT_REPEAT_MAX` behavior. Corrected those descriptions in this pass.
- Packet sequence is shared with shots, names, brick events, and other traffic.
  It is not a simulation tick or elapsed-time value. Snapshot history needs
  explicit timing to handle variable rates and gaps reliably.
- Snapshots carry integer cells and current joy, with no movement phase,
  timestamp, or ordinary-move versus discontinuity marker. TCP ordering does
  not supply presentation timing or restore history discarded by the client.
- `next_tick = now + tick_ms` shifts deadlines when an iteration is late.
  The clean baseline did not show a significant host scheduling stall, but a
  fixed deadline accumulator with bounded overrun handling is preferable.
- Host sockets already use nonblocking I/O and TCP_NODELAY. There is no evidence
  here that changing transport again is the first fix. A snapshot is 24 wire
  bytes including framing/CRC, about 240 bytes/s at 10 Hz; snapshots alone do
  not establish serial saturation. Duplicate reliable/legacy event traffic,
  queued old snapshots, and FujiNet buffering still need load measurements.
- Pending-input correctness also needs attention before a cadence rewrite:
  `NET_LOCAL_REPLAY_PENDING` loads zero then takes `BEQ NLRP_X` before its replay
  loop; the loop is unreachable. The server advances receive freshness before
  checking queue capacity, and a later cumulative ACK can pass a dropped input.
  Atari's pending ring is local reconciliation state, not automatically an
  outbound retransmission queue. Do not rely on comments promising replay to
  solve queue overflow. These are adjacent contract risks, not measured causes
  of the clean-loopback remote pauses above.

## Recommended implementation sequence

### A. Make the current renderer correct and measurable first

1. Fix the wrong rightward branch and preserve recovery distance. Add behavioral
   execution checks for all four directions, neutral joy, one/two/three-cell
   gaps, blocked routes, and counters. Prefer executing assembled routines with
   controlled memory over another grep-only assertion.
2. Add per-remote snap reasons/counts, maximum render age, animation start/finish
   frame stamps, overwritten-target counts, and RX high-water marks. Keep VBI
   instrumentation small and read it in batches to avoid disturbing timing.
3. Drive remote animation phase from elapsed display time, with enough capacity
   for ten cells/s and bounded recovery headroom. Preserve all four visual
   phases and draw/erase invariants. Do not merely halve MOVRATE or perform two
   whole-cell starts in one VBI: those can skip pictures and corrupt pointers.
4. Test this as one small gameplay checkpoint before extending the wire format.
   It should remove wrong-way recovery and structural render backlog, but the
   100/200 ms authoritative spacing will remain.

### B. Replace latest-cell chasing with bounded timed playback

1. Keep a small fixed-size ring of authoritative remote movement samples before
   the latest-wins authority handoff discards them. Simulation remains current;
   only presentation consumes history. Preserve intermediate corners.
2. Add a versioned snapshot extension or negotiated packet carrying a dedicated
   simulation tick and tick duration; explicitly identify respawn/slot resets.
   Update the server and both Linux clients together. Do not reinterpret the
   existing global packet sequence as time or silently change v1 lengths.
3. Start with a measured one-tick presentation delay, then tune a bounded buffer
   using underrun and age measurements. Retain legal segments through turns;
   do not interpolate diagonally through a brick when a sample is missing.
4. On short underrun hold at the last known endpoint; on overflow or a genuine
   discontinuity perform an explicit bounded reset. Never accumulate an
   unbounded visual queue. Clear samples across death/respawn and slot reuse.

This adds deliberate presentation delay (initially about 100 ms), so it must
replace growing accidental backlog, not stack on top of the current slow
follower. Smoothness and latency must be reported separately. A one-tick buffer
may underrun with 200 ms command gaps; measure that tradeoff rather than calling
the initial buffer size a proven solution.

### C. Align the input/application/prediction contract

1. Use a common wall-clock logical movement rate, independent of PAL/NTSC and
   network poll frequency. At 10 Hz that is six nominal NTSC or five PAL frames,
   but simply changing the constant is insufficient: command ownership,
   prediction starts, and animation capacity must agree first.
2. Retain server authority and explicit per-command identity. Specify intended
   tick/application deadline, ACK meaning, late-command handling, queue bounds,
   and overflow recovery. Match local prediction to the same logical command
   schedule; record whether each command was actually predicted.
3. Use fixed server deadlines with bounded missed-tick policy. Validate rapid
   turns and stop/fire edges under input bursts; do not introduce unlimited
   catch-up, fold directional steps, or restore blind held-input repetition.
4. Repair and test pending replay/overflow semantics as part of this contract.
   Changing to held-input duration commands would be an alternative protocol
   design requiring coordinated prediction changes, not a server-only patch.

### D. Acceptance before commit

Replace the old UDP movement rig for these tests; its README explicitly says
its bots/relay have not migrated to TCP. Retain its lessons about instrument
pacing and bot liveness. Run normal-speed emulation, not turbo, with repeated
traces per condition and video standard.

Exercise straight runs, both orders of corners, reversals, stop/start, fire,
two moving humans, zombies, and join/death/respawn, first on clean TCP and then
with controlled delay, jitter, batching, and stream stalls. Distinguish TCP
stall simulation from deliberately corrupting the SIO byte stream.

Acceptance requires no wrong-way movement, no increasing render age during a
sustained run, no routine recovery snaps on clean LAN, preserved corner paths,
and exact agreement on applied versus predicted command accounting. Record
median/p95/worst input-to-apply delay and apply-to-display delay, buffer age,
underruns, and frame-phase intervals. Establish numeric latency limits from the
baseline before claiming a perceptual improvement. Run combat/respawn/transport
regression coverage, then the user's emulator/emulator and hardware/emulator
feel checks. The user's successful test remains the commit gate.
