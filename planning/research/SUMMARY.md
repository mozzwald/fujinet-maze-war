# Project Research Summary

**Project:** FujiNet Maze War
**Domain:** Atari 8-bit FujiNet networked multiplayer game
**Researched:** 2026-04-07
**Confidence:** HIGH

## Executive Summary

FujiNet Maze War is a server-authoritative multiplayer Maze War adaptation where the hard problem is not basic networking or rendering, but making an Atari 8-bit client feel immediate while remaining faithful to authoritative state during live mixed sessions. The research converges on a conservative, proven architecture: keep the Atari client in MADS assembly, keep the server and Linux debug client in plain C over UDP semantics, use Altirra plus the FujiNet emulator bridge for the inner loop, and treat the Linux client as a protocol twin that improves observability rather than as a separate product surface.

The recommended implementation approach is to stop treating smoothness as a tuning problem and instead fix the contract between client and server. The roadmap should start by normalizing NetStream transport, then add recipient-specific input acknowledgements so the Atari client can replay only unacknowledged local inputs. From there, freeze same-tick turn/move/fire ordering, move all smoothing to the render layer, and formalize zombie/human slot ownership transitions. That sequence directly supports the product goal: smooth Atari movement, trustworthy bullet origin, and a stable 1 Atari + 1 Linux + 2 zombie session.

The major risks are architectural, not speculative. If reconciliation remains threshold-based, bullet origin will continue to drift. If action ordering stays implicit, move-fire and turn-fire cases will remain flaky. If slot handoff lacks an ownership epoch, mixed sessions will leak zombie state into human control. Those risks are manageable, but only if the roadmap enforces the dependency order instead of jumping to HUD polish, smarter AI, or other visible distractions.

## Key Findings

### Recommended Stack

The stack research is strong and consistent with the current repo. Keep the Atari gameplay client in MADS, keep the authoritative server in ISO C/POSIX UDP, keep a Linux C client for fast protocol testing, and standardize the emulator workflow on Altirra plus the current FujiNet firmware PC target and emulator bridge. The critical stack decision is to pin versions for MADS, NetStream handler binaries, and emulator tooling so protocol or handler regressions do not masquerade as gameplay bugs.

**Core technologies:**
- MADS `2.1.5+` pinned: Atari client build toolchain — matches the existing assembly code and avoids a costly assembler migration.
- FujiNet NetStream handler binary pinned: Atari transport boundary — isolates gameplay work from handler regressions.
- FujiNet firmware PC target: emulator-side FujiNet implementation — replaces the deprecated standalone `fujinet-pc` repo as the upstream source of truth.
- Altirra `4.40+`: primary Atari debug environment — best inner-loop option for stepping frame, memory, and timing behavior.
- ISO C / POSIX UDP server in `gcc`/`clang` C17: authoritative simulation — simplest and most inspectable fit for the current multiplayer model.
- Linux C test client with `ncurses`: protocol/debug client — fastest way to validate joins, movement, shots, respawns, and zombie transitions outside the Atari loop.

### Expected Features

The feature research is tightly aligned to the project brief. MVP is not “more Maze War features”; it is authoritative gameplay correctness under mixed-session conditions. The priority cluster is movement reconciliation, shot origin correctness, hit/death/respawn coherence, stable slot management, zombie backfill, clean human takeover, scoreboard accuracy, and consistent brick state.

**Must have (table stakes):**
- Authoritative movement with client-side prediction and bounded reconciliation.
- Shot origin matching the wizard position visibly shown on the Atari client.
- Server-authoritative shot, hit, death, and respawn handling.
- Stable 4-slot mixed sessions with deterministic slot identity and AI zombie backfill.
- Clean human takeover of zombie slots without ghost input, stale shots, or bad role state.
- Accurate score and role visibility from authoritative state.
- Consistent brick or wall state across clients.

**Should have (competitive):**
- Better diagnostics and observer-style tooling for debugging mixed sessions.
- Optional richer Linux visual client if protocol work is already stable.
- Smarter zombie behavior only after sync and combat correctness are proven.

**Defer (v2+):**
- Radar or map overlay.
- Text chat, lobby, or broader social flow.
- Custom mazes or editor pipeline.
- Alternate rulesets, team modes, or large player-count expansion.

### Architecture Approach

The architecture research is the strongest input and should drive planning. The right model is a four-loop system: local prediction for the local actor only, server-authoritative simulation for world state and projectiles, client-side interpolation for remote actors, and server-only zombie AI that emits virtual inputs through the same simulation path as humans. The crucial protocol change is adding recipient-specific `last_applied_input_seq` data to authoritative snapshots so the Atari client can rebuild `predicted_local` from `authoritative_local` plus only unacknowledged inputs.

