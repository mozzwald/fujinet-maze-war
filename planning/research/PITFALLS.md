# Pitfalls Research

**Domain:** Atari 8-bit FujiNet networked multiplayer gameplay
**Researched:** 2026-04-07
**Confidence:** HIGH

## Critical Pitfalls

### Pitfall 1: Threshold-Based Reconciliation Instead of Ack-Based Reconciliation

**What goes wrong:**
The Atari client feels jumpy because local movement is predicted, but corrections are driven by distance thresholds and snapshot drift instead of replaying only unacknowledged local inputs. The visible result is snap-back, teleport-like correction, and a bullet being fired from a tile the player is no longer looking at.

**Why it happens:**
Authoritative snapshots arrive at a low fixed rate, but the protocol does not tell the client which local input sequence the server has already applied. That forces the client to guess when to reconcile. In this repo, the Atari code already relies on heuristic thresholds such as `NET_RECON_P0`, `NET_HARD_P0`, and desync counters instead of true server reconciliation ([clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L75), [clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1416), [clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2713)).

**How to avoid:**
- Add an explicit "last processed local input seq" field to authoritative server updates for the recipient.
- Keep a small ring buffer of Atari local inputs and replay only inputs newer than the server-acked sequence.
- Reconcile from authoritative state plus unacked inputs, not from distance thresholds alone.
- Keep hard snaps only as a corruption guard rail, not as the normal correction path.

**Warning signs:**
- Fixes require constant retuning of `NET_RECON_*` / `NET_HARD_*` values.
- Double-tap move or turn-fire-turn sequences visibly snap backward on Atari.
- Bullet origin is wrong mainly after rapid local movement, not when standing still.
- Linux client looks stable while Atari still pops, because the problem is prediction bookkeeping rather than server authority.

**Phase to address:**
Phase 1: Protocol and reconciliation contract.

---

### Pitfall 2: Undefined Same-Tick Ordering for Turn, Move, and Fire

**What goes wrong:**
The server and Atari renderer disagree about whether a fire action uses pre-move, post-move, pre-turn, or post-turn actor state. That creates the exact symptom this project is chasing: bullets spawn from the wrong row or column relative to the wizard the Atari player sees.

**Why it happens:**
Fast multiplayer projects often say "server authoritative" but never freeze the exact order of operations inside one tick. In this repo, the server calls `start_shot()` before `apply_move_if_free()` and suppresses movement when trigger and a cardinal stick are both active ([server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L721), [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L953)). If the Atari client visually animates movement or turning ahead of that authoritative ordering, the muzzle position and the server shot origin diverge.

**How to avoid:**
- Write an explicit gameplay contract for one input tick:
  `sample input -> resolve facing -> resolve fire -> resolve move` or another fixed order.
- Make server, Linux client, Atari prediction, and tests all follow that same ordering.
- If a shot must match the Atari-visible wizard position, include enough data in the shot event to prove it: shooter tile, facing, and ideally the local input seq or action seq that produced the shot.
- Add deterministic test vectors for "move+fire same frame", "turn+fire same frame", and "fire during reconcile".

**Warning signs:**
- Bullets are correct when stationary but wrong after quick strafelike cell changes or turns.
- Bugs appear only when trigger is pressed during an animation frame boundary.
- Fixes that only adjust render timing improve symptoms without eliminating wrong origins.

**Phase to address:**
Phase 2: Shot semantics and action ordering.

---

### Pitfall 3: Smoothing at the Gameplay State Instead of at the Render Layer

**What goes wrong:**
Movement gets smoother in one case but breaks correctness in another. Actors pass through bad intermediate cells, bullets inherit fake positions, or reconciliation becomes harder because the gameplay state is no longer a clean copy of authoritative-plus-predicted state.

**Why it happens:**
The tempting fix for 10 Hz snapshots is to lerp the actor state directly. That is the wrong abstraction for an authoritative game. Gaffer explicitly recommends snapping simulation state hard and smoothing only rendering error, because smoothing the simulation state makes extrapolation start from invalid invented state. The repo already separates some staged network state from live VBI-applied state on Atari, which is the correct direction ([clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1353), [clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1602)).

**How to avoid:**
- Treat collision, shot origin, occupancy, and reconciliation state as authoritative/predicted simulation state only.
- Apply visual smoothing as a separate render offset or animation choice, never by mutating authoritative coordinates.
- For remote players, prefer interpolation from authoritative history over dead reckoning unless the movement model is predictably inertial.
- For the local Atari wizard, preserve original Maze War movement animation, but source the animation from reconciled state transitions, not fake intermediate coordinates.

**Warning signs:**
- A bug fix makes motion look smoother but causes more wrong bullet starts or hit registration oddities.
- Remote actors appear to glide through blocked cells that the server never allowed.
- There are multiple "current position" variables and it is no longer obvious which one bullets or occupancy checks use.

**Phase to address:**
Phase 3: Render/simulation separation.

---

### Pitfall 4: Treating Transport Framing Drift as a Gameplay Problem

