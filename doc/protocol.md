# Maze War UDP Protocol (v1)

This document matches current server behavior in `server/main.c`.

## Overview

- Transport: UDP
- Endianness: byte-wise, no multi-byte integers
- Players: 4 slots (`pid` 0..3)
- Playfield: 20 columns (`x=0..19`), 19 rows (`y=0..18`)
- Sequence: 8-bit sequence numbers (`seq`)

## Packet Types

| Type | Name        | Dir   | Size | Description |
|------|-------------|-------|------|-------------|
| 0x40 | SNAPSHOT    | S->C  | 20   | Authoritative world/player state |
| 0x41 | DELTA       | C->S  | 4    | Client input update |
| 0x42 | SHOT        | S->C  | 6    | Shot state update |
| 0x50 | BRICK_FULL  | S->C  | 51   | Full brick bitset |
| 0x51 | BRICK_DELTA | S<->C | 4    | Brick removed |
| 0x52 | RESPAWN     | S<->C | 6    | Respawn request/event |

## Packet Integrity (server -> client)

Every server-to-client packet carries one extra trailing byte: the sum of all
preceding bytes of that packet, modulo 256. The lengths in the table above are
payload lengths; on the wire each is one byte longer.

This exists for the Atari. Its receive path is a byte stream over SIO rather
than discrete datagrams, so a single dropped or duplicated byte shifts framing
and payload bytes begin to be read as packet type markers. Bounds checks alone
were not enough on real hardware: corrupt positions placed actors on the border
and the erase pass blanked border cells, corrupt scores flickered, a corrupt
BRICK_DELTA cleared a random cell that the 3s map resync then repainted seconds
later, and a corrupt sequence number parked the client roughly a hundred ticks
in the future so every genuine snapshot was dropped as stale for seconds.

The client accumulates the sum as it collects a packet and compares it against
the trailing byte. On a mismatch the packet is discarded whole and the parser
resyncs to the next recognisable type marker.

Client-to-server packets are unchanged: they arrive as UDP datagrams with the
kernel's own checksum, and the inbound path accepts several historical DELTA
framings that a length change would disturb.

## Common Encoding

### Joystick (`joy`, 1 byte)

```
bits 0-3: stick nibble (Atari STICK encoding, active-low)
bit 4   : trigger (1=pressed)
bits 5-7: must be 0
```

Valid stick nibbles accepted by server:
- `0x07` right
- `0x0D` down
- `0x0B` left
- `0x0E` up
- `0x0F` neutral

### Coordinates

- `x`: `0..19`
- `y`: `0..18`

## Packets

### 0x40 SNAPSHOT (20 bytes, S->C)

Sent each server tick (default 10 Hz) to each connected client.

```
[0]  type = 0x40
[1]  seq
[2]  flags

[3]  p0_x   [4]  p0_y
[5]  p1_x   [6]  p1_y
[7]  p2_x   [8]  p2_y
[9]  p3_x   [10] p3_y

[11] p0_joy [12] p1_joy [13] p2_joy [14] p3_joy
[15] p0_score [16] p1_score [17] p2_score [18] p3_score
[19] ack_seq
```

`flags` bit layout:
- bit0: valid (always 1 in current server)
- bits1..2: recipient `pid` (slot id assigned by server)
- bits3..6: zombie slot bitmask (bit `n` corresponds to slot `n`)
- bit7: `ack_valid`

Notes:
- Clients should treat position/joy/score in snapshots as authoritative.
- Scores are raw 0..255 values.
- `ack_seq` is the last local `DELTA` sequence authoritatively **applied** for
  the recipient of this snapshot, not merely the latest received. The server
  queues inbound inputs and applies one per tick in order; the ack names the
  entry it applied. Acking a sequence that was received but never applied makes
  the client discard an input it predicted, which is what produced a snap-back
  when turning a corner at speed.
- When `ack_valid` is clear, byte `[19]` must be ignored.

### 0x41 DELTA (4 bytes, C->S)

Primary wire format:

```
[0] type = 0x41
[1] seq
[2] pid
[3] joy
```

Compatibility formats accepted by server:
- `[0x41][pid][seq][joy]` (seq/pid swapped)
- byte-stream form with extra leading `0x41`: `[0x41][0x41][seq][pid][joy]`

Internal canonical DELTA form after normalization:

```
{type=0x41, seq, pid=slot, joy}
```

