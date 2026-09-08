---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: ready
stopped_at: HUD seat occupancy (0x44 SEATS) landed 2026-09-08, awaiting the human Atari check. Phases 3, 3.1, 5 and 5.1 complete and human-approved. Phase 4 render-state separation is next.
last_updated: "2026-09-08T00:00:00.000Z"
progress:
  total_phases: 8
  completed_phases: 6
  total_plans: 15
  completed_plans: 15
---

# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-07)

**Core value:** An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.
**Current focus:** Phase 05 — slot lifecycle and zombie handoff

## Current Position

Phase: 03 (combat-and-world-authority) — COMPLETE 2026-09-03. Human mixed-session checkpoint approved.
Phase: 03.1 (handler-refresh-pokey-isolation) — COMPLETE 2026-09-02. All four success criteria verified; see "Phase 3.1 verification" below.
Phase: 05 (slot-lifecycle-and-zombie-handoff) — COMPLETE. Code 2026-09-02 (LIFE-01..04 addressed), smoke suite green including `slot_lifecycle_smoke.sh`. Human confirmation of live join/leave handoff received 2026-09-04; human testing continues alongside each change from here.
Phase: 05.1 (link-integrity-and-frame-resync) — COMPLETE 2026-09-04. Unplanned, driven by real-hardware symptoms. See "Phase 5.1" below.

Execution order going forward: 4 -> 6 (minimal) -> 6 (full). Phases 3, 3.1, 5 and 5.1 are closed and human-approved.

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

- Phase 4 render-state separation. See "Phase 4 starting notes" and "Phase 4 investigation log" below; both were written before the 5.1 work and are partly superseded — read the 5.1 section first.
- Phase 6 minimal validation: scripted emulator sessions including join/leave handoff.
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
- `tests/seat_occupancy_smoke.sh` (new) asserts the mask live against the real
  server: broadcast on join without waiting for the repeat timer, zombie slots
  excluded, a second client added, and the repeat itself.

Note observed while writing the test: with `--zombies 2` the second client
takes slot **3**, not slot 1. `find_or_add_client` prefers a free non-zombie
slot over displacing the AI, so `--zombies N` shrinks only once the free slots
run out.

Not yet verified on the Atari: the emulator rig could not be brought up this
session (see Session Continuity).

### Known gaps (not addressed)

- ~~`combat_world_authority_smoke.sh` flakiness~~ FIXED 2026-09-03. Two causes,
  both artefacts of the server reading one joy per tick: a direction or a fire
  sent exactly once could be overwritten by a neighbouring neutral before the
  tick read it (the tests run at `--tick-hz 4`, a 250ms window), and the BFS
  `blocked` set was captured once at startup while actors kept moving. The
  walks now hold the direction and hold the trigger the way a player does, and
  occupancy is read live at every plan. 20 consecutive green runs.

- Slot identity is address+port with no client token, so a fast reconnect still briefly shows the player's old slot until the 15s timeout expires. Self-healing; a proper fix needs a protocol change.
- With `--zombies N` below 3, slots beyond N stay empty and still render as motionless wizards **on the board**. They are no longer listed in the HUD (see "Seat occupancy" above). Left as-is on the board because it is a design decision about what `--zombies` means.

## Session Continuity

Last session: 2026-09-08
Stopped at: HUD seat occupancy (`0x44 SEATS`) implemented on the server, the
Atari client and both Linux clients; full smoke suite green. Awaiting the human
Atari check. Next: Phase 4 render-state separation.
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

Test suite is 23 smokes, all green. Emulator workflow note: start FujiNet-PC
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
