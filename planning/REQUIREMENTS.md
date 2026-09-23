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

- [x] **COMB-01**: Atari fire input sends intent only; projectile origin is derived from authoritative wizard state using one shared server-defined action order.
- [x] **COMB-02**: Bullets shown on the Atari client always originate from the currently visible wizard position and facing direction.
- [x] **COMB-03**: Server uses one explicit same-tick ordering contract for turn, move, fire, hit, death, and respawn behavior.
- [x] **COMB-04**: Linux and Atari clients both follow the same combat ordering contract so identical inputs do not produce different visible shot behavior.

### Rendering

- [x] **RNDR-01**: Atari client keeps authoritative simulation state, predicted local state, and render-facing state separate so smoothing never mutates gameplay truth directly.
- [x] **RNDR-02**: Remote wizards and AI zombies are rendered from server snapshots with visual smoothing only and are never locally predicted as if they were the local player.

### Lifecycle

- [x] **LIFE-01**: Server maintains four wizard slots and fills up to the configured 0..3 unused slots with server-controlled AI Zombies.
- [x] **LIFE-02**: When a human client joins an occupied zombie slot, the server cleanly transfers control without stale input, ghost shots, stale facing, or inherited transient state.
- [x] **LIFE-03**: When a human client disconnects or times out, that slot returns to AI zombie control when configured without breaking the running match.
- [x] **LIFE-04**: Clients can identify current slot ownership and role changes well enough to stay visually and logically in sync through zombie/human handoff.

### World

- [x] **WRLD-01**: Brick or wall state stays consistent across Atari client, Linux client, and server so movement and line-of-fire checks use the same maze state.
- [x] **WRLD-02**: Score, death, and respawn state displayed to clients matches server-authoritative gameplay outcomes.

### Validation

- [x] **VALD-01**: A live session with 1 Atari client, 1 Linux client, and 2 AI zombies runs without movement-desync bugs that block normal play.
- [x] **VALD-02**: Mixed-session validation includes deterministic checks for move-then-fire, turn-then-fire, zombie replacement, and human disconnect replacement cases.
- [x] **VALD-03**: The Atari client can be validated through the FujiNet-PC or current FujiNet emulator workflow without requiring protocol changes unique to Linux-only testing.

## v2 Requirements

### Realtime Transport (FujiRealm-informed, Phase 7, branch `realm-net`)

- [x] **RTP-01**: The server accepts realtime connections over TCP instead of a single shared UDP socket, with no change to game tick rate or packet payload semantics.
- [x] **RTP-02**: The Atari client and both Linux clients connect over TCP using the same vendored netstream handler and framing, with transport selected by a flag bit rather than a handler rebuild; physical Atari/FujiNet acceptance is recorded.
- [x] **RTP-03**: Every server-to-client and client-to-server frame is protected by CRC-16/CCITT-FALSE instead of a one-byte additive sum, catching corruption patterns the sum could miss, without weakening the existing one-frame-cost-of-corruption resync property.
- [x] **RTP-04**: Brick-destroyed, respawn, and name-change events are delivered through one ordered, cumulatively-acknowledged reliable stream instead of three separate fixed-repeat-count echo mechanisms, recovering a single lost event within one retransmit interval rather than the multi-second full-resync window.
- [x] **RTP-05**: The trade-off of adopting client-authoritative local movement (as FujiRealm does) is documented with a clear recommendation, and is intentionally not implemented without an explicit, separate decision to do so.

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

### Rounds, rooms, and FujiNet Lobby (Phase 8)

