# Architecture Research

**Domain:** Atari 8-bit Maze War with server-authoritative multiplayer, Linux test client, and server AI zombies
**Researched:** 2026-04-07
**Confidence:** HIGH

## Standard Architecture

### System Overview

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                               Client Layer                                 │
├───────────────────────────────┬─────────────────────────────────────────────┤
│ Atari ASM Client              │ Linux Test Client                           │
│ - input sampling              │ - same packet protocol                      │
│ - local prediction            │ - deterministic debug rendering             │
│ - render interpolation        │ - packet trace / replay hooks               │
│ - server correction           │ - no client authority                       │
└───────────────┬───────────────┴───────────────────────────────┬─────────────┘
                │ input deltas (seq, joy/fire)                 │
                │ snapshots / shot / respawn / brick updates   │
┌───────────────▼───────────────────────────────────────────────▼─────────────┐
│                         Authoritative Server Loop                            │
├─────────────────────────────────────────────────────────────────────────────┤
│  Connection/slot manager  |  Input queue/ack  |  World simulation          │
│  Zombie slot allocator    |  Snapshot builder |  Projectile authority       │
│  Packet normalizer        |  Event broadcaster|  Respawn / scoring          │
└───────────────┬───────────────────────────────────────────────┬─────────────┘
                │                                               │
                │ authoritative state                           │ AI reads world,
                │                                               │ writes virtual input
┌───────────────▼──────────────────────────┐   ┌────────────────▼─────────────┐
│ World State                              │   │ AI Zombie Controller         │
│ - players                                │   │ - server-only                │
│ - occupied cells                         │   │ - same movement/fire rules   │
│ - bricks                                 │   │ - replaces empty human slots │
│ - active shots                           │   │ - emits virtual joy/fire     │
└──────────────────────────────────────────┘   └──────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Typical Implementation |
|-----------|----------------|------------------------|
| Atari client gameplay loop | Sample joystick, run short-horizon local prediction, animate movement, apply bounded server correction | MADS ASM with separate predicted/live authoritative arrays and VBI-owned commit path |
| Linux test client | Mirror protocol and prediction model for debugging, packet capture, deterministic repro | C client with optional trace logging and state diff tools |
| Server transport layer | Bind endpoints to slots, normalize odd FujiNet framing, reject stale input, broadcast world events | UDP socket loop with per-slot input sequence tracking |
| Server simulation | Advance players, shots, bricks, scoring, respawn, and zombie behavior from authoritative state | Single-threaded fixed tick loop |
| AI zombie controller | Decide movement/fire for unoccupied slots using the same rules as humans | Server-side virtual input generator, not a fake network client |

## Recommended Project Structure

```text
server/
├── net/                # packet decode/encode, slot binding, seq ack rules
├── sim/                # players, collision, shots, respawn, score
├── ai/                 # zombie slot replacement and decision logic
└── main.c              # tick scheduler and wiring

clients/linux/
├── protocol/           # shared packet structs/constants and decode tests
├── client_core/        # prediction, reconciliation, interpolation
├── debug/              # traces, repro harness, desync diff tooling
└── main.c              # IO and rendering

clients/atari/
├── net/                # NS handler IO, packet parser, staging buffers
├── sim/                # local predicted actor state and reconcile helpers
├── render/             # VBI/DLI-owned animation and draw state
└── maze-war.asm        # integration entry point
```

### Structure Rationale

- **Server split by transport vs simulation:** desync bugs are easier to isolate when packet acceptance and world advancement are not interleaved in one function.
- **Atari split by network/sim/render:** the 6502 client cannot afford accidental shared mutable state between parser and renderer; staging then VBI commit is the right pattern.
- **Linux as protocol twin:** it should behave like the Atari client logically, but add observability the Atari cannot.

## Recommended Architecture

The right model is a four-loop architecture:

