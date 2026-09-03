# Roadmap: FujiNet Maze War

## Overview

This roadmap follows the dependency chain identified in research: normalize transport first, lock the reconciliation contract next, freeze combat semantics before smoothing presentation, then harden slot lifecycle and validate the full mixed-session target of 1 Atari client, 1 Linux client, and 2 AI zombies. Each phase delivers one coherent capability that can be planned and verified independently.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Transport Normalization and Observability** - Canonicalize packet ingress so transport bugs stop masquerading as gameplay bugs.
- [x] **Phase 2: Reconciliation Contract** - Add acknowledged-input reconciliation so Atari movement can stay smooth and bounded. Final real-Atari verification approved the cadence/fire-direction replay fix and closed RECN-01 through RECN-04.
- [x] **Phase 3: Combat and World Authority** - Freeze action ordering and authoritative world outcomes so firing behaves identically across clients. Human mixed-session checkpoint approved 2026-09-03.
- [x] **Phase 3.1: Netstream Handler Refresh and POKEY Channel Isolation (INSERTED)** - Update to the latest netstream handler (built from source) and remap all sound to POKEY channels 1+2 so the game can never corrupt the handler's channel 3+4 baud timer. See `ref/net-fix-plan.md` for full analysis. Completed 2026-09-02; all four criteria verified (see STATE.md).
- [ ] **Phase 4: Render-State Separation** - Keep render smoothing isolated from gameplay truth for local and remote actors. Reordered after Phase 5: presentation polish, needed for release but not for reliable play.
- [x] **Phase 5: Slot Lifecycle and Zombie Handoff** - Make four-slot zombie backfill and human takeover stable through joins and disconnects. Reordered ahead of Phase 4: correctness work (ghost shots, stale facing, inherited state) that blocks reliable play. Code complete 2026-09-02; human confirmation of a live handoff still wanted.
- [ ] **Phase 6: Mixed-Session Validation and Hardening** - Prove the acceptance scenario in the real Atari/FujiNet validation workflow. A minimal validation pass (scripted emulator sessions including join/leave handoff) runs after Phase 5; full hardening runs after Phase 4.

## Phase Details

### Phase 1: Transport Normalization and Observability
**Goal**: Clients reach gameplay simulation through one canonical transport path, with enough observability to separate framing faults from sync faults.
**Depends on**: Nothing (first phase)
**Requirements**: TRAN-01, TRAN-02
**Success Criteria** (what must be TRUE):
  1. An Atari client and Linux client can both send gameplay input through the same canonical packet path without startup or join failures caused by NetStream framing differences.
  2. A tester can inspect counters or logs and tell whether a mixed-session failure came from malformed or normalized transport input versus gameplay reconciliation.
  3. Mixed-session debugging no longer requires guessing whether packet framing drift or gameplay state drift caused the visible issue.
**Plans**: 2 plans
Plans:
- [x] `01-01-PLAN.md` — Create Wave 0 transport smoke coverage and move DELTA framing normalization behind a canonical server boundary.
- [x] `01-02-PLAN.md` — Add transport counters, debug summaries, and transport contract documentation on top of the canonical ingress path.

### Phase 2: Reconciliation Contract
**Goal**: The Atari client predicts locally, replays only unacknowledged inputs, and stays closely aligned with authoritative movement and maze collisions.
**Depends on**: Phase 1
**Requirements**: RECN-01, RECN-02, RECN-03, RECN-04
**Success Criteria** (what must be TRUE):
  1. The local Atari wizard moves immediately on input and remains visually smooth during normal play while still converging back to server truth.
  2. When a correction is needed, the Atari wizard settles back to authoritative state without large teleports and with no more than about one maze cell of visible correction in normal play.
  3. Original Maze War movement and turning cadence remains recognizable on Atari while client prediction is active.
  4. Movement disagreements caused by stale local input are resolved by replaying only unacknowledged inputs instead of repeated snap-threshold retuning.
**Plans**: 5 plans
Plans:
- [x] `02-01-PLAN.md` — Extend SNAPSHOT with recipient-specific `ack_seq`, update Linux ack decoding, and add a real-server ack smoke harness.
- [x] `02-02-PLAN.md` — Add the Atari pending-input ring, ack discard helpers, and replay wiring at the staged authoritative commit seam.
- [x] `02-03-PLAN.md` — Replace threshold-only local correction with bounded ack-driven replay, then verify correction size and cadence on real Atari/FujiNet play. Real Atari verification failed due to player-movement screen artifacts.
- [x] `02-04-PLAN.md` — Fix Atari replay/correction stale-draw cleanup and repeat the blocked real-hardware movement approval for RECN-03 and RECN-04. Ghosting is fixed, but the retry still failed on cadence feel and a fire-plus-direction jump/snap-back while firing.
- [x] `02-05-PLAN.md` — Tighten Atari local input/replay cadence handling so trigger-bearing directional inputs do not replay as spurious movement, then repeat the real-hardware cadence/fire checkpoint. Approved on real Atari/FujiNet.

