# Maze War Protocol (v1 payloads over TCP)

This document matches current server behavior in `server/main.c`.

## Overview

- Transport: TCP; the compatibility default is one room on port 9000. Atari
  uses `NET_FLAGS=$05`, 57600 baud.
- Endianness: byte-wise, no multi-byte integers
- Players: 4 slots (`pid` 0..3)
- Playfield: 20 columns (`x=0..19`), 19 rows (`y=0..18`)
- Sequence: 8-bit sequence numbers (`seq`)

### Server rooms and ports

One server process can host multiple isolated four-seat rooms. Each room owns
its clients, player/shot state, brick map, input history, packet sequence,
reliable-event revisions and queues, compatibility echoes, Zombie allocation,
  and all broadcast/tick timers. There is no room id in a gameplay payload: the
  TCP listener selects the room. Round-scoped payloads do carry a one-byte
  `round_id` so delayed state cannot cross a reset boundary.

The default command still starts one room on TCP port 9000. `--port PORT` is
the one-room compatibility form. Multi-room hosting uses `--room-count N` and
`--port-base PORT`; room zero listens on the base and each later room uses the
next consecutive port. `--zombies N` applies one Zombie count to every room,
while `--room-zombies 1,2,3` supplies one count per configured room. Counts are
0 through 3. The server rejects an incomplete per-room list, a port range that
exceeds 65535, `--port` with more than one room, and conflicting port or Zombie
forms. If any listener cannot start, it closes every listener already opened
and exits instead of serving only part of the configured room set.

The Atari startup screen collects the hostname and decimal TCP port in separate
fields. The port field defaults to 9000, accepts only digits, validates the
range 1 through 65535, and supplies the selected port to `NS_INIT`. A colon is
therefore not required in the hostname field.

## Packet Types

| Type | Name        | Dir   | Size | Description |
|------|-------------|-------|------|-------------|
| 0x40 | SNAPSHOT    | S->C  | 21   | Authoritative world/player state |
| 0x41 | DELTA       | C->S  | 5    | Client input update |
| 0x42 | SHOT        | S->C  | 7    | Shot state update |
| 0x43 | NAME        | S<->C | 11   | Per-slot display name |
| 0x44 | SEATS       | S->C  | 3    | Which slots a client holds |
| 0x45 | RELIABLE_ACK | C->S | 4    | Cumulative reliable-event ACK |
| 0x46 | HELLO       | C->S  | 2    | Required protocol-version offer |
| 0x47 | WELCOME     | S->C  | 5    | Session/round anchor |
| 0x48 | REJECT      | S->C  | 3    | Incompatible session response |
| 0x50 | BRICK_FULL  | S->C  | 52   | Full brick bitset and round id |
| 0x51 | BRICK_DELTA | S<->C | 5    | Brick removed in a round |
| 0x52 | RESPAWN     | S<->C | 7    | Respawn request/event in a round |
| 0x53 | RELIABLE_EVENT | S->C | 7..56 | Ordered wrapper for reliable events |
| 0x54 | MATCH_END   | S->C* | 43   | Frozen authoritative result |
| 0x55 | ROUND_START | S->C* | 3    | New-round authorization |

`*` means the payload is carried as the inner body of `RELIABLE_EVENT`.

## Framing and Integrity (server -> client)

Every packet in both directions is COBS encoded and followed by a single `$00`
delimiter byte. Inside the encoded frame, the payload carries a two-byte
CRC-16/CCITT-FALSE trailer: polynomial `$1021`, initial value `$FFFF`, low byte
first. The lengths in the table above are payload lengths; on the wire each is
the encoded length plus the CRC and delimiter.


This exists for the Atari. Its receive path is a byte stream over SIO rather
than discrete datagrams, so a single dropped or duplicated byte shifts framing
and payload bytes begin to be read as packet type markers. Bounds checks alone
were not enough on real hardware: corrupt positions placed actors on the border
and the erase pass blanked border cells, corrupt scores flickered, a corrupt
BRICK_DELTA cleared a random cell that the 3s map resync then repainted seconds
later, and a corrupt sequence number parked the client roughly a hundred ticks
in the future so every genuine snapshot was dropped as stale for seconds.

COBS is what makes the stream self-synchronising: no zero byte can appear
inside an encoded frame, so the next delimiter is always a frame boundary. The
client buffers bytes until a delimiter, decodes in place, checks the trailing
CRC, and dispatches on the type byte. A byte lost, gained or flipped costs
exactly one frame and the parser realigns immediately -- verified by
tests/cobs_resync_smoke.sh against a literal transcription of the 6502 decoder.