**Major components:**
1. Atari client input/prediction/reconciliation/render split — immediate local feel without corrupting authoritative simulation state.
2. Authoritative server transport plus simulation split — clean separation between packet normalization, slot ownership, world advancement, and snapshot building.
3. Linux protocol twin and debug tooling — deterministic repro, packet tracing, and state diffing outside Atari constraints.
4. Server-side zombie controller — virtual input generator for empty slots using the same rules as humans.

### Critical Pitfalls

1. **Threshold-based reconciliation instead of ack-based reconciliation** — add per-recipient input acknowledgements, keep a local input ring buffer, and replay only unacknowledged inputs.
2. **Undefined same-tick ordering for turn, move, and fire** — freeze one gameplay contract and make server, Atari prediction, Linux client, and tests all follow it.
3. **Smoothing gameplay state instead of render state** — keep authoritative and predicted simulation state clean; apply visual smoothing only in rendering.
4. **Transport framing drift leaking into gameplay logic** — normalize NetStream framing at the transport edge and instrument malformed, normalized, and stale packet counts.
5. **Slot ownership without an ownership epoch** — reset transient slot state on every human/zombie handoff and expose the transition explicitly to clients.

## Implications for Roadmap

Based on research, suggested phase structure:

### Phase 0: Transport Normalization and Observability
**Rationale:** Reconciliation and combat work are unreliable if NetStream framing issues are still being silently normalized inside gameplay paths.
**Delivers:** Canonical `DELTA` framing, transport-edge normalization, malformed/resync counters, and packet-level observability for Atari and Linux sessions.
**Addresses:** Stable mixed-session networking and the prerequisite for reliable movement sync.
**Avoids:** Treating transport drift as a gameplay problem.

### Phase 1: Protocol and Reconciliation Contract
**Rationale:** This is the root dependency for every visible sync issue. Without input acknowledgement, smoothing remains heuristic and fragile.
**Delivers:** Recipient-specific `last_applied_input_seq` in snapshots, Atari pending-input replay buffer, predicted versus authoritative local state split, and Linux-side verification tooling.
**Addresses:** Smooth authoritative movement, bounded correction, and the foundation for trustworthy shot origin.
**Avoids:** Threshold-based reconciliation and endless retuning of snap thresholds.

### Phase 2: Shot Semantics and Action Ordering
**Rationale:** Once reconciliation is real, combat correctness becomes the next hard requirement. Bullet origin cannot be trusted until turn/move/fire order is explicit and shared.
**Delivers:** Frozen same-tick gameplay ordering, server-owned projectile spawn semantics, deterministic move-fire and turn-fire tests, and explicit shot payload expectations.
**Addresses:** Correct fire, hit, death, and respawn behavior with visible shot origin alignment.
**Uses:** The existing C authoritative server, Linux protocol twin, and Atari predicted-local state model.
**Implements:** Server simulation ordering plus client-side action interpretation.

### Phase 3: Render and Simulation Separation
**Rationale:** After logic correctness is established, visual smoothness should be improved without contaminating simulation state.
**Delivers:** Remote actor interpolation, render-layer-only smoothing, clear state ownership between staged network data and VBI-owned render state, and correction thresholds used only as guard rails.
**Addresses:** Smooth local and remote presentation while preserving original Maze War cadence.
**Avoids:** Smoothing gameplay state and introducing fake positions into bullets, occupancy, or collisions.

### Phase 4: Slot Lifecycle and Zombie/Human Handoff
**Rationale:** The milestone’s validation scenario explicitly depends on AI backfill and human replacement. That makes slot lifecycle a core gameplay phase, not a later polish task.
**Delivers:** Ownership epoch or generation tracking, transient-state reset on handoff, faster test-mode timeout and reconnect semantics, and mixed-session takeover tests.
**Addresses:** Stable 1 Atari + 1 Linux + 2 zombie sessions with clean join, leave, timeout, and takeover behavior.
**Avoids:** Ghost shots, inherited zombie input, stale respawn state, and reconnect ambiguity.

### Phase 5: Mixed-Session Validation and Hardening
**Rationale:** The research consistently defines success as a concrete four-slot validation scenario, not isolated subsystem wins.
**Delivers:** End-to-end validation runs across Altirra/FujiNet bridge and physical FujiNet hardware, packet capture baselines, sanitizer-backed Linux debug builds, and a go/no-go checklist tied to the active requirements.
**Addresses:** MVP acceptance across movement, combat, slot management, and brick/score consistency.
**Avoids:** Declaring success based only on Linux tests or stationary firing cases.