Server behavior:
- Client identity is bound to UDP source address/port (slot), not trusted from payload.
- Incoming DELTA is accepted only if payload `pid` (or swapped `pid`) matches that slot.
- DELTA seq is filtered per slot: duplicate or too-old packets are dropped.
- Invalid `joy` bytes (bits 5..7 set or invalid stick nibble) are dropped.
- Wire compatibility is repaired before gameplay input mutation; later gameplay code only consumes the canonical DELTA form above.

### Transport Debug Counters

When the server runs with `--debug`, it logs `transport accepted slot=` for each accepted DELTA and `transport summary slot=` every 2000 ms plus on disconnect/shutdown. Summary lines expose these normalization counters:

- `raw_datagrams`
- `raw_bytes`
- `delta_primary`
- `delta_swapped`
- `delta_extra_41`
- `delta_resync`
- `drop_bad_joy`
- `drop_stale_seq`
- `accepted_delta`

### 0x42 SHOT (6 bytes, S->C)

```
[0] type = 0x42
[1] seq
[2] pid
[3] x
[4] y
[5] flags
```

`flags` bit layout:
- bit0: active (`1` active, `0` clear/inactive)
- bits1..2: direction when active (`0=right, 1=down, 2=left, 3=up`)
- bits3..7: reserved

Notes:
- On clear, server sends `flags=0` and may send repeated clear bursts for reliability.
- Clients must treat `SHOT` as server-authored projectile state. Fire remains
  intent-only `DELTA joy` input; clients do not derive projectile origin locally.

### 0x43 NAME (11 bytes, S<->C)

```
[0]    type = 0x43
[1]    seq
[2]    pid
[3..10] name, 8 bytes
```

Behavior:
- Client sends its display name after connecting. The server uses the
  **sender's own slot** and ignores `pid`, so a client cannot rename anyone
  else.
- The server folds the name to the uppercase subset the Atari character set can
  draw (`A-Z`, `0-9`, space, `-`, `.`), drops anything else, and pads to 8 with
  spaces. Treat inbound names as untrusted text.
- The server broadcasts the sanitized name to every client immediately, and
  re-announces one slot per `NAME_ROTATE_MS` (1s) so a lost NAME heals. The
  rotation is deliberately slow and never shares a tick with other traffic:
  running it every tick roughly doubled the inbound packet rate and starved
  `BRICK_DELTA`. Clients re-send their own name every ~2s until they see
  it echoed back for their slot.
- An all-zero or all-space name means unnamed; clients fall back to their
  `WIZARD` label. A slot handoff clears the name with the rest of the slot's
  transient state, so an incoming player never inherits one.
- Both Linux clients speak the same contract: `--name NAME` on either, and the
  SDL client also prompts for it alongside the hostname.
- 8 characters is what the Atari HUD can show: each slot owns columns 4..11 of
  its 20-column line before the score digit at column 15. Zombie slots always
  render `ZOMBIE` regardless of any stored name.

### 0x50 BRICK_FULL (51 bytes, S->C)

Full brick layout bitset (`20*19=380` bits => 48 bytes).

```
[0] type = 0x50
[1] seq
[2] flags (bit0=full)
[3]..[50] brick bitset (48 bytes)
```

Bit ordering:
- Row-major (`y` then `x`)
- For cell `(x,y)`, linear index is `idx = y*20 + x`
- Byte index `idx/8`, bit index `idx%8` (LSB-first in each byte)

### 0x51 BRICK_DELTA (4 bytes, S<->C)

```
[0] type = 0x51
[1] seq
[2] x
[3] y
```

Behavior:
- Server broadcasts this when a non-outer-wall brick is destroyed, then echoes
  it on the next `BRICK_ECHO_REPEATS` ticks (one packet per tick). A single
  lost packet used to leave the wall painted on a client until the next full
  resync seconds later.
- Client may request brick removal with this packet; server validates bounds and
  rejects outer border cells.
- During authoritative combat resolution, a shot that would spawn directly into
  an interior brick destroys that brick immediately and emits `BRICK_DELTA`
  without first emitting an active `SHOT`.

### 0x52 RESPAWN (6 bytes, S<->C)

```
[0] type = 0x52
[1] seq
[2] pid
[3] x
[4] y
[5] flags
```

`flags` bits currently used:
- bit0: pending respawn (player inactive/dead)
- bit1: final spawn position valid
- bit2..7: reserved

Current server behavior:
- On hit: server sends pending respawn (`flags=0x01`), then after ~2 seconds
  sends final respawn (`flags=0x03`) with new `(x,y)`.
- Client respawn request is accepted as packet type/length; server respawns the
  sender's slot and broadcasts final respawn (`flags=0x03`).
