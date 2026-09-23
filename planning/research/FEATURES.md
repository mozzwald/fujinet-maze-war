# Feature Research

**Domain:** Atari 8-bit FujiNet networked Maze War
**Researched:** 2026-04-07
**Confidence:** MEDIUM

## Feature Landscape

### Table Stakes (Core Gameplay Correctness)

These are the features this repo needs for the near-term milestone. They are table stakes not because every Maze War clone ships them all at once, but because this project's stated value is smooth Atari play under server authority.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Authoritative movement with client-side smoothing/prediction | The repo's core promise is that the local Atari wizard feels smooth without diverging from server truth. A networked Maze War that visibly teleports or rubber-bands feels broken. | HIGH | Must preserve original step/turn animation feel while reconciling to server snapshots. Corrections should normally stay within about one cell. |
| Shot origin matches visible wizard position | In Maze War style combat, shots are simple, immediate, and positional. If bullets leave from a different row/column than the wizard the player sees, combat is untrustworthy. | HIGH | Depends on movement reconciliation being stable at fire time. Treat this as a correctness feature, not polish. |
| Server-authoritative shots, hits, death, and respawn | Canonical Maze/Maze War play is built around shooting, scoring, being hit, and reappearing elsewhere. The current protocol already models `SHOT`, `RESPAWN`, and authoritative score state. | MEDIUM | Death/respawn timing should be consistent across Atari, Linux, and zombies. Respawn invulnerability or grace period can stay minimal if state is coherent. |
| Stable mixed-session slot management | The milestone explicitly targets 1 Atari client, 1 Linux client, and 2 AI zombies. Join/leave/timeout behavior is therefore part of MVP gameplay, not infrastructure background work. | HIGH | Server must assign slots deterministically, keep identities stable, and avoid corrupting active combat state when endpoints change. |
| AI zombie backfill for empty slots | Original Maze later added robot players, and this repo's active requirements rely on zombies occupying unused slots. Without backfill, the validation scenario does not exist. | MEDIUM | AI can stay simple and slow. The requirement is continuity of play and slot occupancy, not good tactics. |
| Human takes over zombie slot cleanly | The project explicitly requires human connections to replace zombies without breaking gameplay. That replacement path is part of core multiplayer correctness. | HIGH | Requires join/leave semantics, slot ownership, and immediate authoritative state handoff without duplicate bullets, ghost players, or stale joy state. |
| Shared scoreboard/state visibility | Score was added early in Maze's multiplayer evolution and is already part of this repo's authoritative snapshot. Players need reliable feedback on kills, deaths, and who is zombie vs human. | LOW | Keep presentation simple. Accuracy matters more than UI sophistication. |
| Consistent brick/wall state across clients | This repo's protocol includes full and delta brick updates. If destructible state diverges, movement and shooting become impossible to reason about. | MEDIUM | Full sync on join, delta thereafter. This is below movement/combat priority but still required for deterministic shared play. |

### Differentiators (Worth Deferring)

These can improve the project later, but they should not displace authoritative gameplay work in the current milestone.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Smarter zombie personalities or difficulty tiers | Better bots make solo and sparse sessions more interesting. | MEDIUM | Explicitly out of scope in `PROJECT.md`; current zombies only need to preserve match continuity. |
| In-game radar/map overlay on Atari | Maze and later descendants often expose top-down map/radar views. This is a recognizable genre extra, especially for retro fans. | MEDIUM | Good historical fit, but not required to prove smooth authoritative play. Add after sync and combat are trustworthy. |
| Observer/spectator mode | Historically associated with Maze War lineage and useful for testing sessions without consuming a slot. | MEDIUM | Valuable for debugging and demos, but not needed for milestone validation. |
| Text chat / lobby affordances | Early Maze versions supported text messages; modern players also expect some pre-match coordination. | MEDIUM | Scope creep for an Atari-first gameplay milestone. Use external coordination for now. |
| Multiple maze sets / editor workflow | Original Maze evolved to support custom mazes and level editing. | HIGH | Attractive long-term differentiator, but it multiplies sync, testing, and content validation effort. |
| Team modes / alternate rulesets | Could make the project feel broader than a straight deathmatch-style restoration. | HIGH | Requires rule changes, HUD work, and more join/respawn complexity. Defer until baseline deathmatch is solid. |

### Anti-Features (Distractions For This Milestone)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Richer AI before sync is solved | Bots are visible and easy to improve, so they look like progress. | Better AI does not fix teleporting, bad shot origin, or zombie/human handoff bugs; it hides the real milestone risk. | Keep zombies simple and deterministic until movement/combat are stable. |
| New weapons, power-ups, or inventory | These are common ways to make retro shooters feel "bigger." | They add new state to synchronize before the base shot/hit loop is correct. | Keep one weapon and focus on reliable fire, hit, death, and respawn. |
| Cosmetic HUD polish first | Atari HUD work is tempting because it is immediately visible. | It consumes time without reducing authoritative gameplay bugs. | Limit HUD work to score, role, and essential status indicators. |
| Large player counts for first validation | Maze descendants are remembered for multi-node sessions, so bigger matches sound impressive. | The milestone only needs one Atari, one Linux, and two zombies. Scaling player count early expands failure modes in sloting, packets, and rendering. | Lock validation to the concrete 4-slot mixed session first. |
| Fancy interpolation that overrides original animation cadence | Smoothness is the current pain point. | If smoothing changes movement language too much, the game stops feeling like Atari Maze War and can desync combat perception. | Predict locally, but preserve original step/turn presentation and reconcile gently to server state. |
| Feature creep into social/lobby systems | Join flow often expands into names, rooms, ready states, and menus. | None of that proves the core gameplay loop works on FujiNet. | Use direct connect plus automatic slot assignment for this milestone. |