- [x] **MEM-01**: Atari removes the unreachable blocking game-over routine and unreachable old title assets, reuses their measured space for the new presentation/UI, and enforces code/data/display/zero-page headroom in the memory-layout test.
- [x] **ROOM-01**: One server process runs independently configured four-seat rooms on distinct TCP ports with no gameplay, reliable-event, brick, score, timer, or client-state leakage.
- [x] **ROOM-02**: Each room enforces configured Zombie capacity and human priority without transferring scores or transient state between occupants.
- [x] **ROND-01**: The server alone selects the first player to reach a validated kill limit and broadcasts one idempotent frozen round result, keeping final occupant roles separate from historical Zombie participation.
- [x] **ROND-02**: A new round resets scores, bricks, spawns, shots, respawns, input queues, timers, and round-local effects before authoritative play resumes. Versioned round identity and a recoverable authorization/map/snapshot barrier prevent stale events or lost reset frames from reopening old state; intermission preserves both endpoint watchdogs.
- [x] **ROND-03**: Atari presents loser vaporization, winner animation/vaporization, fade, and frozen results without disabling VBI, stopping network service, enabling DLI, or locally restarting the round.
- [x] **GRCE-01**: Voluntary leave releases a seat immediately, while unexpected loss of the final human preserves the room for a bounded grace period independent of round state.
- [x] **CONF-01**: Public host, TCP room range, default port, Lobby base, appkey, and kill limit are validated build inputs that regenerate Atari constants/data.
- [x] **APKY-01**: Atari owns player identity under Maze War creator `$3022` and application `$03`; it may read the shared Lobby username only as a bounded compatibility fallback.
- [x] **APKY-02**: Atari reads/writes the Maze War selected-room AppKey only under `$3022/$03`, strictly validates its TCP URL against the build's public host/range, and reads Lobby key `$03` only as a fallback.
- [x] **LOBY-01**: Lobby publication is opt-in, asynchronous to the 10 Hz simulation, publishes every room separately with human-only occupancy, and has bounded refresh/retry/shutdown behavior.
- [x] **LOBY-02**: Atari queries the QA Lobby using the pinned binary schema, parses records within a bounded reusable buffer, and displays only validated Maze War rooms.
- [x] **SWCH-01**: Atari voluntarily leaves, explicitly stops NetStream, verifies firmware connection cleanup, clears all connection/round state through one reusable reset path, and repeatedly joins the same or another room without stale state or a reboot.
- [ ] **PROD-01**: Production publication occurs only after QA lifecycle, external launch, repeated switching, and physical Atari/FujiNet checkpoints pass against the release candidate.

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
| COMB-01 | Phase 3 | Complete |
| COMB-02 | Phase 3 | Complete |
| COMB-03 | Phase 3 | Complete |
| COMB-04 | Phase 3 | Complete |
| RNDR-01 | Phase 4 | Complete |
| RNDR-02 | Phase 4 | Complete |
| LIFE-01 | Phase 5 | Complete |
| LIFE-02 | Phase 5 | Complete |
| LIFE-03 | Phase 5 | Complete |
| LIFE-04 | Phase 5 | Complete |
| WRLD-01 | Phase 3 | Complete |
| WRLD-02 | Phase 3 | Complete |
| VALD-01 | Phase 6 | Complete |
| VALD-02 | Phase 6 | Complete |
| VALD-03 | Phase 6 | Complete |
| MEM-01 | Phase 8 | Complete |
| ROOM-01 | Phase 8 | Complete |
| ROOM-02 | Phase 8 | Complete |
| ROND-01 | Phase 8 | Complete |
| ROND-02 | Phase 8 | Complete |
| ROND-03 | Phase 8 | Complete |
| GRCE-01 | Phase 8 | Complete |
| CONF-01 | Phase 8 | Complete |
| APKY-01 | Phase 8 | Complete |
| APKY-02 | Phase 8 | Complete |
| LOBY-01 | Phase 8 | Complete |
| LOBY-02 | Phase 8 | Complete |
| SWCH-01 | Phase 8 | Complete |
| PROD-01 | Phase 8 | Pending |

**Coverage:**
- v1 requirements: 21 total
- Mapped to phases: 21
- Unmapped: 0

---
*Requirements defined: 2026-04-07*
*Last updated: 2026-09-13 for accepted Phase 08-09 QA Lobby room browser*
