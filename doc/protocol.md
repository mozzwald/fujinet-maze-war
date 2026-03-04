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
| 0x40 | SNAPSHOT    | S->C  | 19   | Authoritative world/player state |
| 0x41 | DELTA       | C->S  | 4    | Client input update |
| 0x42 | SHOT        | S->C  | 6    | Shot state update |
| 0x50 | BRICK_FULL  | S->C  | 51   | Full brick bitset |
| 0x51 | BRICK_DELTA | S<->C | 4    | Brick removed |
| 0x52 | RESPAWN     | S<->C | 6    | Respawn request/event |

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

### 0x40 SNAPSHOT (19 bytes, S->C)

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
```

`flags` bit layout:
- bit0: valid (always 1 in current server)
- bits1..2: recipient `pid` (slot id assigned by server)
- bits3..6: zombie slot bitmask (bit `n` corresponds to slot `n`)
- bit7: reserved

Notes:
- Clients should treat position/joy/score in snapshots as authoritative.
- Scores are raw 0..255 values.

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

Server behavior:
- Client identity is bound to UDP source address/port (slot), not trusted from payload.
- Incoming DELTA is accepted only if payload `pid` (or swapped `pid`) matches that slot.
- DELTA seq is filtered per slot: duplicate or too-old packets are dropped.
- Invalid `joy` bytes (bits 5..7 set or invalid stick nibble) are dropped.

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
- Server broadcasts this when a non-outer-wall brick is destroyed.
- Client may request brick removal with this packet; server validates bounds and
  rejects outer border cells.

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

## Connection and Slot Semantics

- Server tracks clients by UDP source address+port.
- On first packet from a new endpoint, server assigns a slot (`pid`).
- New clients immediately receive a `BRICK_FULL`.
- Client timeout is 60 seconds without packets.

## Gameplay and Timing Semantics

- Authoritative simulation runs server-side.
- Tick rate is configurable (`--tick-hz`, default 10).
- If a human client's input is stale for >500 ms, server forces neutral input.
- Zombie AI can control unoccupied slots (`--zombies`).

## Client Guidance

- Send DELTA input updates.
- Use SNAPSHOT as authoritative state.
- Apply BRICK_FULL once then BRICK_DELTA updates.
- Apply SHOT and RESPAWN events as received.