### Phase 3: Combat and World Authority
**Goal**: Turn, move, fire, hit, death, respawn, and shared world checks follow one explicit authoritative contract across server, Atari, and Linux clients.
**Depends on**: Phase 2
**Requirements**: COMB-01, COMB-02, COMB-03, COMB-04, WRLD-01, WRLD-02
**Success Criteria** (what must be TRUE):
  1. Bullets shown on the Atari client always originate from the wizard position and facing direction currently visible to the player.
  2. Repeating the same move-then-fire or turn-then-fire input sequence on Atari and Linux produces the same visible shot behavior and gameplay outcome.
  3. Hits, deaths, respawns, and score changes visible on clients match the server-authoritative outcome rather than diverging by client.
  4. Wall and brick interactions used for movement and line-of-fire checks stay consistent across Atari, Linux, and server simulation.
**Plans**: 3 plans
Plans:
- [x] `03-01-PLAN.md` — Freeze the authoritative same-tick combat/world contract on the server, document it in the protocol, and add server smoke coverage for ordering plus world outcomes.
- [x] `03-02-PLAN.md` — Align Atari shot/respawn/score consumption to the visible authoritative actor state so bullet origin and death/respawn presentation stay server-true.
- [x] `03-03-PLAN.md` — Align Linux combat/world packet interpretation, extend parity coverage, and finish with the mixed Atari/Linux approval checkpoint. Code complete, smokes green; the human mixed-session approval checkpoint is still pending and is deferred until Phase 3.1 lands, since the channel 3/4 baud-timer corruption fixed there may explain earlier hardware symptoms.