**What goes wrong:**
Movement drops, stale input filtering misfires, or fire/turn packets disappear sporadically because packet framing is inconsistent before gameplay code ever sees the data. The gameplay layer then gets patched with compatibility branches and resync hacks, which hides the transport defect and makes correctness harder to reason about.

**Why it happens:**
FujiNet NetStream here is byte-stream oriented, while the game protocol is packet-oriented over UDP semantics. This repo already has compatibility handling for swapped `pid/seq` and an extra leading `0x41` before `DELTA` packets ([server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L388), [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L483), [doc/protocol.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L87)). That is a practical stopgap, but if the edge adapter is not normalized, input timing bugs get misdiagnosed as movement bugs.

**How to avoid:**
- Enforce one canonical outbound `DELTA` wire format at the Atari transport boundary.
- Instrument counts for malformed, swapped, normalized, dropped, and stale packets; treat any non-zero steady-state normalization count as a bug to remove.
- Keep packet reconstruction in one transport layer, not spread through simulation logic.
- Evaluate enabling FujiNet UDP sequencing support if it matches the handler path used here ([ref/netstream_api.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/ref/netstream_api.md#L31)).

**Warning signs:**
- Server logs show `using swapped seq/pid decode`, `DELTA normalize`, or `DELTA resync` during normal play.
- Jumpy movement gets worse under load without any change in reconciliation math.
- Repro depends on specific FujiNet-PC or handler paths rather than gameplay situations.

**Phase to address:**
Phase 0: NetStream transport normalization and observability.

---

### Pitfall 5: Slot Ownership Without an Ownership Epoch

**What goes wrong:**
When a zombie is replaced by a human, or a disconnected human slot falls back to AI, stale transient state leaks across owners: `joy`, pending fire, active shots, dead masks, pending respawn, desync counters, or local client assumptions about who "owns" the actor. The result is ghost shots, frozen actors, or the new human inheriting zombie intent.

**Why it happens:**
This project binds identity to a slot and uses empty client slots as zombie candidates ([server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L538), [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L887), [doc/protocol.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L169)). That is fine, but slot reuse is a handoff problem, not just a join problem. Gaffer also points out that address+port-based identity and dirty reconnects create undefined states if reconnect happens before timeout.

**How to avoid:**
- Introduce a per-slot ownership epoch or generation counter that changes on every human/zombie handoff.
- Reset all transient actor state on ownership change: `joy`, active shot, fire pending, respawn state, desync counters, render guards, and any local pending input queue.
- Broadcast a dedicated role/ownership transition event or encode the epoch in snapshots so clients can invalidate stale local state immediately.
- Add mixed-session tests that cover zombie -> human takeover and human timeout -> zombie takeover while the slot is moving, dead, and firing.

**Warning signs:**
- A newly connected human immediately appears facing or firing in the zombie’s last direction.
- Shot clear packets or respawn hides persist after a takeover.
- The bug reproduces only after disconnect/reconnect or only in the 1 Atari + 1 Linux + 2 zombie scenario.

**Phase to address:**
Phase 4: AI/human handoff and slot lifecycle.

---

### Pitfall 6: Timeout and Reconnect Semantics That Are Too Slow for Gameplay Debugging

**What goes wrong:**
A broken or silent Atari client leaves a slot in a half-live state long enough to poison playtests. Developers then chase movement and bullet bugs that are really stale ownership or stale input bugs.

**Why it happens:**
The current protocol times out clients after 60 seconds and only neutralizes stale human input after 500 ms ([server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L22), [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L927), [doc/protocol.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L174)). That is acceptable for a bare transport, but too slow for debugging actor handoff and too forgiving of undefined reconnect state.

**How to avoid:**
- Add an explicit heartbeat/join lifecycle for clients, even if minimal.
- Shorten test-mode disconnect detection so ownership transitions happen promptly during development.
- Log slot assignment, timeout, zombie takeover, and human reclaim with timestamps and sequence numbers.
- Make reconnect behavior explicit: same slot reclaim or new slot assignment, never accidental mixture.

**Warning signs:**
- A playtest requires waiting tens of seconds before the world stabilizes after a disconnect.
- Zombies do not resume cleanly after a client vanishes.
- "Movement bug" repro steps often include restarting FujiNet-PC or reconnecting the Atari client.

**Phase to address:**
Phase 4: AI/human handoff and slot lifecycle.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Tune reconcile thresholds instead of adding input ack/replay | Fast symptom reduction | Permanent snap-back edge cases and fragile bullet origin behavior | Only as a temporary guard rail |
| Let render state double as gameplay state | Less bookkeeping on Atari | Hard-to-debug causality bugs around bullets and collisions | Never |
| Keep server-side compatibility parsing for broken DELTA framing indefinitely | Mixed clients keep working | Protocol remains ambiguous and bugs hide below gameplay | Only until transport is normalized |
| Reuse a slot without resetting transient state | Minimal code | Zombie/human ghost state leaks across owners | Never |

## Integration Gotchas

Common mistakes when connecting to external services.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| FujiNet NetStream | Assuming packet boundaries survive a byte-stream path exactly | Normalize framing at the transport edge and instrument malformed/resync cases |
| FujiNet UDP mode | Ignoring handler flags such as UDP sequencing support | Verify handler capabilities and use one documented configuration consistently |
| Mixed Atari/Linux session | Validating only with Linux client behavior | Treat 1 Atari + 1 Linux + 2 zombies as the required correctness scenario |

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Broadcasting low-rate snapshots without an interpolation/reconciliation strategy | Visible 100 ms jumps and overcorrection | Separate local prediction from remote interpolation and visual smoothing | Already breaking at current 10 Hz |
| Excessive hard-snap correction on Atari | Original animation feel is destroyed | Use replayed unacked input and bounded visual error reduction | Breaks as soon as latency/jitter rises above local LAN conditions |
| Over-logging every packet in steady state | Timing changes hide race conditions | Use counters and sampled debug logging | During mixed-session debugging on slower hosts |

## Security Mistakes

Domain-specific security issues beyond general web security.

| Mistake | Risk | Prevention |
|---------|------|------------|
| Treating UDP source address+port as enough identity for robust reconnect semantics | Slot hijack or undefined reconnect state on dirty reconnect | Use explicit session/ownership semantics if reconnect reliability matters |
| Trusting payload pid over slot identity | One client can spoof another actor | Keep server authority bound to source slot, as current protocol already does |

## UX Pitfalls

Common user experience mistakes in this domain.

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Correct bullets on the server but not on the screen | Game feels unfair and broken | Make visible wizard pose and authoritative fire origin match exactly |
| Smooth remote actors but let local movement snap | Controls feel bad even when rules are correct | Prioritize local prediction correctness first, then remote polish |
| Hide handoff state during zombie/human swaps | Players see "random" behavior | Surface slot role changes clearly during debugging and testing |

## "Looks Done But Isn't" Checklist

- [ ] **Local prediction:** Often missing server ack of processed local input — verify Atari replays only unacked inputs.
- [ ] **Shot origin:** Often missing explicit same-tick action ordering — verify move/turn/fire semantics with deterministic fixtures.
- [ ] **Transport:** Often missing canonical DELTA framing — verify server no longer normalizes swapped or duplicated `0x41` in normal play.
- [ ] **Handoff:** Often missing ownership epoch reset — verify zombie/human swaps clear shots, joy, respawn, and desync state.
- [ ] **Mixed session:** Often missing cross-client validation — verify 1 Atari + 1 Linux + 2 zombies for at least one full connect/disconnect cycle.

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Threshold-based reconciliation | MEDIUM | Add server ack field, keep an input replay buffer, then reduce hard-snap thresholds after instrumentation proves replay is stable |
| Undefined fire/move ordering | MEDIUM | Freeze one tick-order contract, update server and clients to match, add replayable tests from packet captures |
| Transport framing drift | LOW | Normalize DELTA packing at one edge, add counters, remove compatibility branches after captures confirm clean traffic |
| Slot ownership leakage | HIGH | Add ownership epoch/reset semantics, invalidate stale client state on handoff, and retest connect/disconnect under active movement/fire |

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Threshold-based reconciliation instead of ack-based reconciliation | Phase 1: Protocol and reconciliation contract | Atari can move rapidly without visible snap-back while packet captures show acked input replay working |
| Undefined same-tick ordering for turn, move, and fire | Phase 2: Shot semantics and action ordering | Deterministic tests prove bullet origin matches visible wizard tile/facing for move-fire and turn-fire cases |
| Smoothing at the gameplay state instead of at the render layer | Phase 3: Render/simulation separation | Actors render smoothly without bullets, occupancy, or collisions using interpolated/fake coordinates |
| Treating transport framing drift as a gameplay problem | Phase 0: NetStream transport normalization and observability | Normal sessions produce zero normalize/resync warnings and stable DELTA acceptance |
| Slot ownership without an ownership epoch | Phase 4: AI/human handoff and slot lifecycle | Zombie/human swaps during movement, death, and firing leave no stale shots, joy, or hidden actors |
| Timeout and reconnect semantics that are too slow for gameplay debugging | Phase 4: AI/human handoff and slot lifecycle | Dirty disconnect and reconnect scenarios converge quickly and predictably in mixed-session tests |

## Sources

- Local protocol and implementation:
  - [doc/protocol.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md)
  - [server/main.c](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c)
  - [clients/atari/maze-war.asm](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm)
  - [ref/netstream_api.md](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/ref/netstream_api.md)
- External networking references:
  - Gabriel Gambetta, "Client-Side Prediction and Server Reconciliation": https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html
  - Gabriel Gambetta, "Entity Interpolation": https://www.gabrielgambetta.com/entity-interpolation.html
  - Glenn Fiedler, "Snapshot Interpolation": https://gafferongames.com/post/snapshot_interpolation/
  - Glenn Fiedler, "State Synchronization": https://gafferongames.com/post/state_synchronization/
  - Glenn Fiedler, "Client Server Connection": https://gafferongames.com/post/client_server_connection/

---
*Pitfalls research for: Atari 8-bit FujiNet networked multiplayer gameplay*
*Researched: 2026-04-07*