1. **Local prediction loop on each client** for the local actor only.
2. **Authoritative simulation loop on the server** for all actors, shots, bricks, score, and respawn.
3. **Interpolation/recovery loop on each client** for remote actors and server corrections.
4. **Server-only AI loop** that fills empty slots by writing virtual input into the same simulation path as humans.

The critical change from the current protocol is this: **every authoritative snapshot to a client should include the last input sequence from that client that the server has applied**. Without that acknowledgement, the client can compare positions, but it cannot correctly discard already-consumed predicted inputs and replay only the remaining ones. That is the standard requirement for server reconciliation.

### Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| Atari input sampler | Reads stick/trigger, debounces, canonicalizes to cardinal directions | Atari predictor, TX packet builder |
| Atari predictor | Applies pending local inputs immediately to predicted local actor state | Renderer, reconciliation buffer |
| Atari reconciliation buffer | Stores recent local inputs by `input_seq`, drops acked inputs, replays unacked inputs after correction | Snapshot apply path, predictor |
| Atari renderer | Consumes predicted local actor, interpolated remote actors, and authoritative shot events | VBI/DLI routines only |
| Server packet normalizer | Converts FujiNet oddities into canonical input packets, validates slot ownership and input format | Server input queue |
| Server input queue | Stores latest accepted input per human slot and last applied seq per slot | Server simulation, snapshot builder |
| Server simulation | Moves actors, spawns shots, resolves hits, clears bricks, manages respawn | Snapshot builder, event broadcaster |
| Snapshot builder | Emits recipient-specific snapshot with authoritative positions and `last_applied_input_seq` for that recipient | Atari client, Linux client |
| Remote actor smoother | On each client, lerps or step-blends non-local actors toward latest authoritative cell | Renderer |
| Zombie controller | Replaces empty slots, emits virtual input into the same server simulation path as humans | Server simulation |

## Architectural Patterns

### Pattern 1: Predicted Local Actor + Authoritative Anchor

**What:** Keep two representations for the local wizard on the client:

- `predicted_local`: what the player sees immediately
- `authoritative_local`: latest server-confirmed cell and facing

Prediction always updates `predicted_local`. Snapshots only update `authoritative_local`. Reconciliation rebuilds `predicted_local` from `authoritative_local` plus unacked inputs.

**When to use:** Always for the local Atari wizard.

**Trade-offs:** Slightly more state, but it prevents direct snapshot writes from fighting the animation loop.

**Example:**
```c
on_local_input(seq, joy) {
  ring_push(pending_inputs, {seq, joy});
  predicted_local = simulate_one_step(predicted_local, joy);
}

on_snapshot(authoritative_pos, last_applied_seq) {
  authoritative_local = authoritative_pos;
  ring_drop_through(pending_inputs, last_applied_seq);
  predicted_local = authoritative_local;
  for each input in pending_inputs:
    predicted_local = simulate_one_step(predicted_local, input.joy);
}
```

### Pattern 2: Remote Actors Are Never Predicted

**What:** Remote humans and zombies use snapshot-driven target cells plus short visual smoothing. They do not run input prediction on the client.

**When to use:** For slots other than the local player.

**Trade-offs:** Remote motion is slightly delayed, but much simpler and more stable on a 6502. This is the correct trade for Atari.

### Pattern 3: Server-Owned Projectile Origin

**What:** The server decides whether a shot starts, from which cell, and in which direction. Clients may play a tiny muzzle flash immediately, but the projectile itself is rendered only from authoritative shot packets.

**When to use:** Always. Projectile origin mismatch is a gameplay bug, not cosmetic lag.

**Trade-offs:** Slight delay before visible bullet travel under high latency, but origin correctness is guaranteed if the local client reconciles before drawing the first projectile segment.

**Example:**
```c
server_tick(player) {
  if (trigger_pressed(player.input) && !player.shot.active) {
    shot.origin = cell_in_front_of(player.authoritative_pos, player.facing);
    spawn_shot(shot);
  }
}
```

### Pattern 4: AI As Virtual Input, Not Special Movement Code