- For client requests, payload `pid/x/y/flags` is currently ignored by server.
- Score, death, and respawn transitions remain authoritative server outcomes.
- A player awaiting respawn is off the board: it is skipped by zombie
  targeting, fire evaluation, shot hits **and movement collision**. Its stored
  coordinates still hold the cell it died in, so counting it as an obstacle
  would make that cell an invisible wall for the whole respawn delay.

## Connection and Slot Semantics

- Server tracks clients by UDP source address+port.
- Display names are per slot and are cleared on handoff (see Slot handoff).
- On first packet from a new endpoint, server assigns a slot (`pid`).
- New clients immediately receive a `BRICK_FULL`.
- Client timeout is 15 seconds without packets.

### Slot allocation order

- Slot 0 is never zombie-filled; it is the seat the first human takes.
  `--zombies N` fills up to `N` of slots 1..3 that no client currently holds,
  so `--zombies 3` is the configuration in which every slot is always occupied
  by a human or a zombie.
- With a lower `--zombies`, slots beyond that count stay empty until a human
  claims them. An empty slot still renders as a motionless wizard on clients.
- Humans displace zombies: each new client takes the lowest free slot, and the
  zombie mask is recomputed from the slots clients actually hold.

### Slot handoff

A slot changes hands when a human takes over a zombie seat, or when a human
times out and the zombie backfills it. On both transitions the server resets
the slot's transient state so the new occupant does not inherit the old one's:

- an in-flight shot is retired with the usual three-tick clear burst,
- the display name is cleared, so the new occupant is unnamed until it sends
  its own `NAME`,
- `joy` returns to neutral (`0x0F`), so no inherited facing or movement,
- `score` returns to 0,
- zombie think/move/fire schedules are re-based to the current time.

The actor is **not** moved. Its position is the slot's physical location rather
than stale state, so the wizard becomes a zombie (or vice versa) where it
stands, and other clients see no unexplained jump. A pending respawn is left to
finish through the normal `RESPAWN` path.

Because slot identity is address+port, and FujiNet chooses a fresh source port
each time it reopens a stream, a reconnecting player lands in a **new** slot;
their previous slot persists until the timeout above expires.

## Client Input Model

- Inbound `DELTA` inputs are queued per client and applied **one per tick, in
  order**. The server used to keep only the newest joy each tick and discard
  the rest, so any input arriving between ticks was lost.
- Consecutive inputs carrying the **same** joy are coalesced into the waiting
  queue entry, which only advances its sequence. A held direction or an idle
  keepalive therefore costs no queue depth and cannot push a real direction
  change to the back or add input latency. Only genuine transitions take a slot.
- With an empty queue the server repeats the last applied joy for at most
  `INPUT_REPEAT_MAX` ticks before falling back to neutral, so a dropped or late
  packet does not stop a held direction dead.
- On queue overflow the arriving input is dropped and **not** acked, so the
  client keeps it pending and replays it.
- Measured on a live Atari session: queue depth stays at 0-1 with zero
  overflow, so none of this adds latency in practice.

## Gameplay and Timing Semantics

- Authoritative simulation runs server-side.
- Tick rate is configurable (`--tick-hz`, default 10).
- If a human client's input is stale for >500 ms, server forces neutral input.
- Zombie AI can control unoccupied slots (`--zombies`).

## Combat And World Authority Semantics

The server resolves combat and shared-world state in one same-tick order:

1. Finalize any expired respawns and publish final `RESPAWN` packets first.
2. Select each slot's authoritative input/facing state for the tick.
3. Evaluate fire from the actor's current authoritative position and current
   authoritative facing.
4. If the input is directional fire (`trigger=1` with a cardinal stick
   direction), suppress same-tick movement for that slot.
5. Otherwise allow one movement step if authoritative world and occupancy
   checks permit it.
6. Step already-active shots, then publish any resulting `SHOT`,
   `BRICK_DELTA`, score, and `RESPAWN` outcomes.

Additional rules:
- Fire is intent-only on clients. Projectile origin, direct adjacent hits,
  moving-shot hits, score changes, and brick destruction are all derived from
  server-authoritative state.
- Direct adjacent hits may resolve as pending `RESPAWN` plus clearing `SHOT`
  without requiring a visible intermediate projectile packet.
- `SNAPSHOT` score bytes are authoritative scoreboard output from the server's
  combat resolution.
- `BRICK_FULL` and `BRICK_DELTA` define the shared wall/brick truth used for
  both movement legality and line-of-fire legality.

## Client Guidance

- Send DELTA input updates.
- Use SNAPSHOT as authoritative state.
- Apply BRICK_FULL once then BRICK_DELTA updates.
- Apply SHOT and RESPAWN events as received.