## Feature Dependencies

```text
Authoritative movement + local smoothing
    └──requires──> authoritative snapshots and reconciliation
                        └──requires──> stable slot identity per client

Shot origin correctness
    └──requires──> visible local pose matches authoritative fire pose
                        └──requires──> movement reconciliation to settle before/during fire

Hit / death / respawn correctness
    └──requires──> authoritative shots
                        └──requires──> authoritative movement

Human join/leave handling
    └──requires──> stable slot management
                        └──enables──> zombie backfill
                        └──enables──> human replaces zombie cleanly

Zombie replacement behavior
    └──requires──> join/leave semantics
    └──requires──> AI can relinquish slot without stale input or ghost state

Shared score/state visibility
    └──requires──> hit / death / respawn correctness

Brick synchronization
    └──supports──> deterministic movement
    └──supports──> deterministic shots and line-of-fire checks
```

### Dependency Notes

- **Movement sync is the root dependency:** if local pose and server pose diverge, every downstream feature breaks, especially shot origin and hit credibility.
- **Combat depends on sync, not the other way around:** do not try to tune bullets or respawn UX before authoritative movement and fire pose are trustworthy.
- **Join/leave and AI replacement are one feature cluster:** zombie backfill only works if slot ownership can move cleanly between timeout, AI control, and new human control.
- **Brick state is a hidden gameplay dependency:** even if wall destruction is secondary, desynced maze state invalidates both movement collision and combat resolution.

## MVP Definition

### Launch With (v1)

- [ ] Smooth client-predicted Atari movement under server authority — this is the primary product claim.
- [ ] Correct fire/hit/death/respawn loop with bullet origin matching the visible local wizard — this makes combat trustworthy.
- [ ] Stable 4-slot mixed session: 1 Atari, 1 Linux, 2 zombies — this is the explicit validation scenario.
- [ ] Automatic zombie fill plus clean human takeover of empty/zombie slots — this keeps sessions playable despite joins and leaves.
- [ ] Accurate score/role display sourced from authoritative state — this is the minimum shared match feedback.

### Add After Validation (v1.x)

- [ ] Radar/map overlay — add once core sync is proven and CPU/rendering budget is understood.
- [ ] Observer mode and better diagnostics — useful after baseline gameplay works and test loops broaden.
- [ ] Better zombie behavior — add when weak gameplay is due to bot quality rather than sync failures.

### Future Consideration (v2+)

- [ ] Custom mazes / editor pipeline — defer until protocol, storage, and validation strategy are mature.
- [ ] Alternate game modes or teams — defer until the default deathmatch loop is robust.
- [ ] Social/lobby/chat systems — defer until there is evidence the project needs them.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Authoritative movement with local smoothing | HIGH | HIGH | P1 |
| Shot origin correctness | HIGH | HIGH | P1 |
| Hit/death/respawn correctness | HIGH | MEDIUM | P1 |
| Stable join/leave plus zombie takeover | HIGH | HIGH | P1 |
| Score/role visibility | MEDIUM | LOW | P1 |
| Brick synchronization consistency | MEDIUM | MEDIUM | P1 |
| Radar/map overlay | MEDIUM | MEDIUM | P2 |
| Observer mode | LOW | MEDIUM | P2 |
| Smarter AI | MEDIUM | MEDIUM | P2 |
| Custom mazes / editor | MEDIUM | HIGH | P3 |
| Team modes / alternate rules | LOW | HIGH | P3 |
| Chat/lobby systems | LOW | MEDIUM | P3 |

**Priority key:**
- P1: Must have for launch
- P2: Should have, add when possible
- P3: Nice to have, future consideration

## Competitor / Precedent Feature Analysis

| Feature | Original Maze | MIDI Maze / Faceball 2000 | Our Approach |
|---------|---------------|---------------------------|--------------|
| Core loop | Move, turn, shoot, score, respawn | Move smoothly, shoot, deathmatch-style play | Match the simple loop first; do not widen the rules before it is stable on Atari |
| Bots | Robot players supported in later versions | AI/drone logic present in lineage | Keep bots only as slot backfill for session continuity |
| Map / radar | Present in later Maze versions | Map/radar-like affordances exist in later descendants | Defer until sync/combat correctness is solved |
| Large multiplayer | Expanded from 2 to 8+ players historically | Up to 16 STs, smaller on handheld ports | Validate the concrete 4-slot mixed session first |
| Extra systems | Text messages, custom mazes, observer/radar in lineage | More options and mode variations in descendants | Treat as historical inspiration, not milestone scope |

## Sources

- [Project brief](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/PROJECT.md)
- [Protocol semantics](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md)
- [Repository networking overview](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md)
- https://en.wikipedia.org/wiki/Maze_War
- https://en.wikipedia.org/wiki/MIDI_Maze
- https://strategywiki.org/wiki/Faceball_2000/Gameplay

---
*Feature research for: Atari 8-bit FujiNet networked Maze War*
*Researched: 2026-04-07*