The CRC alone is not enough: it makes a damaged packet fail closed, but a
parser that scans for a type marker stays misaligned until a payload byte
happens to look like one, which is how spurious BRICK_DELTAs and corrupt
positions kept getting through.

The server retains a frame parser per accepted connection across arbitrary
partial or combined reads. Phase 8-3 is a coordinated protocol break: every
client must complete the version-1 handshake and send round-tagged gameplay.

All host sockets use TCP_NODELAY and nonblocking I/O after connecting. Send
queues preserve partial writes; queue overflow disconnects the affected peer
instead of dropping part of a frame or blocking the game tick. Linux clients
retain incomplete receive frames, dispatch all complete frames in order, and
validate CRC-16. Neither TCP nor its CRC protects
the SIO hop between Atari and FujiNet.

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

### 0x40 SNAPSHOT (21 bytes, S->C)

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
[20] round_id
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

### 0x41 DELTA (5 bytes, C->S)

Primary wire format:

```
[0] type = 0x41
[1] seq
[2] pid
[3] joy
[4] round_id
```

Internal canonical DELTA form after normalization:

```
{type=0x41, seq, pid=slot, joy}
```

Server behavior:
- Client identity is bound to the accepted TCP connection (slot), not trusted from payload.
- Incoming DELTA is accepted only if payload `pid` (or swapped `pid`) matches that slot.
- DELTA seq is filtered per slot: duplicate or too-old packets are dropped.
- Invalid `joy` bytes (bits 5..7 set or invalid stick nibble) are dropped.
- A stale or future `round_id` is discarded without advancing the applied-input ACK.
- During intermission, matching neutral DELTAs are liveness heartbeats and do
  not enter the input queue or advance the applied-input ACK.

### Transport Debug Counters

When the server runs with `--debug`, it logs `transport accepted slot=` for each accepted DELTA and `transport summary slot=` every 2000 ms plus on disconnect/shutdown. Summary lines expose these normalization counters:

- `raw_datagrams` (legacy counter name: counts nonempty socket reads on TCP,
  not packets; split or coalesced reads change this value)
- `raw_bytes`
- `delta_primary`
- `delta_swapped`
- `delta_extra_41`
- `delta_resync`
- `drop_bad_joy`
- `drop_stale_seq`
- `accepted_delta`

### 0x42 SHOT (7 bytes, S->C)

```
[0] type = 0x42
[1] seq
[2] pid
[3] x
[4] y
[5] flags
[6] round_id
```

`flags` bit layout:
- bit0: active (`1` active, `0` clear/inactive)
- bits1..2: direction when active (`0=right, 1=down, 2=left, 3=up`)
- bits3..7: reserved

Notes:
- On clear, server sends `flags=0` and may send repeated clear bursts for reliability.
- Clients must treat `SHOT` as server-authored projectile state. Fire remains
  intent-only `DELTA joy` input; clients do not derive projectile origin locally.
- The Atari tracks ownership of painted shot characters separately from actor
  action flags. Each active update refreshes a 60-VBI watchdog; if all repeated
  clear frames are lost, the orphan is erased after about one second. Entering
  frozen results clears every painted shot on the next VBI.

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

### 0x44 SEATS (3 bytes, S->C)

```
[0] type = 0x44
[1] seq
[2] seat mask (bit `n` = slot `n` is held by a connected client)
```

Behavior:
- Broadcast whenever the mask changes, so a join, a drop or a slot handoff
  shows up immediately, and repeated every `SEAT_REPEAT_MS` (1s) because the
  packet is unacknowledged like `NAME`.
- Bits 4..7 are reserved and currently zero.
- This is the only way a client can tell an empty seat from a human who
  happens to be standing still. The snapshot's zombie mask names the slots the
  AI drives; every other slot used to read as a player, so with two zombies and
  one human the HUD still listed four and the board still showed four wizards.
  A slot is in play only when it is a zombie, is in the seat mask, or is the
  client's own slot; one that is not gets no HUD line -- name and score both --
  and no actor drawn on the board.
- Clients must assume their own slot is occupied regardless of the mask, so the
  HUD is right before the first `SEATS` arrives.

### 0x45 RELIABLE_ACK (4 bytes, C->S)

```
[0] type = 0x45
[1] seq
[2] highest applied reliable revision, low byte
[3] highest applied reliable revision, high byte
```

