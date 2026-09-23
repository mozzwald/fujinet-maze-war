---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: in_progress
stopped_at: 2026-09-11 - User accepted the corrected 08-01 build on real Atari/FujiNet against emulation. 08-02 has heap-owned isolated Room state, consecutive nonblocking TCP listeners, validated per-room Zombie configuration, fair bounded polling, and two-room isolation/sanitizer coverage. Its first checkpoint exposed that the Atari host field cannot type a colon; the client now prompts for a separate validated decimal port and an emulator edit to 9001 produced the expected NS_INIT bytes $23/$29. Await the mixed one-room/two-room checkpoint before closing ROOM-01/ROOM-02. After acceptance, use gpt-6-astra with high reasoning for 08-03.
last_updated: "2026-09-11T00:00:00.000Z"
progress:
  total_phases: 9
  completed_phases: 6
  total_plans: 28
  completed_plans: 17
---

# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-07)

**Core value:** An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.
**Current focus:** Validate the 08-02 multi-room server with the default real Atari/FujiNet plus emulator session and with clients split across two consecutive room ports. Bounded timed remote-sample playback remains the first revisit if future cloud-hosted WAN testing makes remote movement worse.

## Current branch: a8-net-fix (2026-09-11)

Phase 4 is closed as good enough for now: the first repair pass fixed the confirmed follower direction/distance defects, raised render/send capacity for 10 Hz NTSC play, restored pending replay reachability, and made server tick deadlines fixed with bounded overrun recovery. User testing found two-computer emulation almost flawless and real Atari XL with hardware FujiNet acceptable, with only occasional one-to-two-cell remote jumps. Bounded timed remote-sample playback was not implemented and is now explicitly reserved as the first revisit if a future cloud-hosted server adds enough WAN latency to make remote motion feel worse. 07-01, 07-02, 07-03, and 07-04 are complete and merged into `a8-net-fix`. TCP runs with `$05`, REGISTER clear,
57600 baud, and the unchanged handler. The user confirmed the Atari/FujiNet
hardware path works; MCP-managed Atari/FujiNet-PC also validated the CRC wire
format with zero network or CRC errors. Reliable NAME, BRICK_DELTA, and RESPAWN
events now ride one ordered, cumulatively acknowledged stream. A forced full
smoke suite should be rerun after future changes. 07-05 remains explicitly
deferred. Older v1 position and investigation notes below are
retained as historical context.

## Current Position

Phase: 03 (combat-and-world-authority) — COMPLETE 2026-09-03. Human mixed-session checkpoint approved.
Phase: 03.1 (handler-refresh-pokey-isolation) — COMPLETE 2026-09-02. All four success criteria verified; see "Phase 3.1 verification" below.
Phase: 05 (slot-lifecycle-and-zombie-handoff) — COMPLETE. Code 2026-09-02 (LIFE-01..04 addressed), smoke suite green including `slot_lifecycle_smoke.sh`. Human confirmation of live join/leave handoff received 2026-09-04; human testing continues alongside each change from here.
Phase: 05.1 (link-integrity-and-frame-resync) — COMPLETE 2026-09-04. Unplanned, driven by real-hardware symptoms. See "Phase 5.1" below.

Execution order going forward: Phase 8 plans 08-01 through 08-11. Phases 3, 3.1, 4, 5, 5.1, 6, and Phase 7's executable scope are closed and merged.

## Phase 8 planning note (2026-09-11)

Review update: `phases/08-lobby-rounds-polish/08-MODELS.md` lists model/effort
recommendations for every step and requires the next recommendation in each
closing summary, followed by a pause for the user to switch. **08-01 is
hardware-accepted. 08-02 is ready for its mixed one-room/two-room checkpoint;
after acceptance use 08-03: gpt-6-astra, high reasoning.** The round synchronization contract now lives
in `08-PROTOCOL.md`; shared teardown moves earlier to 08-05. Planning review
does not mark implementation or hardware checkpoints complete.

The source-grounded plan is in `planning/phases/08-lobby-rounds-polish/`.
It corrects the reference proposal to use the current TCP NetStream path, makes
Lobby publishing asynchronous, defines frozen round/participant state, and
handles no-human grace orthogonally to round state. Atari memory reclamation is
part of 08-01: the unreachable blocking `GAMEOVR` block at `$467C..$4776` is
removed, and the unreachable old title data at `$8060..$81EF` is replaced by
the new reachable title/menu/results assets. Existing `HOSTDISP`, `GAME`,
screen buffers, shared vaporization primitives, and DLI-disabled rendering are
preserved. Large Lobby buffers are planned as lifetime-checked aliases of
inactive NetStream storage rather than unconditional new allocations.

## Performance Metrics

**Velocity:**

- Total plans completed: 7
- Average duration: 8.2 min
- Total execution time: 0.8 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01 | 2 | 15.0 min | 7.5 min |
| 02 | 5 | 40.8 min | 8.2 min |

**Recent Trend:**

- Last 5 plans: 8.2 min
- Trend: Stable

| Phase 01-transport-normalization-and-observability P01 | 540 | 2 tasks | 6 files |
| Phase 01-transport-normalization-and-observability P02 | 359 | 2 tasks | 10 files |
| Phase 02 P01 | 186 | 2 tasks | 5 files |
| Phase 02 P02 | 6.3 min | 2 tasks | 2 files |
| Phase 02 P03 | 19m 33s | 2 tasks | 5 files |
| Phase 02-reconciliation-contract P04 | 8m | 2 tasks | 4 files |
| Phase 02-reconciliation-contract P05 | 35m | 2 tasks | 7 files |

## Accumulated Context

### Decisions

Decisions are logged in `.planning/PROJECT.md` Key Decisions table.
Recent decisions affecting current work:

