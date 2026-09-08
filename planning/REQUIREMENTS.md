# Requirements: FujiNet Maze War

**Defined:** 2026-04-07
**Core Value:** An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.

## v1 Requirements

### Transport

- [x] **TRAN-01**: Server accepts one canonical client input packet format and normalizes FujiNet NetStream framing before gameplay logic runs.
- [x] **TRAN-02**: Server exposes enough packet/debug counters or logs to distinguish transport-framing problems from gameplay reconciliation problems during mixed-session testing.

### Reconciliation

- [x] **RECN-01**: Server snapshots tell each client which local input sequence has been authoritatively applied for that recipient.
- [x] **RECN-02**: Atari client stores pending local inputs and replays only unacknowledged inputs after applying an authoritative correction.
- [x] **RECN-03**: Atari local wizard movement remains visually smooth under normal play and corrects by no more than one maze cell when reconciliation is required.
- [x] **RECN-04**: Atari client preserves original Maze War movement and turning animation cadence while using client prediction.

### Combat

- [ ] **COMB-01**: Atari fire input sends intent only; projectile origin is derived from authoritative wizard state using one shared server-defined action order.
- [ ] **COMB-02**: Bullets shown on the Atari client always originate from the currently visible wizard position and facing direction.
- [ ] **COMB-03**: Server uses one explicit same-tick ordering contract for turn, move, fire, hit, death, and respawn behavior.
- [ ] **COMB-04**: Linux and Atari clients both follow the same combat ordering contract so identical inputs do not produce different visible shot behavior.

### Rendering

- [ ] **RNDR-01**: Atari client keeps authoritative simulation state, predicted local state, and render-facing state separate so smoothing never mutates gameplay truth directly.
- [ ] **RNDR-02**: Remote wizards and AI zombies are rendered from server snapshots with visual smoothing only and are never locally predicted as if they were the local player.

### Lifecycle

- [ ] **LIFE-01**: Server maintains exactly four wizard slots and fills every unused slot with a server-controlled AI zombie.
- [ ] **LIFE-02**: When a human client joins an occupied zombie slot, the server cleanly transfers control without stale input, ghost shots, stale facing, or inherited transient state.
- [ ] **LIFE-03**: When a human client disconnects or times out, that slot returns to AI zombie control without breaking the running match.
- [ ] **LIFE-04**: Clients can identify current slot ownership and role changes well enough to stay visually and logically in sync through zombie/human handoff.

### World

- [ ] **WRLD-01**: Brick or wall state stays consistent across Atari client, Linux client, and server so movement and line-of-fire checks use the same maze state.
- [ ] **WRLD-02**: Score, death, and respawn state displayed to clients matches server-authoritative gameplay outcomes.

### Validation

- [ ] **VALD-01**: A live session with 1 Atari client, 1 Linux client, and 2 AI zombies runs without movement-desync bugs that block normal play.
- [ ] **VALD-02**: Mixed-session validation includes deterministic checks for move-then-fire, turn-then-fire, zombie replacement, and human disconnect replacement cases.
- [ ] **VALD-03**: The Atari client can be validated through the FujiNet-PC or current FujiNet emulator workflow without requiring protocol changes unique to Linux-only testing.

## v2 Requirements

### AI

- **AI-01**: AI zombies use smarter pathing or combat behavior than the current simple slow baseline.
- **AI-02**: AI difficulty can be tuned by mode or slot.

### Debug

- **DEBG-01**: Project includes observer or replay-oriented debugging tools beyond current protocol logs.
- **DEBG-02**: Linux visual debugging tools provide richer live inspection of authoritative versus predicted state.

### Extras

- **EXTR-01**: Atari client includes radar or map overlay.
- **EXTR-02**: Project supports alternate mazes or an editor pipeline.
- **EXTR-03**: Project supports alternate game modes, teams, or lobby/chat systems.

## Out of Scope

| Feature | Reason |
|---------|--------|
| Smarter zombie AI in this milestone | Zombie quality is explicitly out of scope; zombies only need to preserve slot continuity and basic gameplay coverage. |
| HUD polish unrelated to sync debugging or authoritative state visibility | Presentation improvements do not solve the core movement and combat correctness problems. |
| Expanding beyond the fixed 4-wizard model | The game is expected to run with exactly four slots for this project scope. |
| Requiring all-human validation before declaring progress | The defined acceptance scenario is 1 Atari client, 1 Linux client, and 2 AI zombies. |
| Adding new weapons, power-ups, or other combat systems | New gameplay state would complicate synchronization before the base move/fire loop is trustworthy. |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| TRAN-01 | Phase 1 | Complete |
| TRAN-02 | Phase 1 | Complete |
| RECN-01 | Phase 2 | Complete |
| RECN-02 | Phase 2 | Complete |
| RECN-03 | Phase 2 | Complete |
| RECN-04 | Phase 2 | Complete |
| COMB-01 | Phase 3 | Pending |
| COMB-02 | Phase 3 | Pending |
| COMB-03 | Phase 3 | Pending |
| COMB-04 | Phase 3 | Pending |
| RNDR-01 | Phase 4 | Pending |
| RNDR-02 | Phase 4 | Pending |
| LIFE-01 | Phase 5 | Pending |
| LIFE-02 | Phase 5 | Pending |
| LIFE-03 | Phase 5 | Pending |
| LIFE-04 | Phase 5 | Pending |
| WRLD-01 | Phase 3 | Pending |
| WRLD-02 | Phase 3 | Pending |
| VALD-01 | Phase 6 | Pending |
| VALD-02 | Phase 6 | Pending |
| VALD-03 | Phase 6 | Pending |

**Coverage:**
- v1 requirements: 21 total
- Mapped to phases: 21
- Unmapped: 0

---
*Requirements defined: 2026-04-07*
*Last updated: 2026-04-07 after initial definition*
