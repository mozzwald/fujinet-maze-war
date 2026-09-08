# Phase 3: Combat and World Authority - Research

**Researched:** 2026-04-08
**Domain:** Authoritative combat ordering, projectile origin, respawn/score authority, and shared brick/world consistency across server, Atari, and Linux clients
**Confidence:** HIGH

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| COMB-01 | Atari fire input sends intent only; projectile origin is derived from authoritative wizard state using one shared server-defined action order. | The server already treats fire as `joy` trigger intent and derives shot spawn from authoritative `players[shooter].x/y` before movement in [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L754) and [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L986); Phase 3 should freeze and document that contract, then remove any client-side assumptions that compete with it. |
| COMB-02 | Bullets shown on the Atari client always originate from the currently visible wizard position and facing direction. | Atari already renders only server-authoritative shots through `NET_SHOT_APPLY`, but it applies `SHOT`, `RESPAWN`, and `SNAPSHOT` on separate paths with independent sequencing in [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1650), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1802), and [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2278); the shot apply path needs to be tied explicitly to the visible actor state, not just raw packet coordinates. |
| COMB-03 | Server uses one explicit same-tick ordering contract for turn, move, fire, hit, death, and respawn behavior. | The current authoritative order is implicit in `step_players`: resolve respawns, compute `action_joy`, call `start_shot`, optionally move when not firing directionally, then `step_shots` in the same tick ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L938), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L986), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L997)). Phase 3 should make that contract explicit and testable. |
| COMB-04 | Linux and Atari clients both follow the same combat ordering contract so identical inputs do not produce different visible shot behavior. | Linux clients consume `SHOT`, `RESPAWN`, `BRICK_DELTA`, and `SNAPSHOT` independently in [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c#L209) and [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c#L930); Atari does the same in separate VBI queues. Phase 3 should align both client families to one documented authoritative event interpretation. |
| WRLD-01 | Brick or wall state stays consistent across Atari client, Linux client, and server so movement and line-of-fire checks use the same maze state. | The server destroys non-border bricks on immediate fire and on moving shots ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L773), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L846)); Atari mirrors the authoritative map through `NET_BRICK_FULL_APPLY` and `NET_BRICK_DELTA_APPLY` ([`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2154), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2252)); Linux mirrors the same packets in both clients ([`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c#L200), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c#L913)). Phase 3 should harden the shared world contract around combat. |
| WRLD-02 | Score, death, and respawn state displayed to clients matches server-authoritative gameplay outcomes. | Server score increments and respawn transitions originate only in `start_shot` and `step_shots` ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L798), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L871)); Atari updates scoreboard digits from snapshots and hides/unhides actors on respawn packets ([`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1521), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1839)); Linux clients do the same via packet handlers ([`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c#L209), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c#L953)). |
</phase_requirements>

## Summary

Phase 3 should not invent new combat mechanics. The server already has an authoritative combat model, but the model is implicit rather than frozen: accepted `DELTA` packets overwrite per-slot `joy` state, then each tick resolves respawns, evaluates fire from authoritative position/facing, suppresses same-tick directional movement while firing, and only then advances active shots ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L453), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L754), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L912)). That ordering is the right seam to freeze.