Behavior:
- Sent by clients after applying a `RELIABLE_EVENT`, and also when a later
  revision arrives before the missing next revision.
- The ACK is cumulative: revision `N` means every reliable event through `N`
  has been applied in order.
- A duplicate ACK for the previous revision triggers a rate-limited fast
  retransmit of the unacknowledged stream before the timeout path fires.
- ACKs beyond the highest revision actually sent are rejected. A full reliable
  queue disconnects only that stalled client; the room and other peers keep
  advancing.

### 0x46 HELLO, 0x47 WELCOME, 0x48 REJECT

The first accepted client payload is `HELLO [0x46, version=1]`. Until it
arrives, the socket owns no gameplay seat and receives no snapshots or map.
Valid non-HELLO frames received before the handshake are ignored without
mutating game state; this lets a client recover if its network adapter replaces
the host TCP socket after the first HELLO was queued. The Atari sends no NAME
or DELTA before WELCOME and retries HELLO at its normal 10 Hz send cadence.
The server allows three seconds for this exchange.

An accepted client receives:

```
WELCOME [0x47, version, round_id, phase, kill_limit]
```

`phase` is 0 while playing and 1 during frozen results. An explicit HELLO with
an incompatible version or malformed length receives
`REJECT [0x48, expected_version, reason]` and the connection closes. WELCOME
is the initial modulo-256 round anchor.

### 0x50 BRICK_FULL (52 bytes, S->C)

Full brick layout bitset (`20*19=380` bits => 48 bytes).

```
[0] type = 0x50
[1] seq
[2] flags (bit0=full)
[3]..[50] brick bitset (48 bytes)
[51] round_id
```

Bit ordering:
- Row-major (`y` then `x`)
- For cell `(x,y)`, linear index is `idx = y*20 + x`
- Byte index `idx/8`, bit index `idx%8` (LSB-first in each byte)

### 0x51 BRICK_DELTA (5 bytes, S<->C)

```
[0] type = 0x51
[1] seq
[2] x
[3] y
[4] round_id
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

### 0x52 RESPAWN (7 bytes, S<->C)

```
[0] type = 0x52
[1] seq
[2] pid
[3] x
[4] y
[5] flags
[6] round_id
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

### 0x53 RELIABLE_EVENT (7..56 bytes, S->C)

```
[0] type = 0x53
[1] seq
[2] stream revision, low byte
[3] stream revision, high byte
[4..] inner event payload: NAME, BRICK_FULL/DELTA, RESPAWN, MATCH_END,
       or ROUND_START
```

Behavior:
- Each TCP client has its own ordered reliable-event stream. Revisions start at
  1 for a new connection and are acknowledged with `RELIABLE_ACK`.
- Reliable revisions continue across rounds. Retransmits are byte-identical
  copies of the stored wrapper packet.
- Clients apply only the next expected revision. A gap is not applied; the
  client re-ACKs the highest applied revision so the server can retransmit.
- Clients consume and ACK a valid old-round event to advance the reliable
  stream while suppressing its gameplay effect.
- Direct brick/respawn repeats, name rotation, and periodic full-map repair
  remain. A direct `BRICK_FULL` repairs the current map but cannot authorize a
  new round; the reliable map baseline is part of the reset barrier.

### 0x54 MATCH_END (43-byte reliable inner payload)

```
[0] 0x54  [1] round_id  [2] winner_pid
[3] final_active_mask   [4] final_zombie_mask  [5] kill_limit
[6..9] final scores p0..p3
[10] historical_zombie_mask
[11..42] four frozen, padded 8-byte display names
```

The first score mutation that reaches the configured limit (1..10, default 5)
is clamped and freezes this payload once. Later combat in the same tick stops.
Fallback names are slot-qualified `WIZARD n` or `ZOMBIE n`; unused result rows
are blank. NAME and SEATS updates during intermission do not rewrite the frozen
result. The server continues snapshots and clients continue neutral heartbeats
through the intermission (default 15000 ms). The default allows the Atari's
five-second winner dance and vaporize/fade effects to finish before keeping the
completed result screen visible for at least eight seconds. Test and custom
servers may shorten this with `--intermission-ms`.

### 0x55 ROUND_START (3-byte reliable inner payload)

```
[0] 0x55  [1] round_id  [2] kill_limit
```