**What:** Zombies choose a desired `joy/fire` command server-side, then pass through the exact same movement, collision, shot, and respawn code path as humans.

**When to use:** Always for AI slot replacement.

**Trade-offs:** AI looks simple, but it avoids a second movement system and removes a whole class of desync bugs.

## Data Flow

### Local Player Flow

```text
[Atari stick/trigger sample]
    ↓
[Canonical input + input_seq]
    ↓
[Send DELTA to server]
    ↓
[Apply same input locally to predicted_local]
    ↓
[Render predicted local wizard immediately]
    ↓
[Receive snapshot with authoritative local state + last_applied_input_seq]
    ↓
[Set authoritative_local]
    ↓
[Drop acked inputs from ring buffer]
    ↓
[Replay remaining inputs]
    ↓
[If residual error is small: ease back over a few frames]
[If residual error is large: hard snap at cell boundary]
```

### Remote Actor Flow

```text
[Server snapshot]
    ↓
[Update authoritative target cell/facing for remote slot]
    ↓
[Client stores target]
    ↓
[Renderer advances remote actor toward target at Maze War animation cadence]
    ↓
[If target drift exceeds guard threshold: snap remote actor to target]
```

### Projectile Flow

```text
[Client presses fire]
    ↓
[Input DELTA reaches server]
    ↓
[Server sim uses authoritative player position/facing]
    ↓
[Server spawns shot at cell directly in front of authoritative wizard]
    ↓
[Server broadcasts SHOT active packet]
    ↓
[Clients render projectile only from authoritative SHOT packet]
    ↓
[Server steps shot, applies hit/brick clear, sends SHOT clear or BRICK_DELTA/RESPAWN]
```

### Zombie Replacement Flow

```text
[Empty slot detected on server]
    ↓
[Slot marked zombie-owned in server role mask]
    ↓
[Zombie AI chooses virtual input]
    ↓
[Server simulation advances zombie through normal player path]
    ↓
[Snapshot marks slot as zombie to clients]
    ↓
[Human connects]
    ↓
[Server rebinds slot, clears zombie ownership, keeps authoritative state]
```

### Key Data Flows

1. **Prediction and reconciliation:** client input flows up; authoritative ack and position flow back; replay happens only on the client.
2. **Remote actor smoothing:** server snapshots flow down; only target state crosses into renderer.
3. **Projectile authority:** fire intent flows up; projectile spawn/step/clear flows down from server.
4. **Zombie control:** world state flows into AI on the server; virtual input flows from AI into the same simulation path as humans.

## Suggested Packet Model

Keep the packet set small, but change snapshot semantics:

| Packet | Recommendation |
|--------|----------------|
| `DELTA` | Keep `input_seq`, `pid`, and `joy/fire`. This is the client intent packet. |
| `SNAPSHOT` | Add recipient-specific `last_applied_input_seq` and, ideally, local facing if it can diverge from `joy`. |
| `SHOT` | Keep as authoritative projectile state. Do not let clients originate projectile coordinates. |
| `RESPAWN` | Keep as explicit hide/show event. It is the clean boundary for actor visibility. |
| `BRICK_FULL` / `BRICK_DELTA` | Keep authoritative and one-way from server after validation. |

If bandwidth is tight, add only one byte to snapshot first: `last_applied_input_seq`. That single byte does more for desync repair than more frequent snapshots.

## FujiNet / Atari-Specific Notes

- **Do not rely on transport cleanliness.** The current server already compensates for extra `0x41` framing and swapped bytes seen on FujiNet paths. Keep a byte-stream parser and packet normalizer at the boundary.
- **Keep TX non-blocking and tiny.** `NS_SendByte` can report a full queue. Atari should stage one small packet at a time and retry remaining bytes next poll, exactly as the current code does.
- **Separate parser state from VBI-owned render state.** The current staged snapshot commit is the right idea. Preserve that separation and extend it to reconciliation state.
- **Use client prediction only for the local wizard.** A 6502 can afford one replay ring buffer for one actor; predicting all actors is not worth the complexity.
- **Smooth visually, reconcile logically.** Keep correction decisions in cell space, then let the existing animation system present them over a few frames.
- **Design for packet loss.** FujiNet over UDP plus low tick rates means clients must survive lost shot clear packets, duplicate snapshots, and stale inputs. Sequence checks and idempotent apply paths are mandatory.