### Phase 3.1: Netstream Handler Refresh and POKEY Channel Isolation (INSERTED)
**Goal**: The game runs the latest netstream handler built from source, and no game code can disturb the handler's POKEY serial configuration (channels 3+4 joined baud timer, AUDCTL, SKCTL) after netstream init.
**Depends on**: Phase 3 code (inserted before the Phase 3 human checkpoint)
**Requirements**: Supports COMB-01..04 and VALD-03 indirectly (transport integrity under sound activity)
**Reference**: `ref/net-fix-plan.md` (full exploration findings, site-by-site remap list, memory-map analysis)
**Success Criteria** (what must be TRUE):
  1. `build/maze-war-net.xex` embeds a handler built from `../fujinet-atari-netstream` source (path overridable via `NETSTREAM_DIR`), including the `NS_InitNetstream` c_sp-leak fix and the RX IRQ overrun/error-latch fix; the checked-in `NSENGINE.OBX` fallback is refreshed to match.
  2. Netstream clock config remains TX external / RX internal (`NET_FLAGS = $04` — verified already correct).
  3. After `NS_INIT`, the game never writes AUDF3/AUDF4/AUDC3/AUDC4/AUDCTL/SKCTL. In particular `MOVSND` (the live bug: walk sound for slots 2/3 writes the handler's AUDF3/AUDF4 baud divisor every frame) routes through a channel 1+2 allocator: local player owns channel 1, remotes share channel 2 last-writer-wins with owner-checked silencing.
  4. During a soak with zombies walking in slots 2/3, POKEY shows AUDCTL=$28 and handler-programmed AUDF3/AUDF4 throughout, and the new sticky `NS_GetStatus` error byte stays clear.
**Plans**: 2 plans
Plans:
- [x] `03.1-01` — Build the handler from the netstream source repo in the game Makefile (with checked-in OBX fallback), refresh `NSENGINE.OBX`, and add minimal `NS_GetStatus` error-latch observability to `NET_POLL`. Implemented 2026-07-19. Verified 2026-09-02: the from-source rule reproduces the checked-in OBX byte for byte.
- [x] `03.1-02` — Remap all sound sites to a channel 1+2 priority allocator (`SND_SEL`/`SND_OFF`), guard the cold-start AUDCTL/SKCTL writes and restart-path AUDC3/AUDC4 clears, and add `sound_channel_guard_smoke.sh`. Implemented 2026-07-19. Verified 2026-09-02 in an emulator session covering movement, firing and a full role handoff: AUDCTL=$28, AUDF3/AUDF4 handler-programmed throughout, NET_NS_ERRS=$00.

### Phase 4: Render-State Separation
**Goal**: Authoritative state, predicted local state, and render-facing state are cleanly separated so smoothing improves presentation without mutating simulation truth.
**Depends on**: Phase 5 (reordered — presentation polish for release, after correctness work)
**Requirements**: RNDR-01, RNDR-02
**Success Criteria** (what must be TRUE):
  1. Remote wizards and AI zombies move smoothly on the Atari display without being treated as locally predicted actors.
  2. Local smoothing and remote interpolation never create fake gameplay positions that alter bullets, collision checks, or slot state.
  3. The Atari client can present smoother actor motion while keeping authoritative simulation state and predicted-local simulation state inspectably distinct.
**Plans**: TBD

### Phase 5: Slot Lifecycle and Zombie Handoff
**Goal**: The match always maintains four valid wizard slots, with clean zombie backfill and clean human takeover or disconnect recovery.
**Depends on**: Phase 3 (including the 3.1 insertion and the Phase 3 human checkpoint; reordered ahead of Phase 4 as correctness work)
**Requirements**: LIFE-01, LIFE-02, LIFE-03, LIFE-04
**Success Criteria** (what must be TRUE):
  1. A running match always shows exactly four active slots, with unused slots controlled by server AI zombies.
  2. When a human joins a zombie-filled slot, control transfers cleanly without ghost shots, stale movement, stale facing, or inherited transient state.
  3. When a human disconnects or times out, the slot returns to AI zombie control without breaking the match for remaining players.
  4. Clients can visibly track who owns each slot and remain in sync through zombie-to-human and human-to-zombie role changes.
**Plans**: 1 plan
Plans:
- [x] `05-01` — Reset per-slot gameplay state on both handoff directions server-side, shorten the client timeout so a reconnecting player is not stranded beside their own ghost, clear the matching per-slot latches on the Atari client when a role or local pid changes, document the slot contract, and cover it with `slot_lifecycle_smoke.sh`. Implemented 2026-09-02.

**Status note**: LIFE-02/03/04 are addressed. LIFE-01 is satisfied at `--zombies 3`; lower values intentionally leave seats open for more humans, and an empty seat still renders as a motionless wizard. That is documented in `doc/protocol.md` rather than changed, since it is a decision about what `--zombies` means.

### Phase 6: Mixed-Session Validation and Hardening
**Goal**: The target live session is repeatably validated in the supported Atari/FujiNet workflow with deterministic checks for the known failure cases.
**Depends on**: Phase 5 (minimal validation pass); Phase 4 (full hardening for release)
**Requirements**: VALD-01, VALD-02, VALD-03
**Success Criteria** (what must be TRUE):
  1. A live session with 1 Atari client, 1 Linux client, and 2 AI zombies runs without movement-desync bugs that block normal play.
  2. Deterministic validation covers move-then-fire, turn-then-fire, zombie replacement, and human disconnect replacement cases and can be rerun after changes.
  3. The project can be validated through the current FujiNet-PC or FujiNet emulator workflow without needing Linux-only protocol shortcuts.
**Plans**: TBD

## Traceability

| Requirement | Phase |
|-------------|-------|
| TRAN-01 | Phase 1 |
| TRAN-02 | Phase 1 |
| RECN-01 | Phase 2 |
| RECN-02 | Phase 2 |
| RECN-03 | Phase 2 |
| RECN-04 | Phase 2 |
| COMB-01 | Phase 3 |
| COMB-02 | Phase 3 |
| COMB-03 | Phase 3 |
| COMB-04 | Phase 3 |
| WRLD-01 | Phase 3 |
| WRLD-02 | Phase 3 |
| RNDR-01 | Phase 4 |
| RNDR-02 | Phase 4 |
| LIFE-01 | Phase 5 |
| LIFE-02 | Phase 5 |
| LIFE-03 | Phase 5 |
| LIFE-04 | Phase 5 |
| VALD-01 | Phase 6 |
| VALD-02 | Phase 6 |
| VALD-03 | Phase 6 |

**Coverage:**
- v1 requirements: 21 total
- Mapped to phases: 21
- Unmapped: 0

## Progress

**Execution Order:**
1 -> 2 -> 3 (code) -> 3.1 (INSERTED) -> 5 -> 3 human checkpoint -> 6 (minimal validation) -> 4 -> 6 (full hardening)

Phases 1, 2, 3, 3.1 and 5 are done. Next: Phase 6 minimal validation, then Phase 4.

Phase 5 was brought forward ahead of the Phase 3 human checkpoint: both need the same
mixed session to verify, and running the checkpoint before the slot work would have
meant running it twice.

"Reliably playable" is reached after the minimal Phase 6 validation pass; Phase 4 and full
Phase 6 hardening (plus FujiNet Lobby integration, out of roadmap scope for v1) make it
"Lobby-releasable".

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Transport Normalization and Observability | 2/2 | Complete | 2026-04-08 |
| 2. Reconciliation Contract | 5/5 | Complete | 2026-04-08 |
| 3. Combat and World Authority | 3/3 | Complete | 2026-09-03 |
| 3.1 Netstream Handler Refresh and POKEY Channel Isolation | 2/2 | Complete | 2026-09-02 |
| 5. Slot Lifecycle and Zombie Handoff | 1/1 | Code complete; human handoff confirmation wanted | 2026-09-02 |
| 4. Render-State Separation | 0/TBD | Not started | - |
| 6. Mixed-Session Validation and Hardening | 0/TBD | Not started | - |