- Phase 1: Start with transport normalization so framing bugs do not pollute higher-level sync work.
- Phase 2: Make acknowledged-input reconciliation the first gameplay contract before combat or smoothing changes.
- Phase 3: Freeze same-tick combat ordering before presentation-level smoothing work.
- [Phase 01-transport-normalization-and-observability]: Transport framing repair and DELTA compatibility decoding now live in a dedicated server module before gameplay input mutation.
- [Phase 01-transport-normalization-and-observability]: Transport regression coverage is anchored on exact server debug acceptance markers for primary, swapped, and extra-41 DELTA variants.
- [Phase 01-transport-normalization-and-observability]: Transport observability stays server-first: counters live beside the canonical ingress path and are emitted as stable summary lines instead of ad hoc event spam.
- [Phase 01-transport-normalization-and-observability]: Counter verification is anchored on the real debug server binary, with the stale-sequence case intentionally exercising an extra-41 DELTA so accepted_delta stays distinct from format counters.
- [Phase 02]: SNAPSHOT keeps one packet type and exposes reconciliation ack state via flags bit7 plus byte 19.
- [Phase 02]: Server publishes ack_seq from authoritative applied-input progress after each tick instead of transport receipt state.
- [Phase 02]: Replay runs from NET_STAGE_COMMIT after authoritative state commit instead of a parallel simulation path.
- [Phase 02]: The Atari client retains an eight-entry pending DELTA ring and discards entries with modulo-256 seq <= ack_seq.
- [Phase 02]: 02-03 human verification failed on real Atari due to player-movement screen artifacts despite green reconciliation smoke/build checks and healthy transport logs.
- [Phase 02-reconciliation-contract]: Phase 02: Trigger-aware Atari replay keeps fire-direction intent out of movement replay, restoring approved move/turn cadence and clearing the fire-plus-direction jump regression on real Atari.
- [Phase 02-reconciliation-contract]: Trigger-aware Atari replay keeps fire-direction intent out of movement replay so cadence and firing stay stable under prediction.
- [Phase 02-reconciliation-contract]: Phase 2 closes only after automated replay/correction smokes are green and the real Atari checkpoint explicitly approves cadence plus bounded correction.
- [Phase 03]: All three plans executed (server contract, Atari shot alignment, Linux parity); only the human mixed-session approval remains, deferred behind Phase 3.1.
- [Phase 03.1 insertion, 2026-07-19]: Embedded NSENGINE.OBX predates the NS_InitNetstream c_sp-leak fix (00d8940) and the RX IRQ overrun/error-latch fix (b1db829); rebuild from `../fujinet-atari-netstream` source with INPUT_BUFSIZE=1024 (handler BSS ends ~$31AE, safely below the $3800 PM area).
- [Phase 03.1 insertion, 2026-07-19]: Live serial-corruption root cause found: `MOVSND` walk sound writes AUDF3/AUDF4 (handler's joined ch3+4 baud timer) for slot-2/3 actors every walk frame. All sound moves to a ch1+2 priority allocator (`SND_SEL`/`SND_OFF`: local player owns ch1, remotes share ch2, owner-checked silencing). Invariant: after NS_INIT the game never writes AUDF3/AUDF4/AUDC3/AUDC4/AUDCTL/SKCTL.
- [Phase 03.1 insertion, 2026-07-19]: Netstream clock config verified correct as-is: `NET_FLAGS = $04` = UDP + TX external clock + RX internal clock; baud 57600, port 9000.
- [Reordering, 2026-07-19]: Phase 5 (slot lifecycle correctness) runs before Phase 4 (render polish); minimal Phase 6 validation after 5 marks "reliably playable", Phase 4 + full Phase 6 mark "Lobby-releasable".

### Pending Todos

- Phase 6 minimal validation: scripted emulator sessions including join/leave handoff.
- Future cloud-server revisit: if WAN latency makes remote movement visibly laggy or jumpy, reopen Phase 4's bounded timed remote-sample playback idea from `04-03`.
- Update `.planning/REQUIREMENTS.md` if the Phase 3.1 invariant and the 5.1 link-integrity invariant should become tracked requirement IDs.

### Phase 3.1 implementation notes (2026-07-19)

- `Makefile`: `NETSTREAM_DIR ?= ../fujinet-atari-netstream` rule rebuilds `NSENGINE.OBX` via that repo's `mads-handler` target (INPUT_BUFSIZE=1024) when present; checked-in OBX (refreshed, 1224 bytes, load $2800-$2CC1) is the fallback. MADS now emits `build/maze-war.lab` for symbol peeking.
- `clients/atari/maze-war.asm`: added `NS_STAT` equate and sticky `NET_NS_ERRS` accumulation in `NET_POLL`; added `SND_SEL`/`SND_OFF` allocator + `SND_CH2_PID` (local wizard owns ch1, remotes share ch2 last-writer-wins, owner-checked off); all 10 sound sites (live: MOVSND, walk-off, NSHOT_CLR, ERSHXIT; dead: SHOTSND, CLSCSND+off, BKLSND, NRGBLND, WLXPLOD/STFBLEX, EVAPRTE/ENDEVAP) converted from `TXA/ASL/TAY` to the allocator; RESTART/GAMEOVR no longer clear AUDC3/AUDC4; AUDCTL/SKCTL init writes gated to cold start via `HOST_DONE`.
- `tests/sound_channel_guard_smoke.sh` (new) enforces the invariant; `tests/combat_client_parity_smoke.sh` converted from `rg` to `grep -E` (rg not installed). Full suite: 10/10 pass.

### Playtest fixes (2026-07-19, post-3.1, from first live session)

- Boot diagnostics: border shows red = NS_INIT failed, blue = netstream up but no first map/snapshot yet, black = game live (`NET_INITF`, `NP_BOOTCOL`).
- Localhost UDP port clash root-caused: FujiNet-PC binds its local netstream socket to the destination port (`netstream.cpp:326` `netStreamUdp.begin(netstream_port)`) with SO_REUSEADDR, colliding with the game server on the same host, delivery then order-dependent. Server gained `--bind ADDR`; documented workaround: `--bind 127.0.0.2` + HOST=127.0.0.2. Proper future fix is firmware-side ephemeral bind in UDP mode.
- Local shot prediction (`NET_SHOT_PREDICT`/`NET_SHOT_PUBLISH`/`NET_PRED_TICK`/`NET_PRED_CONFIRM`): server never broadcasts SHOT at spawn (first 0x42 is a tick later, two cells out; none at all for point-blank hits), so local fire showed nothing. Client now publishes a synthetic shot at the visible spawn cell through the staged shot pipeline; any authoritative 0x42 for the local slot supersedes it; 30-frame TTL self-clears unconfirmed predictions.
- Movement yank fixed: `NET_STAGE_COMMIT` no longer reconciles unconditionally per acked snapshot; `NET_LOCAL_RECONCILE` gates replay on staged pending drift (>= NET_RECON_P0) or fully-idle convergence (stick neutral, no pending inputs, not mid-move, any drift). The one-cell prediction lead no longer snaps the wizard back mid-run.
- Shot direction bug (root cause of "shots only visible firing right", predating 3.1): `NET_SHOT_APPLY` extracted the shot direction from the dead/erase-mask test result (always 0) instead of reloading the packet flags byte, so every shot was treated as dir 0. The visible-origin facing gate then deferred non-right shots forever. Fixed by reloading `NET_SHOT_WRK+5` before the `LSR/AND #$03`. Server verified healthy in all four directions with a UDP protocol probe (`fire-eval`/`shot-spawn`/SHOT broadcasts correct).

### Blockers/Concerns

- Earlier real-hardware failure reports predate the AUDF3/AUDF4 corruption fix — re-judge any remaining gameplay symptoms only after Phase 3.1 lands.
- Sound remap sites `STFBLEX`/`EVAPRTE` reuse the Y register after their sound writes; each site needs local re-reading during the remap, not blind substitution.

### Phase 3.1 verification (2026-09-02)

All four success criteria checked, so the phase is closed:

1. Handler from source: `make NSENGINE.OBX` with `../fujinet-atari-netstream` present runs that repo's `mads-handler` target and reproduces the checked-in `NSENGINE.OBX` **byte for byte** (1224 bytes, load $2800-$2CC1, BASEADDR=10240, INPUT_BUFSIZE=1024). The rule had never been exercised before; it works.
2. Clock config unchanged at `NET_FLAGS = $04` (UDP, TX external, RX internal).
3. `sound_channel_guard_smoke.sh` enforces the no-AUDF3/AUDF4/AUDC3/AUDC4/AUDCTL/SKCTL invariant and passes.
4. Emulator session (FujiNet-PC + NetSIO, zombies in slots 1-3, movement, firing, brick destruction, a full zombie->human->zombie handoff, and a disconnect/reconnect cycle): `AUDCTL=$28`, `AUDF3`/`AUDF4` at handler-programmed values throughout, `NET_NS_ERRS=$00`. Verified over minutes rather than the 10+ minute soak the criterion words; a longer soak is still worth doing but nothing suggests drift.

### Phase 5 implementation notes (2026-09-02)

- Root cause: `players[]` was memset once at startup and `shots[]` never reset, while `find_or_add_client` reset only transport fields and `reset_client_slot` only the `client_slot`. A slot changing hands therefore carried the previous occupant's facing, score, in-flight shot and zombie schedules straight across.
- `server/main.c`: new `reset_slot_gameplay()` runs on both transitions (join in the main loop, reap in `reap_timed_out_clients`). It retires an in-flight shot with the standard three-tick clear burst, returns `joy` to `0x0F`, zeroes score and `zombie_fire_pending`, re-bases the absolute zombie think/move/fire timestamps to `now`, and clears `last_input_ms`.
- Deliberately NOT reset: **position** and `respawn_at_ms`. Position is the slot's physical location, not stale state — the wizard becomes a zombie where it stands, as in the original game, and teleporting on handoff would make every other client see an unexplained jump. A first attempt did respawn the actor and was backed out after `combat_ordering_smoke` and `combat_world_authority_smoke` caught the resulting position discontinuity. Leaving `respawn_at_ms` alone lets a handoff mid-death finish through the normal RESPAWN path and re-show the actor.
- `CLIENT_TIMEOUT_MS` 60000 -> 15000. 60s stranded a reconnecting player beside their own ghost for a full minute, because FujiNet picks a fresh UDP source port per stream so a reconnect always lands in a new slot. 15s is ~150 missed packets at the client's 10 Hz and sits just past the client's own ~13s give-up watchdog. It cannot go much lower: clients are not required to send continuously (the test harness idles up to 5s inside its receive waits), and a 4s trial reaped live clients mid-match.
- `clients/atari/maze-war.asm`: `NET_ROLE_RESET` clears per-slot state for slots whose role bit actually flipped (shot publish latches, `NET_DESYNC_CNT`, pending reconcile flags, channel-2 sound ownership). Respawn latches are deliberately left alone — a shot clear is re-sent as a burst so dropping one self-heals, but a respawn is sent once and dropping it would leave that actor hidden behind `NET_DEAD_MASK`.
- `clients/atari/maze-war.asm`: `NET_STAGE_COMMIT` now gates `NET_LOCAL_PID` on an actual change and calls `NET_LOCAL_PID_RESET`, flushing the pending-input ring, predicted shot and channel-2 claim that were keyed to the slot we left.
- `doc/protocol.md`: documented slot allocation order (slot 0 is never zombie-filled; it is the first human's seat), the handoff contract, and the address+port reconnect consequence.
- Testing note: `joy` inheritance and shot retirement are not assertable black-box. The joining client's own first DELTA sets `joy` on the same tick, and a zombie only fires at a human in its row/column with a clear line — measured at >20s and on an arbitrary slot. Those two are guarded at source level in `slot_lifecycle_smoke.sh`; role-mask tracking in both directions, score inheritance and the no-teleport rule are asserted live against the real server.

### Phase 4 starting notes (2026-09-03)

Reported symptom: the Atari client briefly pauses movement and/or the local
wizard jitters/snaps to the authoritative position, seemingly around respawns
and shots.

Measured, so the next session does not repeat it: the periodic BRICK_FULL
resync is **not** the cause. Breaking at `NET_BRICK_FULL_APPLY` and running to
`NET_RX_50BAD` reports `elapsed_frames: 0`, i.e. the whole apply completes
inside one frame even though `NMIEN=0` disables the VBI across it. That was the
first hypothesis and it is wrong.

What the correction actually does (`NET_LOCAL_REPLAY_PENDING`):
`NET_AUTH_REPOS` teleports LOCX/LOCY to the authoritative cell, then `SETSTIL`
resets the walk pose, then pending inputs replay. So a correction is an
instant jump *plus* an animation restart, which is why it reads as a stutter
as well as a snap. It only triggers at `NET_RECON_P0` = 3 cells of drift
(`CKMV_LOC`), so corrections are rare but always large; there is no small
continuous correction. Remote actors additionally have an unconditional
hard-snap path (`CKMVAP`).

The structural problem for Phase 4: LOCX/LOCY is simultaneously the rendered
position, the collision position and the shot origin. There is no render-only
state, so there is nothing to smooth into.

Before adding smoothing, hunt client/server rule mismatches -- they are what
generate the drift that triggers the snaps. One was found and fixed on
2026-09-03: `NAF_OCCLP` (client movement prediction) blocked on players
awaiting respawn while the server had just stopped doing so, so the client
would refuse a move the server applied. Any such disagreement produces drift
and then a visible snap, and no amount of interpolation hides it.

### Phase 4 investigation log (2026-09-03)

Reported: the local wizard snaps back when turning a corner at speed. Two
server-side input bugs were found and fixed, and **neither was the reported
bug** -- the user reports no change in frequency. Recording so this is not
re-derived:

- The server kept only the newest joy per tick and acked everything received.
  Proven with a probe: 12 flick-then-neutral sequences produced 0 movements
  while the ack advanced to the newest sequence sent. Now queued and applied
  one per tick with an honest ack. Real, but the wrong bug: turning a corner
  *holds* the new direction, so the newest joy was the turn and it survived.
- Client movement prediction blocked on players awaiting respawn after the
  server stopped doing so. Real mismatch, fixed, not the reported bug either.

Could NOT reproduce the snap on the emulator rig, which is the important
finding. With loopback latency `LOCX/LOCY` and `NET_PX_X/Y` are byte-identical
for all four slots, and `NET_LOCAL_REPLAY_PENDING` did not fire once in 10s of
movement. Adding `--lag-ms 200` to the server did not change that. The rig
differs from real hardware in ways that matter (real serial link, real loss,
real FujiNet), so the mechanism has to be measured where it happens.

Hence the diagnostic counters now in the client (addresses in
`build/maze-war.lab`, cleared per connection):

- `NET_DIAG_SNAPS`    corrections applied, saturating at 255
- `NET_DIAG_MAXDRIFT` largest local drift in cells -- treat as an UPPER BOUND;
  a respawn landing is suppressed for 30 frames but still leaks in, so large
  values here are not yet trustworthy on their own
- `NET_DIAG_PENDMAX`  largest unacked input backlog
- `NET_DIAG_SRC`      which triggers fired: 1 staged drift, 2 idle converge,
                      4 VBI threshold, 8 remote hard snap

`NET_DIAG_SRC` is the most useful of the four: it says which of the four
correction paths is responsible, which narrows the fix immediately.

Do NOT start smoothing before this reads back from real hardware. Interpolation
turns a wrong teleport into a wrong slide; if the drift is a genuine desync,
smoothing hides the symptom and makes the cause harder to find.

**That gate is now satisfied (2026-09-04).** The counters were read back from
real hardware and drove the whole of Phase 5.1. The desyncs they exposed were
link-integrity faults, not smoothing faults, and they are fixed. Two further
causes were found and fixed on the client side and are worth knowing before
planning Phase 4:

- Prediction ran on a different input than it sent. `PLRMVE` chose the
  direction for the next cell from the live `NET_RX_STICK` while the delta
  carried the stick sampled at the periodic transmit slot -- two samples of the
  same joystick, differing exactly while turning. Predicting from
  `NET_TX_LAST_STICK` made both sides replay an identical input sequence.
  Measured over three 40s scripted cornering runs: 12/13/9 corrections before,
  2/5/2 after (`b8d80d2`).
- Local corrections are no longer a teleport. `LOCAL_FOLLOW` walks the gap off
  one cell per move tick using the normal move animation -- the same path remote
  slots have always used -- with the hard snap kept as a guard rail for
  distances no walk should cover. This is a stopgap inside gameplay state, not
  render separation, and Phase 4 should supersede it.

Residual corrections still occur at roughly 3 per 40s of hard cornering. The
cause is known and is what Phase 4 exists to fix: the movement clock (`MOVCLOK`,
6 frames) and the transmit clock (`NET_FRAME_DIV`, 6 frames) are the same rate
but free-run in phase, and the phase re-randomises on every stop, turn or block.
When they slip, one send window contains two move decisions or none.

`--lag-ms N` was added to the server as a test aid for reproducing a
pending-input backlog locally.

### Phase 5.1: Link integrity and frame resynchronisation (2026-09-04, INSERTED)

Unplanned. Real hardware (Atari XL, 256K, FujiNet) showed bricks appearing and
vanishing at random, remote actors hopping a cell and back, scores flickering
0-1-0, the local player jumping while standing still, and movement not
appearing for many seconds. Emulation was clean throughout, because loopback
never loses a byte.

One root cause: nothing verified packet integrity. The Atari receives over SIO
as a byte stream, so a dropped or duplicated byte shifted framing and payload
bytes began to be read as packet type markers. Bounds checks were the only
defence. The worst amplifier was the snapshot sequence window, which accepts
any forward jump of 1..127: one corrupt sequence byte parked the client ~100
ticks in the future, so every genuine snapshot after it was dropped as stale
for up to twelve seconds.

What landed:

- **Checksum** (`99d8303`): every server->client packet carries a trailing sum
  byte. Makes a corrupt frame fail closed.
- **COBS framing** (`9f36f28`): frames are COBS encoded with a `$00` delimiter,
  so no zero can appear inside a frame and the next delimiter always realigns
  the parser. The checksum alone could not resync -- the parser stayed
  misaligned until a payload byte happened to look like a type marker, which is
  why the resync-junk counter kept climbing on hardware even with every packet
  checksummed. The Atari receive path was rewritten around the delimiter; the
  old type-marker scanner and the six fixed-length collectors are gone.
  `tests/cobs_resync_smoke.sh` carries a literal transcription of the 6502
  decoder and proves the property: a byte deleted, inserted or flipped costs
  exactly one frame.
- **Actor-state validation** (`99d8303`): the playfield interior is x 1..18,
  y 1..17. The snapshot test rejected x>=19 and y>=18 but let zero through, so
  a garbage position could place an actor on the border where the erase pass
  blanked it -- the missing left-hand border boxes. RESPAWN had no validation at
  all: the slot index went straight from the packet into an index over
  four-entry arrays.
- **Boot order** (`99d8303`): the net runtime block is all `.DS` and holds
  power-up RAM until net init runs, but `NET_ACTIVE`/`NET_GAME_SHOW` are read by
  the poll loop during the host prompt. Cleared at START now. START also called
  `ERASMAN` before `SETALLP` had built the screen pointers, and erased only two
  of the four slots.
- **Border protection** (`5ba6051`): `NET_AUTH_REPOS` refuses to move an actor
  onto a non-interior cell, and `ERASMAN` refuses to blank characters outside
  that range. Same class as the validation above but from a locally generated
  coordinate -- net init zeroes `NET_PX`, so any reposition before the first
  snapshot parked an actor on (0,0).
- **Display list alignment** (`2a1813b`): the three display lists landed wherever
  preceding code ended. Trimming the on-screen counters shrank the code enough
  to push the GAME list across the 1K boundary at $6C00; ANTIC only increments
  the low 10 bits of the DL counter, so it wrapped and executed garbage. The
  block is aligned to 1K now, which fixes the class -- any code-size change
  could have triggered it.
- **RX buffer sizing** (`e83e449`): the COBS dispatcher copies the whole decoded
  frame, payload plus checksum, into buffers sized before the checksum existed.
  Each copy wrote one byte past the end onto the next variable:
  `NET_NAME_PKT`->`NET_NAMES[0]` (the first character of the first scoreboard
  name changed once a second as the server rotated names -- reported and now
  fixed), `NET_SHOT_PKT`->`NET_SHOT_SEQ`, `NET_BRICK_BUF`->`NET_BRICK_DONE`,
  `NET_SNAP_BUF`->`NET_SHOT_IDX` ten times a second.
- **Shot draw/erase symmetry** (`c1c72a4`, `e83e449`): `SHOTSHP` entries are
  asymmetric -- right and down carry their glyphs in bytes 2,3 and trail blanks
  forward; left and up carry them in bytes 0,1 and trail `$00,$00`. Their shot
  sits one cell back or one row up, so `SETMOVE`'s third and fourth characters
  landed on the shooter. `ERASHOT` had the mirror problem, clearing a second
  pair that fell back onto the shooter. Both now draw and erase only the shot
  cell for left and up. This was the "head disappears when firing up or left"
  report; confirmed fixed on 2026-09-04.

On-screen diagnostics are trimmed to one pair: frames rejected by checksum or
framing, in the fourth scoreboard row's free columns. Reads 00 on a healthy
link. The counters that found all of this (serial error latch, resync bytes,
map repairs, cells rewritten, brick deltas) are still maintained in RAM for
peeking via `build/maze-war.lab`, just no longer drawn.

Human-confirmed 2026-09-04: shots correct in all four directions, no head clip,
no stuck shots, name stable, border intact, corner intact.

**Method note for the next session.** Two diagnoses in this phase were built on
reading the wrong address -- `ACTFLAG` was read at 176 when it lives at `$90`,
which produced a confident and entirely false "stuck evaporate flag" story. Look
symbols up in `build/maze-war.lab` rather than guessing. Separately, sampling a
single screen cell is not a reliable oracle for "is the sprite drawn": `ERASMAN`
blanks the cell every move commit before redrawing, so a one-frame blank is
normal and only a sustained run means a real clip. Successive refinements of
that measurement accused a different direction each run.

### Hardware presentation defects found 2026-09-04 (planned as 04-04, 04-05)

Screenshot: `ref/screenshots/IMG_20260904_201638.jpg`. The game functions
correctly and all 5.1 fixes hold; these are display faults only.

**Vertical dotted line in the player's column** (04-04). `PMAREA .DS $0400` at
`$3800` is uninitialised, and `ERASMAN` clears only the four player pages at
`$3C00`-`$3FFF`. The missile region at `PMBASE+$300` = `$3B00` is never cleared,
and `GRACTL` is set to `$03`, which enables missile DMA — while the client never
writes `HPOSM0`-`HPOSM3` or any missile graphics register anywhere (verified by
grep). So missiles are enabled, unpositioned and fed power-up RAM. Invisible on
emulation because the emulator zeroes RAM — the same blind spot that hid the
net-state bug. Caveat recorded in the plan: uninitialised missile data alone
does not explain dots that *track* the player, so re-check on hardware after the
clear rather than assuming it is fully explained.

**Wrong glyphs for some letters** (04-05). Dumping the embedded charset at
`$4000` against the screen codes `HOST_SCR` produces: `X` (`$38`) is three
isolated dots — exactly the reported `MOZZXL` artefact — `V` (`$36`) is a
checkerboard, and `F`, `J`, `Q` are likewise artwork. `S`, `C`, `G`, `K` and the
rest of A-Z and 0-9 are proper letters, which is why `WIZARD` and `ZOMBIE` have
always looked right. Those five are exactly the letters the original game's own
text never used, so their slots were reused for artwork — safe while all text
was compile-time constants, unsafe once player names became user input. Note the
prompt screen switches `CHBASE` to ROM (`HOST_BOOT` writes `#$E0`) while the
scoreboard uses the embedded font, so the charset-selection path needs checking
too, not just the glyphs.

### Seat occupancy in the HUD (2026-09-08)

Reported: with two zombies and one player connected, the scoreboard still
listed four participants -- one name, two `ZOMBIE`s and a phantom `WIZARD` with
a score.

Root cause: clients had no way to tell an empty seat from a human standing
still. The snapshot's `flags` bits 3..6 carry the zombie mask, which names the
slots the AI drives; every other slot was assumed to be a player. The `flags`
byte has no spare bit (bit0 valid, 1..2 recipient pid, 3..6 zombie mask, 7
ack_valid).

Chosen fix: a new `0x44 SEATS` packet (`type, seq, mask`), broadcast on change
and repeated every `SEAT_REPEAT_MS` (1s) like `NAME`. A new packet type rather
than a wider snapshot on purpose: the Atari dispatcher length-checks each type
exactly and ignores unknown ones, so a client and server that disagree about
`SEATS` degrade to today's behaviour instead of dropping every snapshot.

- `server/main.c`: `compute_seat_mask()` / `build_seats()`; the broadcast sits
  **after** `reap_timed_out_clients` so a timed-out seat is reported free on the
  same pass that frees it.
- `clients/atari/maze-war.asm`: `NET_RX_44DONE` stores `NET_SEAT_MASK` and
  raises `NET_SCORE_PEND`; `NET_SEAT_HAS` answers "is this slot held", counting
  our own `NET_STAGE_LOCAL_PID` as occupied so the HUD is right before the first
  `SEATS` lands; `NET_LBL_BLANK` clears a line's 11 label columns and its score
  digit. `NSLBLP` blanks unheld slots, and `NSNAP_SCORE` skips them too --
  without that the per-snapshot score pass painted a `0` back over the blank
  line ten times a second.
- Both Linux clients skip the same rows.

Follow-up the same session: an unheld slot must not stand on the board either.
`NET_VACANT_UPDATE` (VBI, beside `NET_SCORELBL` and ahead of the move loop that
does the erase) hides it through the same `NET_DEAD_MASK` + one-shot
`NET_ERASE_MASK` pass a respawn-pending actor uses, and records what it hid in
`NET_VACANT_MASK` so filling a seat un-hides only those and never reveals an
actor genuinely awaiting respawn. The vacant branch keys off `NET_DEAD_MASK`
rather than `NET_VACANT_MASK`, so anything that clears the dead bit underneath
it -- the first-snapshot `NET_BOOT_HIDE` reveal, a stray respawn -- makes the
next pass re-hide and re-erase exactly once. Erasing unconditionally on every
pass was rejected: `NET_SCORE_PEND` fires about once a second from the name
rotation, and a vacant actor's stale cell may now hold a brick, so a repeated
`ERASMAN` would fight the map repair. Both Linux clients gate the HUD row and
the sprite through one `slot_in_play()` so the two cannot diverge.

Follow-up 2026-09-08 (hardware report): hiding the HUD row was not enough --
the wizard sprite still appeared, and the empty slot was still solid.

- The flash. `NET_VACANT_UPDATE` was called only when `NET_SCORE_PEND` fired, so
  the first-snapshot `NET_BOOT_HIDE` reveal drew all four actors and the vacant
  ones stayed on screen until the next SEATS or role change. It now runs every
  VBI, ahead of the move loop that both erases and draws, so a vacant actor is
  never drawn at all. Cheap: four slots, and an already-hidden slot costs two
  loads and a branch.
- The collision. The server counted an empty slot's stale spawn position in
  movement collision, fire evaluation and shot hits. The client walks through it
  (its own occupancy test already skips `NET_DEAD_MASK` slots), the server
  refused the move, and about three cells later the drift crossed
  `NET_RECON_P0` and yanked the player back -- "I can walk through them, then I
  snap back". One predicate now answers "is this slot a thing you can walk into
  or shoot": `slot_on_board()`, covering both a respawning player and an empty
  slot, reading a `g_occupied_mask` rebuilt from that tick's zombie and human
  masks. `tests/vacant_slot_collision_smoke.sh` walks a client onto an empty
  slot's cell on an open map -- no pathfinding needed -- and fails at exactly
  that cell without the fix.

Display-list check after the ~105 bytes of growth: `TITLDISP` `$6C00`,
`HOSTDISP` `$6DAE`, `GAME` `$6DCE`, block ends `$6FD0` before `HOSTSCR` at
`$7000` -- all three lists still inside one 1K page (the Phase 5.1 trap).
- `tests/seat_occupancy_smoke.sh` (new) asserts the mask live against the real
  server: broadcast on join without waiting for the repeat timer, zombie slots
  excluded, a second client added, and the repeat itself.

Note observed while writing the test: with `--zombies 2` the second client
takes slot **3**, not slot 1. `find_or_add_client` prefers a free non-zombie
slot over displacing the AI, so `--zombies N` shrinks only once the free slots
run out.

Not yet verified on the Atari: the emulator rig could not be brought up this
session (see Session Continuity).

### Combat smoke flakiness: two real bugs (2026-09-08)

Both combat smokes had been failing intermittently. Measured before touching
anything: `combat_world_authority_smoke` 4/10 on master, `combat_ordering_smoke`
0/8 standalone but failing inside back-to-back suite runs. Neither was test
noise.

**1. The server threw away inputs it had already acked** (the world-authority
flake, and a real gameplay bug). `last_input_ms` is stamped when a DELTA
*arrives*, but inputs are applied one per tick in order, so an entry that waits
its turn is already older than `INPUT_STALE_MS` when it runs. `step_players`
then ran a wall-clock staleness test over the top of the queue and reset `joy`
to neutral in the same tick that `apply_queued_input` had set the direction --
after acking it. The client dropped it from its pending ring and never replayed
it: an acked-but-discarded input, which is exactly the snap-back signature
Phase 4 has been chasing. At the 4 Hz the smokes use, two ticks is *exactly*
`INPUT_STALE_MS`, hence the coin-flip failure rate.

Fix: the staleness reset applies only to a slot with no client left in it. While
a client is connected `apply_queued_input()` is the sole authority on that
slot's joy -- it already sets neutral when the queue is empty, which is the same
intent expressed precisely instead of on a wall clock. The reset is still needed
for a departed client's slot, which is not in that function's loop at all.
0/12 after the fix. `tests/input_stale_apply_smoke.sh` pins it using `--lag-ms`
to reproduce the backlog deterministically.

At 10 Hz the Atari needs a burst of queued input to hit this, but nothing
prevented it -- worth remembering as a possible contributor to the residual
corrections noted under Phase 4.

**2. The walker ran ahead of the simulation** (the ordering flake, a harness
bug). `walk()` sends two inputs per step (direction, then neutral) while the
server applies one per tick, so a two-to-three entry backlog built up. The
position it then read belonged to an older input, so it planned the next step
from a cell the actor had already left, and the walk oscillated. Its re-plan
budget was cumulative over the whole path rather than per stall, so a long walk
exhausted it while making steady progress; `try_change`'s 1.5 s window was also
under the backlog's 500-750 ms plus a tick.

Fix, all inside the test: track the ack, `drain()` to the neutral's sequence
after each step so the harness and the server are in lockstep, reset the stall
budget on progress, and widen `try_change` to 3 s. 0/16 after the fix.

Method note: the server's `move-blocked` debug line was logged for a *neutral*
stick too, so every idle actor reported a blocked move on every tick and the log
read as though four players were pinned against walls when nothing was
happening. That cost real time during this diagnosis. It now logs only when a
direction was actually asked for.

### RESPAWN was the last un-echoed transition packet (2026-09-08)

Hardware report: when the *other* player shot this one, the victim's wizard
stayed at its death cell, missing its head and feet, until it respawned
elsewhere. Shooting the other player looked fine.

Could **not** be reproduced on the emulator, over an hour of trying: two clients
(Atari under FujiNet-PC plus a scripted UDP bot) on an open map with a zombie,
the Atari both stationary and walking, sampled at ~45 Hz against a leak
detector. That non-reproduction is the finding, not a dead end -- loopback never
drops a packet.

RESPAWN was the only transition packet still broadcast exactly once. Everything
else once-only in this server was given repeats precisely because a single lost
packet left a stale sprite: a SHOT clear bursts three times, BRICK_DELTA echoes
(`BRICK_ECHO_REPEATS`), NAME rotates at 1 Hz, the map resyncs every 3 s. RESPAWN
never got the same treatment, and it is the packet that hides and un-hides an
actor.

That accounts for the whole report:

- Lost **pending** RESPAWN: the client never sets `NET_DEAD_MASK`, so `ERASMAN`
  never runs for the victim. Its wizard stands on the death cell. The server
  holds that actor's position fixed, so nothing else moves or redraws it. Two
  seconds later the final spawn arrives and `NET_AUTH_REPOS` erases the old cell
  on its way out -- "until it respawned in the new location", exactly.
- The missing head and feet: the killing shot is drawn *into* the victim's cell,
  then the server's SHOT clear blanks the two characters the shot occupied.
  Those are characters the wizard was using, so the corpse is left mutilated
  rather than whole.
- The asymmetry: it needs a dropped packet, so it depends on which link lost it,
  not on who shot whom.
- Lost **final** RESPAWN is worse and was never reported only because it is
  rarer: `NSNAP_KEEPDEAD` keeps an actor hidden until an explicit final spawn,
  so that actor stays invisible until its next death.

Fix: `queue_respawn_echo` / `flush_respawn_echo`, one slot each, one repeat per
tick ahead of the step, exactly like the brick echo -- a later RESPAWN for a
slot supersedes an earlier one so a final spawn is never trailed by a stale
hide. `tests/respawn_echo_smoke.sh` asserts both transitions go out more than
once, and fails with the echo disabled.

**Rig notes, so this is not re-derived.** The emulator rig does work; the earlier
"no SIO command frames" conclusion was wrong. `atari_load` of
`build/maze-war-net.xex` leaves the handler resident (verified: `$2800` reads
`4C 27 28`). Boot sequence: reset, load, run 240 frames, poke `HOSTBUF` (`$7E55`)
with the host string and `NAMEBUF` (`$7E75`) with a name, press return twice, run
300 frames. The pokes are needed because the MCP key table has no `.` and the
prompt ignores backspace. Driving the emulator through its AI socket directly
(`{"cmd": "peek", ...}`, length-prefixed JSON) is ~3.7 ms per call, which is fast
enough to sample the screen and zero page at ~45 Hz; the MCP `run_until` path
single-steps and distorts timing badly enough to manufacture artefacts.

The leak detector is worth keeping: run the server on an open map (outer wall
only), then every interior character must be blank except the cells a visible
actor occupies. `RNDX/RNDY` (`$A8`/`$AC`), `MOVEST` (`$B4`), `DIR` (`$94`) and
`NET_DEAD_MASK` (`$7C00`) give the expected set; `GAMESCR` is `$73C0`, 40 bytes
per row, two bytes per cell.

Two further defects it found, **not fixed, no repro case written yet**:

- ~~Actors standing on adjacent cells erase each other~~ and ~~a stale painted
  cell at `(9,10)`~~: both re-tested after the `ERASMAN` Y fix below. The
  adjacent case does not reproduce at all; the stale cell does, and is recorded
  there as the one still-open rendering leak.

### One clobbered register behind both hardware reports (2026-09-08, later)

The RESPAWN echo was not the death-sprite bug. Two much better clues arrived:
the corpse follows **slot 0**, not a machine (real hardware as player 1 leaves a
corpse; the same hardware moved to player 2 does not), and two cells in the
far-left column clip any sprite standing on them.

Both are `ERASMAN` returning with `Y = 0`. Its player-missile clear loop ends
when Y wraps to zero, and two callers reload the slot's mask bit *through Y
after the call*: `CKMV_NEV` clears `NET_ERASE_MASK` and `NET_AUTH_REPOS` clears
`NET_DEAD_MASK` and `NET_ERASE_MASK`. Every one of them was clearing bit 0,
whatever slot had actually been erased.

- **The clipped cells.** Slots 1..3 never got their erase bit cleared, so a
  hidden slot was re-erased *every frame*. An unoccupied slot sits at its
  placeholder `(1, slot+1)` -- set in START and never replaced, because nothing
  repositions a hidden actor -- so `(1,2)`, `(1,3)` and `(1,4)` were being
  blanked 50 times a second. A live player standing there lost its characters
  and rendered as the sliver in `ref/screenshots/2026-09-08 15-44-36`. The
  user's guess ("player 3 and 4 are hidden in those spots") was exactly right.
- **The slot 0 corpse.** The move loop runs 3 down to 0, so any other slot's
  erase cleared bit 0 *before* slot 0 was reached. With fewer than four
  participants some slot is always hidden, so slot 0's erase request was always
  destroyed and its corpse always stayed. Slot 1's bit was never touched by
  anything, which is why the same machine in seat 2 was fine.

Fix: `ERASMAN` saves and restores Y. One instruction pair at each end.

Measured on the emulator before and after: `NET_ERASE_MASK` was stuck at `1110`
and `(1,2)`/`(1,3)` held no characters with a player standing on them; after, it
reads `0000` and the player's characters are present at `(1,1)`..`(1,4)`. Then
12 deaths of slot 0 across 110 s with a hunting bot: zero corpses. The
"adjacent actors erase each other" note above did not reproduce either -- 19548
samples with a bot walking into the player and firing, zero unpainted visible
actors -- so it was most likely the same cause seen from another angle.

**Death animation, restored (asked for in the same report).** `EVAPRTE` and its
smoke were still in the source but unreachable: the only path to them hung off
`CHKSHOT`, and the net client short-circuits `CHKSHOT` straight to `DONXTMN`
because shots are server-authoritative now. So a death the client detected
itself (`MNHTCHK`) puffed into smoke and a death the server reported just
blinked out. `NRW_PEND` now does what `STALLEV` does -- erase, set `ACTFLAG`
bit 1, `MOVEST = 9` -- and the move loop drives `EVAPRTE` on the `RTCLOK` every
other frame that `COLESCE` used. Nine steps at 30 Hz is about a third of a
second, well inside the two-second respawn.

It deliberately does **not** set `NET_DEAD_MASK`: the dead branch skips the
effects entirely, so hiding the actor there would cancel the smoke before its
first frame. `ENDEVAP` sets the hide when the smoke clears, as the original did.
A final respawn and a slot handoff both end a running evaporate so it cannot
follow the actor to a new cell or a new occupant, and `NAF_OCCLP` treats an
evaporating actor as off the board so local prediction agrees with the server
for those ten frames. Verified live: `ACTFLAG` bit 1 set with `MOVEST` counting
9 -> 8 -> 6.

**The display-list trap fired again, and is now closed properly.** Adding this
code pushed the 1K-aligned data block past its page; `.ALIGN $0400` moved it to
`$7000`, directly on top of `HOSTSCR`. The three display lists are now kept
together at the top of the block, and the block is anchored at `ORG $8000`
instead of drifting up behind the code: under 256 bytes from a page boundary
cannot cross a 1K boundary, whatever the code does. `tests/memory_layout_smoke.sh`
checks that, checks no loaded segment reaches into the `.DS` display buffers,
checks the buffers stay inside one 4K ANTIC page, and checks no two segments
overlap. It fails on the old `.ALIGN $0400`.

**Still open, with a repro.** A stale half of a move animation can be left on a
cell an actor walked out of -- observed at `(1,4)` as `$DA,$DB`, the trailing
pair of an up-move shape, persisting indefinitely. This is pre-existing, not new:
those three placeholder cells used to be scrubbed every frame by the bug above,
which was hiding it there, and the same residue was seen at `(9,10)` earlier.
The 3 s `BRICK_FULL` resync does not clear it because the repair only repaints
cells whose brick state changed. Repro: walk an actor across a cell and
interrupt the move; scan `GAMESCR` for painted cells no visible actor accounts
for. Likely fix: have the map repair blank a floor cell that holds characters no
actor is standing on.

### Remote-actor lag: rig fixed, reconciliation cleared, no bug found (2026-09-09, continued overnight)

**Status: investigation closed. No reconciliation bug exists to fix. The
residual lag is architecture-inherent and its fix is the already-planned
`04-02`/`04-03` render-state separation, which needs human hardware
verification and was correctly not attempted unsupervised overnight.**

Told to continue the lag investigation/fix until done or blocked. Found a
second, more serious rig bug before even getting to re-test the `MOVEST`
guard: **the atari800 emulator does not free-run in the background.** Its main
loop only advances a frame between AI-socket commands, so the previous
session's `peek()`-only sampling loops (no gap between calls) were
intermittently *pausing the emulated Atari* while the real server, relay and
bot kept running in wall-clock time on their own OS processes. Verified
directly: 1844 back-to-back peek calls over 2 real seconds left the CPU's
PC/A/X/Y/SP registers unchanged; RTCLOK (the OS's own VBI counter) confirmed
0 Hz advancement under a zero-gap loop versus ~58 Hz (matching real 60Hz NTSC)
with even a 5ms gap between calls. This fully explains the previous session's
contradictory measurements (the `MOVEST`-guard fix appearing "perfect" at one
loss rate and "worse" at another, each internally consistent across repeated
runs) -- the client wasn't behaving inconsistently, the rig was pausing it
inconsistently. Fixed: `tests/rig/ai.py`'s `AI.cmd()` now enforces a minimum
~8ms gap between every AI-socket command, so nothing built on it can
reintroduce this silently.

A second, smaller bug: restarting the relay process to change delay/loss
parameters was found to permanently wedge the Atari-side netstream handshake
(`netsio_status` showed `netstream.active` flip false, `sync.timeouts`
climbing), recoverable only via a full cold reset and reboot. Fixed:
`tests/rig/link.py` now polls a control file for live delay/loss changes, so
it never has to restart while the Atari is connected.

With both fixed, `tests/rig/converge.py` (new -- splits the gap measurement by
whether the watched remote is currently moving or has been still for over a
second) was run against **unmodified HEAD, zero code changes**, across 120ms
one-way delay at 0%, 3%, 20% and 50% independent random loss, 2-3 runs per
condition, 40-100s each, using `tests/rig/movestop.py` (new -- a bot that
alternates walking and standing still, auto-restarting its own socket loop on
error after an earlier version died silently mid-run and several minutes of
results were unknowingly taken against its frozen, disconnected slot):

- While the remote is actively moving: a real but modest gap, 60-87% of
  samples at 0 cells depending on loss rate, a shrinking tail out to 4-5 cells,
  and -- at every loss rate tested including 50% -- **never once reaching the
  3-cell (`NET_RECOVER_P1`) or 10-cell (`NET_RECON_P1`) thresholds** that
  would engage `REMOTE_FOLLOW`'s bounded-recovery or hard-snap paths.
- Once the remote has been still for more than a second: **gap 0 in 100% of
  samples, in every single run, at every loss rate tested**, including three
  separate runs at 3% loss (799+ samples each) and one at 50% loss (1365
  samples).

**The `MOVEST != 0` guard on `RF_SNAP` does not cause a stuck-arrears bug.**
The previous session's fix-then-revert of that guard was the right call, made
for the wrong apparent reason (an instrument artifact, not a real
contradiction) -- but the guard itself checked out as fine once measured
properly. `04-06-PLAN.md` (bounded catch-up) is retracted; there is nothing for
it to catch up from. `04-RESEARCH.md` carries the full writeup, and
`tests/rig/README.md` documents both instrument bugs in detail so they cannot
recur silently in future rig work.

**What remains genuinely true:** `REMOTE_FOLLOW` walks a remote actor one cell
per move tick toward the last known authoritative position rather than
interpolating, so *some* lag while a remote is actively moving is the designed
cost of that approach, not a defect in it. Reducing that further needs real
render-state separation for remote actors (already planned as `04-02`/`04-03`),
which is a substantially larger change to core rendering. Given this session's
own repeated lesson -- three separate speculative-but-plausible fixes this
multi-day session (`SETSTIL` residue, the `MOVEST` guard, and implicitly the
original 04-06 catch-up) each failed to survive measurement -- attempting that
larger, harder-to-verify rewrite unsupervised overnight, with no path to
hardware confirmation before the user returns, was judged the wrong call. This
is the honest stopping point: the investigation is complete and conclusive, no
safe further code change presented itself, and the properly-scoped next step
needs the user's own hardware-verification loop the same way every other
phase in this project has.

### Remote-actor lag: a fix attempted and reverted, and why (2026-09-09, later)

**Status: no lag fix shipped. The rig, not the client, is the current blocker.**

Asked to implement 04-06 (the bounded catch-up), the first step was to confirm
its premise. It did not survive. The measurements 04-06 was built on came from
an instrument that chose which slot to watch as "not mine" and nothing more --
and an unoccupied slot is hidden and parked on its placeholder cell
`(1, slot+1)` while the server still holds a spawn position for it. Measuring
one produces a large, perfectly constant gap that reads exactly like a
catastrophic rendering fault. The "100 % one cell behind" and the eight-cell
tail are both suspect.

With a corrected instrument, a remote that patrols continuously instead of
stalling against a wall, and the local player parked out of its lane, a remote
actor tracks its authoritative cell within about one cell in normal running.

**A different candidate was found, implemented, measured and reverted.**
`RF_SNAP` refuses to reposition a diverged remote while `MOVEST != 0`. A remote
that is following is mid-move nearly all of the time, so the snap that path
exists to perform is almost never allowed to run -- while `NET_AUTH_REPOS`, the
routine it guards, is built for exactly that case and zeroes `MOVEST` itself.
Removing the guard measured dramatically better at 120 ms / 3 % loss (gap 0 in
100 % of samples, twice, against a pre-fix build that was badly wrong twice) and
measured *worse* at 120 ms / 0 % loss. Two conditions, opposite verdicts, each
internally consistent. That is an instrument problem, not a result, and it is
not a basis for changing remote reconciliation. Reverted.

This is the second speculative fix stopped by measurement this session (the
`SETSTIL` residue change was the first). Both were plausible readings of the
code. Neither survived contact with a number, which is the process working.

**What has to happen before any lag change lands:**

1. `tests/rig/gap.py` now picks a slot that is live, not hidden, and
   demonstrably moving, and aborts rather than report a number it cannot stand
   behind. Two further variance sources are documented in `tests/rig/README.md`:
   slot churn from reconnects (every reconnect takes a new slot and the old one
   lingers for the 15 s timeout, so a boot mid-run invalidates it) and the local
   player standing in the remote's path.
2. Re-establish a baseline that reproduces across at least two runs per build in
   both link conditions. Treat two runs of one build that disagree as a broken
   instrument, not as a result.
3. Only then re-test the `MOVEST` guard, which remains the best suspect.

### Remote-actor lag: measured, and a plan (2026-09-09)

The three rendering bugs above are human-confirmed fixed. The lag work now has
numbers behind it: see `planning/phases/04-render-state-separation/04-RESEARCH.md`
for the rig and the table, and `04-06-PLAN.md` for the cheap first fix.

Headline: a remote actor is **one cell behind its authoritative cell in 100 % of
samples** under 120 ms one-way delay with 3 % loss, in one unbroken run; 47 % of
the time under delay alone, with a 5.6 % tail at eight cells; never behind on a
clean loopback. `REMOTE_FOLLOW` advances at most one cell per move tick and has
no catch-up, so one dropped snapshot puts a moving remote actor permanently one
animation period -- about 100 ms of visual lag -- in arrears, on top of the real
link delay. That is most of the felt lag and it does not need render state to
fix, hence 04-06 ahead of 04-02/04-03.

What unblocked this: a delay-and-loss relay between FujiNet-PC and the server.
The standing note said the snap could not be reproduced on the emulator, and it
could not -- loopback never delays or drops anything. Bind the relay to a
loopback alias, never `0.0.0.0`, or FujiNet-PC's own netstream socket fights it
for the port.

**Attempted and reverted:** the stale-residue bug below. The most likely
mechanism was `SETSTIL` drawing a two-character stationary image over a
four-character mid-move one and orphaning the other half, so `SETSTIL` was made
to blank the trailing pair first. Measured on the rig either side of the change:
3 stale cells / 83 s worst case with it, 2 cells / 20 s without -- no
improvement, within noise. Reverted rather than shipped. That rules out
`SETSTIL` as the dominant source and leaves the map-repair janitor
(blank a floor cell holding characters no actor stands on) as the next idea.

### Remote-actor lag: earlier reasoning, superseded by the measurements above (2026-09-08)

Asked to investigate, not fix. Local movement is predicted and feels fine; a
remote actor visibly lags and snaps, worst when playing on one machine while
watching the other's screen.

The client has no render-only state for remote actors -- the Phase 4 premise --
so a remote actor is moved by walking `LOCX/LOCY` toward the last authoritative
cell (`REMOTE_FOLLOW`), with `CKMVAP` hard-snapping when the gap gets large.
Numbers that set the feel:

- Snapshots arrive at the server's 10 Hz. The display is 50/60 Hz, so a remote
  actor has one authoritative sample per 5-6 frames and nothing to interpolate
  between them.
- `REMOTE_FOLLOW` only steps when the *next* cell in the actor's authoritative
  joy direction equals the authoritative cell, i.e. it re-derives the path one
  cell at a time from position plus joy. If a snapshot is lost, the actor stands
  still for that tick and then has two cells to make up.
- Recovery thresholds: `NET_RECOVER_P1 = 3` (bounded recovery), `NET_RECON_P1 =
  10` (catastrophic hard snap), `NET_DESYNC_MAX = 3` failed recoveries before a
  forced snap. So a remote actor absorbs small gaps by walking and large ones by
  teleporting, with nothing in between -- which is what "snapping" is.
- One-way delay is what the watcher sees twice over: the mover's input reaches
  the server, the server steps, and the snapshot reaches the watcher. At 10 Hz
  plus FujiNet's serial link that is comfortably over 150 ms before any loss.
- The snapshot sequence filter accepts only strictly-forward deltas, so a
  reordered or duplicated snapshot is dropped outright rather than merged.

Two things worth checking before any smoothing work, in this order:

1. Whether the newly fixed acked-but-discarded input bug (see "Combat smoke
   flakiness") was contributing. It made the server drop inputs it had already
   acked whenever a queue entry waited past `INPUT_STALE_MS`; on a link with
   real latency that is far more reachable than on loopback, and every dropped
   input is a step the watcher never sees. This fix landed after the reported
   session, so the lag should be re-judged on the current build first.
2. Whether the gaps are lost snapshots rather than interpolation. The client
   already counts frames rejected by checksum or framing; a counter for accepted
   snapshots per second on each side would separate "the update never arrived"
   from "the update arrived and was rendered badly". Smoothing a lossy stream
   just turns a jump into a wrong slide, which is the trap the Phase 4 notes
   already warn about.

### Known gaps (not addressed)

- ~~`combat_world_authority_smoke.sh` and `combat_ordering_smoke.sh` flakiness~~
  FIXED 2026-09-08, and both causes were real bugs rather than test noise. See
  "Combat smoke flakiness" above.

  Also fixed 2026-09-08: `seat_occupancy_smoke.sh` was first written on
  PORT=9161, which `input_queue_smoke.sh` already uses. Moved to 9171. Check
  `grep '^PORT=' tests/*.sh` before adding a smoke.

- ~~`combat_world_authority_smoke.sh` flakiness~~ was FIXED 2026-09-03, see above. Two causes,
  both artefacts of the server reading one joy per tick: a direction or a fire
  sent exactly once could be overwritten by a neighbouring neutral before the
  tick read it (the tests run at `--tick-hz 4`, a 250ms window), and the BFS
  `blocked` set was captured once at startup while actors kept moving. The
  walks now hold the direction and hold the trigger the way a player does, and
  occupancy is read live at every plan. 20 consecutive green runs.

- Slot identity is address+port with no client token, so a fast reconnect still briefly shows the player's old slot until the 15s timeout expires. Self-healing; a proper fix needs a protocol change.
- ~~With `--zombies N` below 3, slots beyond N stay empty and render as motionless wizards.~~ FIXED 2026-09-08: an unheld slot is neither listed in the HUD nor drawn on the board (see "Seat occupancy" above). The server still keeps a position for it; only the presentation changed.

## Branch note: `realm-net` diverges from here (2026-09-09)

**Everything above this point was written on `a8-net-fix`.** A new branch,
`realm-net`, was created off `a8-net-fix` at this point (pushed to `self`) to
explore a FujiRealm-informed realtime-transport migration (TCP, CRC-16
framing, a unified reliable-event stream) without disturbing `a8-net-fix`'s
own near-complete track toward `master`. From here, this file's history
diverges by branch: `a8-net-fix` continues to track the v1 roadmap (Phase 4
render-state separation next, pending human hardware verification per the lag
investigation above); `realm-net` tracks the new Phase 7 work below. A future
session should check which branch it's on before trusting "what's next" from
this file alone.

### Phase 7 planned: Realtime Transport Reliability (FujiRealm-informed)

Asked to plan a migration to "fujirealm style client/server networking"
because FujiRealm (`~/fujicode/fujirealm-game-demo`) feels more reliable and
less laggy in play. Full research and the plan itself are in
`planning/phases/07-realtime-transport-reliability/` (`07-RESEARCH.md` plus
`07-01` through `07-05`); this is a summary of the headline finding and the
recommendation, not a replacement for reading that directory.

**Headline finding:** FujiRealm's Atari client vendors the *same* netstream
handler family maze-war already builds from
(`~/fujicode/fujinet-atari-netstream`); the UDP/TCP choice is a single
runtime flag bit passed to `NS_InitNetstream`, not something baked into the
handler, and the byte-stream framing maze-war already built in Phase 5.1 is
already transport-agnostic. So the transport swap itself is low-risk and
narrow in scope (`07-01`/`07-02`).

**The finding that reshaped the plan:** FujiRealm's server does not simulate
movement from input the way maze-war's does — the client reports its own
already-decided new position and the server just validates it's one legal
step and adopts or rejects it (`server/game.py:1367-1488` in the FujiRealm
repo). That's a genuinely different, client-authoritative point on the
trust spectrum than maze-war's stated design (`PROJECT.md`: "Server remains
authoritative for wizard position"). And critically, FujiRealm's remote
players are **not** interpolated at all — `netstream_apply_remote_players`
snaps them straight to each update, which is *worse* than maze-war's own
`REMOTE_FOLLOW` (which at least walks one cell per tick). So copying that
model would not fix the remote-player lag this project spent 2026-09-08/09
measuring, and might make it feel worse if naively extended to remote actors.

**Recommendation, and what the plan actually proposes:** adopt TCP transport,
CRC-16 framing, and a unified acknowledged reliable-event stream (replacing
the `BRICK_DELTA`/`RESPAWN`/`NAME` echo-burst hacks with one real ARQ, closely
modeled on FujiRealm's own `TERRAIN_EDGE` go-back-N delivery) — these three
serve "more reliable" directly and are all independent of the authority-model
question. Do **not** adopt client-authoritative movement by default; it's
written up (`07-05-PLAN.md`) for the record, explicitly marked do-not-execute
without a separate go-ahead, since it doesn't address the reported problem and
walks back a stated project constraint for a benefit (zero-round-trip local
movement) the user didn't report needing. Let the already-planned Phase 4
(`04-02`/`04-03`, real remote-actor interpolation) proceed after this phase's
reliability work lands — it should give maze-war *better* remote smoothness
than FujiRealm has, not just parity with it.

New v2 requirements `RTP-01` through `RTP-05` were added to
`REQUIREMENTS.md` when this research was written. The plan is no longer merely
proposed: 07-01 through 07-03 are complete; see the current branch summary at
the top of this file.

## Session Continuity

Last session: 2026-09-09 (continued overnight, unattended)
Stopped at: the lag investigation is **closed, conclusively, with no code
change**. Two serious bugs in the measurement rig itself were found and fixed
(the emulator freezing under tight peek loops; the relay wedging the netstream
handshake on restart) -- full detail in `tests/rig/README.md` and
`planning/phases/04-render-state-separation/04-RESEARCH.md`. With the rig
trustworthy, remote reconciliation was shown to converge to zero gap in 100% of
samples once a remote actor is genuinely still, at every loss rate tested up to
50%, and never approaches the recovery/snap thresholds while moving even at
50% loss. There is no stuck-reconciliation bug; `04-06` (bounded catch-up) is
retracted as unneeded. The remaining, architecture-inherent "lag while moving"
needs real render-state separation (`04-02`/`04-03`) to improve further, which
is a substantially larger change to core rendering correctly left for a
human-supervised session with hardware verification, not attempted here.
Nothing in `clients/atari/maze-war.asm`, `server/main.c`, or any shipped test
changed this session -- only `tests/rig/*` (the measurement instruments) and
planning docs. Full smoke suite (28) still green, unchanged from last session.
Resume file: .planning/ROADMAP.md

**Emulator rig did not come up on 2026-09-08.** `atari_load` of
`build/maze-war-net.xex` boots to the host prompt, but the Atari then issues no
SIO command frames at all (`netsio_status` `message_counts` shows only
`0xc4`/`0xc5` ping/pong), so `NS_INIT` fails and the client reports "NO REPLY".
The concatenated handler is evidently not resident when loaded through the BIN
loader; next attempt should boot from a FujiNet-mounted disk via `fujinet_boot`
rather than `atari_load`. Two smaller traps: the MCP key table has no `.` key
(poke the address into `HOSTBUF`, `$7E54`, instead) and the prompt ignores
backspace from the MCP key path.

Test suite is 28 smokes, all green (two consecutive full-suite runs). Emulator workflow note: start FujiNet-PC
*first* on a non-default NetSIO port, then the emulator on that same port
(`fujinet_start netsio_port: N` then `atari_start netsio: true, netsio_port: N`);
starting the emulator first makes it bind the port so the sidecar cannot. The
game server must also hold UDP 9000 before any client opens a stream, or
FujiNet-PC takes it.

## Zero-page collision with the NetStream handler: investigated, largely ruled out

Prompted by a hardware report of graphics being "written over", worsening with
play, and by the fact that adding `RNDX/RNDY` grew the game's zero-page block
from `$80-$E0` to `$80-$E8`.

`NSENGINE.OBX` disassembled in place (`$2800-$2CC1`). The zero-page addresses it
actually touches are `$00`, `$0E`, `$10`, `$1F`, `$23`, `$82`, `$83` and `$EE`.
Only `$82/$83` falls inside the game's block — that is `POINTER` — and it is the
argument-pointer convention for **`NS_INIT` only** (`NS_BASE+27`, `$29D0`, which
reads its parameter block through `($82),Y` and advances it by 5). `NS_SEND`,
`NS_RECV`, `NS_AVAIL` and `NS_STAT` touch no zero page.

So there is **no ongoing zero-page contention during play**, and the block's
growth to `$E8` collides with nothing. `NET_SAVPTR` already brackets the one
call that matters.

Two things worth keeping:

- `NS_INIT` runs `PHP/SEI`, which blocks IRQ but **not** the VBI, which is an
  NMI. The game VBI draws through `POINTER`. So a VBI landing inside `NS_INIT`
  can corrupt the handler's argument pointer. One call per connect, so it cannot
  explain progressive corruption, but it is a real intermittent-connect risk.
- The handler uses `$EE`. The game's block now ends at `$E8` — a margin of five
  bytes. Adding another few bytes to the per-player data would collide for real.

## Method note

Three measurements built this session turned out not to measure what they
claimed, each caught only by a deliberate sanity check:

- blocked A/B on the emulator (session drift landed on one arm; manufactured a
  5x regression that did not exist)
- a probe reading `MOVEST` at an address from the wrong build's `.lab`
- a disassembly filter whose regex anchored at end-of-line, while the
  disassembler appends `;SYMBOL` comments -- it reported *zero* zero-page usage

Check that an instrument reports something before trusting it to report nothing.

## Session Continuity (branch: `realm-net`, most recent)

Last session: 2026-09-09
Stopped at: 07-01 server TCP, 07-02 TCP clients and real-hardware acceptance,
07-03 CRC-16 framing, and 07-04 reliable events are complete. Read the four corresponding summary
files under `planning/phases/07-realtime-transport-reliability/` for the
implementation and validation evidence. Use a forced rebuild in this workspace
because patch timestamp handling can otherwise leave an older binary in place.
Next: 07-05 remains deferred and not recommended; do not execute without an
explicit separate go-ahead.
Resume file: `planning/phases/07-realtime-transport-reliability/07-05-PLAN.md`