## Build Order For Desync Fixes

Fixing order matters. Build in this sequence:

1. **Add recipient-specific input acknowledgement to snapshots.**
   This unlocks real reconciliation. Without it, every other smoothing change is partial.
2. **Unify local player state into `authoritative_local + predicted_local + pending_inputs`.**
   Stop writing snapshot positions directly into the same live state the renderer uses.
3. **Make projectile spawn depend only on authoritative local state at the server.**
   This fixes the visible bullet-origin bug and proves reconciliation is working.
4. **Convert remote players and zombies to pure target/interpolation clients.**
   Remove any remaining local guesswork for non-local actors.
5. **Refactor zombie slots to be explicit server-owned virtual inputs.**
   This avoids human/zombie handoff bugs and keeps the server model uniform.
6. **Only then tune thresholds and smoothing constants.**
   Tuning before ack-based reconciliation is noise.

## Scaling Considerations

| Scale | Architecture Adjustments |
|-------|--------------------------|
| 1 mixed match | Single-threaded server loop is correct and simplest |
| Several concurrent matches | Run one simulation instance per match; keep packet and sim modules reusable |
| Many matches | Add match routing/process isolation before touching the core simulation model |

### Scaling Priorities

1. **First bottleneck:** state correctness under packet loss, not CPU. Fix protocol semantics first.
2. **Second bottleneck:** observability. Add Linux replay/diff tooling before performance work.

## Anti-Patterns

### Anti-Pattern 1: One State Array For Everything

**What people do:** Let network RX, gameplay code, and render code all mutate the same actor position arrays.
**Why it's wrong:** On Atari this creates visible teleports and race-like frame inconsistencies.
**Do this instead:** Keep staged authoritative state, predicted local state, and render-facing state separate.

### Anti-Pattern 2: Client-Originated Projectile Coordinates

**What people do:** Fire bullets from whatever cell the client currently shows.
**Why it's wrong:** Once prediction drifts, bullet origin becomes visibly wrong.
**Do this instead:** Send fire intent only; server computes shot origin from authoritative state.

### Anti-Pattern 3: AI With Special Simulation Rules

**What people do:** Move zombies via direct position writes or custom movement timing outside player simulation.
**Why it's wrong:** Human and AI occupancy stop obeying the same rules, causing impossible states.
**Do this instead:** AI emits virtual inputs and uses the same movement/fire pipeline.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| FujiNet NETStream handler | Byte-oriented send/recv queue with explicit polling | Treat it as a lossy stream boundary; normalize framing before gameplay logic |
| Linux input/render stack | Test harness only | Use it to record authoritative/predicted divergence and replay packet traces |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| Atari `net` ↔ `render` | staged buffers plus VBI commit | Never let packet RX write live render state directly |
| Server `net` ↔ `sim` | canonical packet structs and per-slot input state | Packet weirdness must stop at the net boundary |
| Server `sim` ↔ `ai` | read-only world query plus virtual input output | AI should not mutate world state directly |

## Sources

- Local project brief: `.planning/PROJECT.md`
- Current wire protocol: `doc/protocol.md`
- Current server implementation: `server/main.c`
- Current Atari client implementation: `clients/atari/maze-war.asm`
- NETStream handler API: `ref/netstream_api.md`
- Valve Developer Community networking references: https://developer.valvesoftware.com/wiki/Category:Networking
- Gaffer On Games networking articles: https://gafferongames.com/

---
*Architecture research for: Atari 8-bit Maze War with authoritative multiplayer*
*Researched: 2026-04-07*
