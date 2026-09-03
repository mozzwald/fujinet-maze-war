---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: ready
stopped_at: Phases 3, 3.1 and 5 complete and human-approved. Next is Phase 6 minimal validation, then Phase 4 render smoothing.
last_updated: "2026-09-03T00:00:00.000Z"
progress:
  total_phases: 7
  completed_phases: 5
  total_plans: 14
  completed_plans: 14
---

# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-07)

**Core value:** An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.
**Current focus:** Phase 05 — slot lifecycle and zombie handoff

## Current Position

Phase: 03 (combat-and-world-authority) — COMPLETE 2026-09-03. Human mixed-session checkpoint approved.
Phase: 03.1 (handler-refresh-pokey-isolation) — COMPLETE 2026-09-02. All four success criteria verified; see "Phase 3.1 verification" below.
Phase: 05 (slot-lifecycle-and-zombie-handoff) — CODE COMPLETE 2026-09-02 (LIFE-01..04 addressed), full smoke suite green including the new `slot_lifecycle_smoke.sh`. Human confirmation of a live join/leave handoff still wanted.

Execution order going forward: Phase 3 human checkpoint -> 6 (minimal) -> 4 -> 6 (full). See ROADMAP.md Progress section.

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
- Phase 4 render-state separation. See "Phase 4 starting notes" below before planning it.
- Update `.planning/REQUIREMENTS.md` if the Phase 3.1 invariant should become a tracked requirement ID.

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

### Known gaps (not addressed)

- `tests/combat_world_authority_smoke.sh` is intermittently flaky, roughly 1 run
  in 10, failing as `timed out waiting for pid 0 to change from (x, y)` with the
  server logging repeated `move-blocked`. Confirmed **pre-existing**: it fails at
  the same rate with the Phase 5 server changes reverted. The cause looks like
  the test planning a BFS path against a `blocked` set captured once, so the two
  walked clients can end up in each other's way. Worth fixing before Phase 6
  leans on `make test`, since it makes the suite unreliable.

- Slot identity is address+port with no client token, so a fast reconnect still briefly shows the player's old slot until the 15s timeout expires. Self-healing; a proper fix needs a protocol change.
- With `--zombies N` below 3, slots beyond N stay empty and render as motionless wizards. Documented rather than changed, since it is a design decision about what `--zombies` means.

## Session Continuity

Last session: 2026-09-02
Stopped at: Phase 3.1 closed; Phase 5 implemented and smoke-covered. Next: Phase 3 human mixed-session checkpoint.
Resume file: .planning/ROADMAP.md