The main risk is not missing packet types. It is cross-packet drift. `SNAPSHOT` carries authoritative position, facing, and score; `SHOT` carries projectile state; `RESPAWN` carries dead/final spawn transitions; and `BRICK_DELTA` mutates line-of-fire geometry ([`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L48), [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L122), [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L157), [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L171)). Atari and Linux both consume those packets on separate paths, so visible shot origin can drift if the client does not explicitly relate a shot packet to the authoritative actor state that is currently on screen.

The planning implication is to make the server’s combat order explicit in protocol/docs and smoke tests first, then align Atari and Linux to that contract rather than adding more prediction. Phase 3 should be about one authoritative ordering contract, one bullet-origin rule, one brick/world truth, and one score/death/respawn truth. Render smoothing belongs to Phase 4.

**Primary recommendation:** Freeze the authoritative combat contract on the server and in `doc/protocol.md`, then make Atari and Linux consume `SHOT`, `RESPAWN`, `SNAPSHOT`, and `BRICK_DELTA` as one coherent combat/world stream whose visible outcome always reflects the server-defined order.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ISO C / POSIX sockets | Repo-local toolchain | Server authoritative combat order and packet emission | The server already owns move/fire/hit/respawn truth in plain C and should remain the single source of combat authority. |
| MADS assembler | Repo-local toolchain | Atari authoritative packet consumption and visible shot origin handling | The Atari client already has the VBI-owned seams for snapshot, respawn, and shot application; Phase 3 should harden those paths rather than replace them. |
| Linux C clients (`main.c`, SDL 1.2 viewer) | Repo-local toolchain + system libs | Protocol parity, deterministic smoke coverage, and quicker combat debugging | Linux clients already expose the packet interpretation path and are faster to use as parity and smoke-test twins before real FujiNet checks. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Existing shell smoke scripts | Repo-local | Low-latency protocol/ordering/world regression checks | Add focused combat/world smokes beside the Phase 1 and Phase 2 harnesses. |
| Existing protocol doc | Repo-local | Frozen combat/world wire contract | Update it so ordering semantics are explicit instead of scattered across handlers. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Freeze server ordering and align clients | Push more local shot prediction into Atari/Linux | That would blur authority again and moves render/prediction complexity into the combat phase. |
| Use packet-level smoke tests plus one real Atari checkpoint | Depend only on manual mixed sessions | Too slow to debug same-tick ordering and cross-client parity regressions. |
| Keep one world authority path for bricks/scores/respawns | Let clients derive extra combat state from local heuristics | Risks exactly the visible divergence this phase is trying to eliminate. |

**Installation:**
```bash
make all
```

**Version verification:** No new external dependencies are required. The active build remains the repository `Makefile` in [`Makefile`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile#L1).

## Architecture Patterns

### Recommended Project Structure
```text
server/
├── main.c                  # Freeze same-tick action order and authoritative combat/world emissions
└── transport_*.{c,h}       # Keep Phase 1 ingress normalization intact

clients/atari/
└── maze-war.asm            # Align visible shot/respawn/brick handling with authoritative local-visible state

clients/linux/
├── main.c                  # Protocol/debug twin for ordering and world-authority parity
└── sdl_main.c              # Visual parity client for shot, respawn, and score/world interpretation

doc/
└── protocol.md             # Freeze the combat/world contract and packet semantics

tests/
├── combat_ordering_smoke.sh
├── combat_world_authority_smoke.sh
└── combat_client_parity_smoke.sh
```

### File Ownership
| Area | Files | Phase 3 Ownership |
|------|-------|-------------------|
| Authoritative combat contract | [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c), [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md) | Primary. Make same-tick ordering and packet meaning explicit and testable. |
| Atari visible combat/world state | [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) | Primary. Keep visible wizard, bullet origin, respawn masking, and brick map consistent with server order. |
| Linux parity path | [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c) | Secondary. Mirror the frozen contract and provide faster deterministic validation. |
| Regression coverage | [`tests/`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests) | Secondary. Add same-tick ordering, world-authority, and parity smokes. |

### Pattern 1: Freeze One Same-Tick Action Contract
**What:** Document and preserve the exact authoritative order: respawn resolution, current-input selection, fire evaluation from authoritative actor state, movement if firing does not consume the directional intent, then shot stepping.
**When to use:** Every server tick.
**Example:**
```c
// Source shape in server/main.c today
if (players[i].respawn_at_ms != 0 && now >= players[i].respawn_at_ms) {
  // finalize respawn
}
action_joy = players[i].joy;
start_shot(i, players, shots, bricks, sock, clients, seq, action_joy, debug);
if (can_move && !(trig && stick != 0x0F)) {
  apply_move_if_free(&players[i], bricks, players, i);
}
step_shots(players, shots, bricks, sock, clients, seq, debug);
```

### Pattern 2: Treat Fire as Intent, Not Client-Side Shot Simulation
**What:** Clients send only `joy` intent through `DELTA`. Shot spawn, immediate brick breaks, immediate adjacent hits, score increments, and respawn transitions come only from the authoritative server.
**When to use:** Every fire/turn/move input combination.
**Example:**
```c
// Server-side only
uint8_t stick = (uint8_t)(joy & 0x0F);
int trig = (joy & 0x10) != 0;
if (!trig || !stick_to_cardinal_delta(stick, &dx, &dy)) {
  return;
}
int sx = players[shooter].x + dx;
int sy = players[shooter].y + dy;
```

### Pattern 3: Visible Shot Origin Must Reference the Visible Wizard State
**What:** On clients, a `SHOT` packet should never draw in a way that contradicts the currently visible authoritative wizard state for that slot. If packet timing would create a mismatch, the client should resolve the actor state first or defer/guard the draw.
**When to use:** Atari and Linux `SHOT` handling.
**Example:**
```text
visible wizard state for slot N
  + authoritative respawn mask
  + latest committed authoritative position/facing
  -> valid origin for authoritative SHOT packet of slot N
```

### Pattern 4: World Authority Flows Through BRICK_FULL/BRICK_DELTA Plus Server Collision Checks
**What:** The server decides when a brick is removed; clients update their local wall maps only from authoritative brick packets and use those maps for render/collision consistency.
**When to use:** Shot-vs-brick, move-vs-wall, and respawn spawn-point validation.
**Example:**
```c
if (is_brick(bricks, sx, sy)) {
  if (!is_outer_wall_cell(sx, sy)) {
    clear_brick(bricks, sx, sy);
    build_brick_delta((*seq)++, (uint8_t)sx, (uint8_t)sy, pkt, sizeof(pkt));
    broadcast_packet(sock, clients, pkt, sizeof(pkt));
  }
  return;
}
```

## Validation Architecture

### Test Strategy
Use three layers:
1. Server-focused smoke checks for authoritative same-tick ordering and world mutations.
2. Client-consumption smoke checks that prove Atari and Linux interpret `SHOT`, `RESPAWN`, `SNAPSHOT`, and `BRICK_DELTA` consistently with the frozen contract.
3. One real mixed-session Atari checkpoint for visible bullet origin and combat feel.

### Required Test Assets
- `tests/combat_ordering_smoke.sh`
  - Proves the server’s same-tick contract for move-then-fire, turn-then-fire, immediate adjacent hit, and fire-into-brick cases.
- `tests/combat_world_authority_smoke.sh`
  - Proves scores, respawn packets, and brick destruction all come from server-authoritative outcomes.
- `tests/combat_client_parity_smoke.sh`
  - Greps or drives Linux/Atari seams so both client families consume the same combat/world packet contract.

### Manual Checkpoint
- Mixed session with 1 Atari client, 1 Linux client, and 2 AI zombies.
- Reproduce move-then-fire and turn-then-fire on Atari and Linux.
- Approve only if the bullet visibly starts from the currently visible wizard location/facing, score/death/respawn outcomes match the server, and brick destruction stays identical across both clients.

### Recommended Plan Split
1. **03-01:** Freeze and test the authoritative combat/world contract on the server and in `doc/protocol.md`.
2. **03-02:** Align the Atari client’s shot/respawn/brick consumption so visible bullet origin and death/respawn outcomes follow the frozen contract.
3. **03-03:** Align Linux clients and add parity validation plus the real mixed-session checkpoint.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Implicit combat ordering scattered across handlers | Server already has one de facto authoritative order in `step_players`/`step_shots` | Pre-Phase 3 | Correct behavior exists, but it is not yet frozen as an explicit contract. |
| Client-local shot logic in legacy Maze War | Network clients render server-authoritative `SHOT` packets only | Existing net port | Good foundation for authority, but packet-to-visible-state alignment still needs tightening. |
| Threshold-only movement sync | Ack-driven authoritative reset plus replay | Phase 2 | Phase 3 can now build on a stable local-visible actor state instead of tuning teleports. |

**Deprecated/outdated:**
- Treating the protocol doc as sufficient even when it omits same-tick ordering semantics.
- Assuming a `SHOT` packet can be rendered in isolation without reference to current visible actor or respawn state.

## Open Questions

1. **Should the combat contract be exposed only in docs, or also as explicit helper boundaries in `server/main.c`?**
   - What we know: the current order is correct but implicit.
   - Planning stance: prefer small helper extraction if it makes smoke coverage and future phases less fragile.

2. **Should Atari defer a shot draw when the matching authoritative actor commit has not become visible yet?**
   - What we know: Atari already queues `SHOT` and `RESPAWN` in VBI-owned buffers.
   - Planning stance: allow a small guard/defer path if needed, but do not add local shot prediction.

3. **Should Linux parity validation be packet-driven only, or include visible-state assertions in the SDL client too?**
   - What we know: `clients/linux/main.c` is the fastest deterministic harness, while `sdl_main.c` shows visual behavior.
   - Planning stance: use `main.c` for deterministic smoke proof and `sdl_main.c` only where visual contract needs a concrete hook.
