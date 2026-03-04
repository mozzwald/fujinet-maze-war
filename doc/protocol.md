# Maze War UDP Protocol (v1)

This document describes the current packet protocol used by the server, Linux client, and Atari client. The protocol is intentionally compact and UDP-based with sequence numbers.

## Overview

- Transport: UDP
- Endianness: byte-wise, no multi-byte integers
- Sequence: 8-bit `seq` for ordering/duplicate detection
- Players: 0..3 (MAX_PLAYERS = 4)
- Playfield: 20 columns (x=0..19), 19 rows (y=0..18)

## Packet Types

| Type | Name         | Dir        | Size | Description |
|------|--------------|------------|------|-------------|
| 0x40 | SNAPSHOT     | S -> C     | 19   | Authoritative state for all players + scores |
| 0x41 | DELTA        | C -> S     | 4    | Input update for one player |
| 0x42 | SHOT         | S -> C     | 6    | Shot position updates |
| 0x50 | BRICK_FULL   | S -> C     | 51   | Full brick layout (bitset) |
| 0x51 | BRICK_DELTA  | S <-> C    | 4    | Single brick destroyed |
| 0x52 | RESPAWN      | S <-> C    | 6    | Respawn event (pending or final) |

## Common Encoding

### Joystick (1 byte)

```
bits 0-3: stick direction (Atari STICK encoding)
bit 4   : trigger (1=pressed, 0=not pressed)
bits 5-7: reserved (0)
```

Stick encoding matches Atari STICK0 (active-low). Neutral is `0x0F`.

### Coordinates

- `x`: 0..19
- `y`: 0..18

## Packets

### 0x40 SNAPSHOT (19 bytes)

Authoritative state for all players plus scores. Sent at fixed rate (default 10 Hz).

```
[0]  type  = 0x40
[1]  seq
[2]  flags (bit0=valid, others reserved)

[3]  p0_x
[4]  p0_y
[5]  p1_x
[6]  p1_y
[7]  p2_x
[8]  p2_y
[9]  p3_x
[10] p3_y

[11] p0_joy
[12] p1_joy
[13] p2_joy
[14] p3_joy

[15] p0_score
[16] p1_score
[17] p2_score
[18] p3_score
```

Notes:
- Clients should treat this as authoritative.
- Scores are simple 0..255 values (not BCD).

### 0x41 DELTA (4 bytes)

Client input update for one player.

```
[0] type = 0x41
[1] seq
[2] pid (0..3)
[3] joy
```

### 0x42 SHOT (6 bytes)

Server shot update for a player. Sent every tick while shot is active.

```
[0] type = 0x42
[1] seq
[2] pid (0..3)
[3] x
[4] y
[5] active (1=active, 0=inactive)
```

When `active=0`, clients should clear the shot for that player.

### 0x50 BRICK_FULL (51 bytes)

Full brick layout bitset (20x19 = 380 bits -> 48 bytes).

```
[0]  type = 0x50
[1]  seq
[2]  flags (bit0=full)
[3]..[50] brick bitset (48 bytes)
```

Bit order:
- Row-major by `y` then `x`
- Bit 0 corresponds to `x=0`
- Bit 7 corresponds to `x=7`
- Next byte continues with `x=8`

### 0x51 BRICK_DELTA (4 bytes)

Single brick destroyed event.

```
[0] type = 0x51
[1] seq
[2] x
[3] y
```

### 0x52 RESPAWN (6 bytes)

Respawn event. Used both for request and server broadcast.

```
[0] type = 0x52
[1] seq
[2] pid
[3] x
[4] y
[5] flags
```

Flags:
- bit0: pending respawn (player is dead/inactive)
- bit1: final spawn (server has chosen spawn point)
- bit2: reserved (future)

Current server behavior:
- On hit, sends pending respawn with `flags=0x01`.
- After 2 seconds, sends final respawn with `flags=0x03`.

## Server Behavior Summary

- Authoritative positions and scores.
- Bricks are authoritative on the server (full bitset + deltas).
- Shots move one cell per tick; hit bricks or players.
- Respawn delay = 2 seconds.
- Zombie AI runs on server; zombie slots are the highest player indices.

## Client Behavior Summary

- Send DELTA input (0x41).
- Apply SNAPSHOT as authoritative state.
- Render bricks from BRICK_FULL/DELTA.
- Render shots from SHOT packets.
- Hide player on pending respawn (flags bit0).

