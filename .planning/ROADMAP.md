# Roadmap: FujiNet Maze War

## Overview

This roadmap follows the dependency chain identified in research: normalize transport first, lock the reconciliation contract next, freeze combat semantics before smoothing presentation, then harden slot lifecycle and validate the full mixed-session target of 1 Atari client, 1 Linux client, and 2 AI zombies. Each phase delivers one coherent capability that can be planned and verified independently.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Transport Normalization and Observability** - Canonicalize packet ingress so transport bugs stop masquerading as gameplay bugs.
- [ ] **Phase 2: Reconciliation Contract** - Add acknowledged-input reconciliation so Atari movement can stay smooth and bounded. All four plans have executed, but real Atari approval remains blocked by fast-feeling cadence and a fire-plus-direction jump regression after the ghosting fix.
- [ ] **Phase 3: Combat and World Authority** - Freeze action ordering and authoritative world outcomes so firing behaves identically across clients.
- [ ] **Phase 4: Render-State Separation** - Keep render smoothing isolated from gameplay truth for local and remote actors.
- [ ] **Phase 5: Slot Lifecycle and Zombie Handoff** - Make four-slot zombie backfill and human takeover stable through joins and disconnects.
- [ ] **Phase 6: Mixed-Session Validation and Hardening** - Prove the acceptance scenario in the real Atari/FujiNet validation workflow.

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
**Plans**: 4 plans
Plans:
- [x] `02-01-PLAN.md` — Extend SNAPSHOT with recipient-specific `ack_seq`, update Linux ack decoding, and add a real-server ack smoke harness.
- [x] `02-02-PLAN.md` — Add the Atari pending-input ring, ack discard helpers, and replay wiring at the staged authoritative commit seam.
- [x] `02-03-PLAN.md` — Replace threshold-only local correction with bounded ack-driven replay, then verify correction size and cadence on real Atari/FujiNet play. Real Atari verification failed due to player-movement screen artifacts.
- [x] `02-04-PLAN.md` — Fix Atari replay/correction stale-draw cleanup and repeat the blocked real-hardware movement approval for RECN-03 and RECN-04. Ghosting is fixed, but the retry still failed on cadence feel and a fire-plus-direction jump/snap-back while firing.

### Phase 3: Combat and World Authority
**Goal**: Turn, move, fire, hit, death, respawn, and shared world checks follow one explicit authoritative contract across server, Atari, and Linux clients.
**Depends on**: Phase 2
**Requirements**: COMB-01, COMB-02, COMB-03, COMB-04, WRLD-01, WRLD-02
**Success Criteria** (what must be TRUE):
  1. Bullets shown on the Atari client always originate from the wizard position and facing direction currently visible to the player.
  2. Repeating the same move-then-fire or turn-then-fire input sequence on Atari and Linux produces the same visible shot behavior and gameplay outcome.
  3. Hits, deaths, respawns, and score changes visible on clients match the server-authoritative outcome rather than diverging by client.
  4. Wall and brick interactions used for movement and line-of-fire checks stay consistent across Atari, Linux, and server simulation.
**Plans**: TBD

### Phase 4: Render-State Separation
**Goal**: Authoritative state, predicted local state, and render-facing state are cleanly separated so smoothing improves presentation without mutating simulation truth.
**Depends on**: Phase 3
**Requirements**: RNDR-01, RNDR-02
**Success Criteria** (what must be TRUE):
  1. Remote wizards and AI zombies move smoothly on the Atari display without being treated as locally predicted actors.
  2. Local smoothing and remote interpolation never create fake gameplay positions that alter bullets, collision checks, or slot state.
  3. The Atari client can present smoother actor motion while keeping authoritative simulation state and predicted-local simulation state inspectably distinct.
**Plans**: TBD

### Phase 5: Slot Lifecycle and Zombie Handoff
**Goal**: The match always maintains four valid wizard slots, with clean zombie backfill and clean human takeover or disconnect recovery.
**Depends on**: Phase 4
**Requirements**: LIFE-01, LIFE-02, LIFE-03, LIFE-04
**Success Criteria** (what must be TRUE):
  1. A running match always shows exactly four active slots, with unused slots controlled by server AI zombies.
  2. When a human joins a zombie-filled slot, control transfers cleanly without ghost shots, stale movement, stale facing, or inherited transient state.
  3. When a human disconnects or times out, the slot returns to AI zombie control without breaking the match for remaining players.
  4. Clients can visibly track who owns each slot and remain in sync through zombie-to-human and human-to-zombie role changes.
**Plans**: TBD

### Phase 6: Mixed-Session Validation and Hardening
**Goal**: The target live session is repeatably validated in the supported Atari/FujiNet workflow with deterministic checks for the known failure cases.
**Depends on**: Phase 5
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
Phases execute in numeric order: 1 -> 2 -> 3 -> 4 -> 5 -> 6

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Transport Normalization and Observability | 2/2 | Complete | 2026-04-08 |
| 2. Reconciliation Contract | 4/4 | Blocked | - |
| 3. Combat and World Authority | 0/TBD | Not started | - |
| 4. Render-State Separation | 0/TBD | Not started | - |
| 5. Slot Lifecycle and Zombie Handoff | 0/TBD | Not started | - |
| 6. Mixed-Session Validation and Hardening | 0/TBD | Not started | - |