At the intermission deadline the server restores canonical bricks, resets
scores, actors, shots, respawns, inputs and echoes, assigns valid spawns, and
increments `round_id`. It retains connections, names, seats and reliable
revision streams. Each client receives reliable `ROUND_START`, a reliable
matching `BRICK_FULL`, and continuing matching snapshots.

Clients clear per-round prediction/transients and open gameplay only after all
three matching pieces have arrived: ROUND_START authorization, fully applied
reliable map, and a fresh snapshot. Arrival order does not matter. Round IDs
use modulo-256 comparison: distances 1..127 are newer, 128..255 are stale, and
equality is a duplicate/current epoch. The handshake anchor makes wrap 255->0
unambiguous within the bounded session and retry lifetimes.

## Connection and Slot Semantics

- Server tracks clients by their accepted TCP socket; the peer address is logged only.
- Display names are per slot and are cleared on handoff (see Slot handoff).
- On accept, server assigns a slot (`pid`); excess connections are closed.
- New clients receive WELCOME and the current reliable round state only after
  a valid HELLO. A join during results receives the frozen MATCH_END and waits
  for the next normal reset barrier.
- Client timeout is 15 seconds without received bytes; a new connection must
  complete an inbound packet within 3 seconds. EOF and socket failures release
  the seat immediately. Both Linux clients send idle keepalives.

### Slot allocation order

- Slot 0 is never zombie-filled; it is the seat the first human takes.
  `--zombies N` fills up to `N` of slots 1..3 that no client currently holds,
  so `--zombies 3` is the configuration in which every slot is always occupied
  by a human or a zombie.
- With a lower `--zombies`, slots beyond that count stay empty until a human
  claims them. The server still holds a position for such a slot, but clients
  neither list it in the HUD nor draw it on the board (see `0x44 SEATS`).
- Humans displace zombies: each new client takes the lowest free slot, and the
  zombie mask is recomputed from the slots clients actually hold.

### Slot handoff

A slot changes hands when a human takes over a zombie seat, or when a human
disconnects or times out and the zombie backfills it. On both transitions the server resets
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

Each reconnect is a new connection and uses the normal free-slot preference.
A cleanly closed old connection releases its slot immediately. An old half-open
connection can still occupy a seat until the silence timeout; TCP is not a
persistent player identity or session-resume mechanism.

## Client Input Model

- Inbound `DELTA` inputs are queued per client and applied **one per tick, in
  order**. The server used to keep only the newest joy each tick and discard
  the rest, so any input arriving between ticks was lost.
- Only consecutive **neutral** keepalives (`joy=0x0F`) coalesce into a
  waiting queue entry. Directional repeats each represent a separate command
  and remain separate, so their ACKs do not discard unapplied predicted steps.
- With an empty queue (or an entry not yet ready under `--lag-ms`), the server
  sets joy to neutral immediately. It does not repeat the previous direction.
- A directional command may move at most one cell when applied, subject to
  collision, respawn, and directional-fire rules. Command arrival rate therefore
  affects movement rate. The Atari NTSC path sends every six display frames,
  matching the default ten server ticks per second; the renderer has enough
  phase capacity to finish the previous cell before the next transmitted
  command grants another predicted cell. PAL pacing still needs a dedicated
  video-rate adjustment rather than pretending the shared packet sequence is a
  simulation clock.
- The per-client input queue holds six entries. Overflow drops the arriving
  command without applying it. Current receive freshness has already advanced,
  and a subsequent cumulative ACK can pass the dropped sequence; clients must
  not assume overflow is repaired by retransmitting that sequence or by local
  pending replay. This is an outstanding contract issue recorded in
  `planning/phases/04-render-state-separation/04-LAG-REVIEW.md`.
- Snapshot `seq` is a shared packet sequence, not a simulation timestamp.
  The current wire format supplies no movement phase or dedicated server tick.

## Gameplay and Timing Semantics

- Authoritative simulation runs server-side.
- Tick rate is configurable (`--tick-hz`, default 10).
- Tick deadlines advance from the previous scheduled deadline with bounded
  overrun recovery, so a late poll iteration does not permanently drift the
  server's 10 Hz cadence.
- Connected humans use the queued-input/empty-queue rule above. The separate
  >500 ms input-staleness check applies only to non-zombie slots without a
  connected client.
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
- Apply the reliable BRICK_FULL baseline for each authorized round, then apply
  matching-round BRICK_DELTA updates. A direct BRICK_FULL may repair the
  current map but does not satisfy the new-round readiness barrier.
- Apply SHOT and RESPAWN events as received.