### Phase Ordering Rationale

- Transport normalization comes before protocol semantics because noisy framing invalidates higher-level debugging.
- Reconciliation comes before combat because shot origin depends on the local visible pose being derived from trustworthy predicted versus authoritative state.
- Render smoothing follows logic correctness because smoothing the wrong layer creates new desync classes instead of fixing old ones.
- Slot handoff is isolated as its own phase because it combines transport, simulation, and lifecycle resets, and the mixed-session requirement makes it easy to underestimate.
- Final hardening is separate so emulator-loop success is verified against the actual acceptance scenario and at least one hardware pass.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 0:** Confirm the cleanest canonical NetStream framing path and whether FujiNet UDP sequencing support should be enabled for this handler workflow.
- **Phase 4:** Validate reconnect and ownership-epoch semantics against current protocol constraints so takeover behavior is explicit rather than inferred.
- **Phase 5:** Confirm the exact Altirra plus FujiNet firmware PC target and bridge setup to standardize reproducible mixed-session test runs.

Phases with standard patterns (skip research-phase):
- **Phase 1:** Ack-based client prediction and server reconciliation are well-documented and already strongly mapped to this repo.
- **Phase 2:** Same-tick action ordering and server-owned projectile authority are straightforward once the protocol contract is fixed.
- **Phase 3:** Render-versus-simulation separation follows established authoritative networking patterns and existing repo structure.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Strongly grounded in current repo constraints plus official or primary upstream tooling sources; the main open item is exact version pinning. |
| Features | MEDIUM | Feature scope is clear and well aligned to the project brief, but some historical references are secondary and mainly useful for prioritization, not implementation detail. |
| Architecture | HIGH | The architecture recommendation is cohesive, specific to the repo, and backed by established authoritative-networking patterns plus direct code/protocol inspection. |
| Pitfalls | HIGH | Pitfalls map directly to known code paths, current symptoms, and established networking failure modes; they are actionable and phase-specific. |

**Overall confidence:** HIGH

### Gaps to Address

- Exact wire-format update for snapshot acknowledgements: define the byte layout and compatibility plan during Phase 1 planning.
- Precise same-tick action contract: freeze server/client order in one spec and capture deterministic fixtures before Phase 2 implementation.
- Ownership epoch encoding: decide whether the epoch lives in snapshots, explicit events, or both before Phase 4 implementation.
- Emulator and hardware validation matrix: pin one known-good Altirra, FujiNet firmware PC target, bridge setup, and hardware check path before Phase 5 execution.

## Sources

### Primary (HIGH confidence)
- [.planning/research/STACK.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/research/STACK.md) — stack recommendations and versioning strategy.
- [.planning/research/FEATURES.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/research/FEATURES.md) — MVP feature prioritization and deferrals.
- [.planning/research/ARCHITECTURE.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/research/ARCHITECTURE.md) — architecture, packet semantics, and component boundaries.
- [.planning/research/PITFALLS.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/research/PITFALLS.md) — failure modes, prevention strategies, and phase mapping.
- [.planning/PROJECT.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/PROJECT.md) — project brief and active requirements.
- [doc/protocol.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md) — current wire protocol and slot semantics.
- [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c) — current authoritative server behavior and slot logic.
- [clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) — current Atari prediction, staging, and reconciliation behavior.

### Secondary (MEDIUM confidence)
- https://mads.atari8.info/mad-assembler-mkdocs/en/ — MADS toolchain guidance.
- https://raw.githubusercontent.com/FujiNetWIFI/fujinet-firmware/master/README.md — current FujiNet upstream workflow.
- https://raw.githubusercontent.com/FujiNetWIFI/fujinet-pc/master/README.md — confirmation that standalone `fujinet-pc` is deprecated.
- https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html — authoritative networking reference for input replay and acknowledgement.
- https://www.gabrielgambetta.com/entity-interpolation.html — remote-actor interpolation patterns.
- https://gafferongames.com/post/snapshot_interpolation/ — snapshot smoothing guidance.
- https://gafferongames.com/post/state_synchronization/ — simulation and state sync guidance.

### Tertiary (LOW confidence)
- https://en.wikipedia.org/wiki/Maze_War — historical feature precedent only.
- https://en.wikipedia.org/wiki/MIDI_Maze — historical feature precedent only.
- https://strategywiki.org/wiki/Faceball_2000/Gameplay — historical feature precedent only.

---
*Research completed: 2026-04-07*
*Ready for roadmap: yes*
