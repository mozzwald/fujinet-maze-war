# 08-02 — Isolated multi-room TCP server

Status: implementation complete and automated validation green; awaiting the
user's real Atari/FujiNet plus emulator acceptance before ROOM-01 and ROOM-02
are marked complete.

## Atari port-selection follow-up

The first checkpoint could not split clients across room ports because the
Atari host editor deliberately accepts hostname characters only and its key
path cannot enter a colon. The startup screen now has separate `HOST`, `PORT`,
and `NAME` fields. `PORT` defaults to 9000, accepts up to five decimal digits,
validates 1 through 65535, and converts the chosen value into the register byte
order already used by `NS_INIT`. Invalid values keep the user on the port field
with a range message. This leaves the hostname filter and gameplay protocol
unchanged.

## Server ownership

The single-room globals and `main()` locals are now one heap-allocated `struct
room` per listener. A room owns its four client slots and their partial-write
and reliable queues, players, shots, live and reset brick maps, input timing,
packet sequence, echo state, Zombie configuration, fixed tick deadline,
broadcast timers, and reserved round/grace fields. Gameplay helpers that read
or mutate this state take the owning room explicitly.

The process creates one nonblocking TCP listener per room and polls all room
listeners and clients together. It services at most one bounded read per peer
and one accept per listener per pass, validates the fd captured in the poll
table before applying an event, and advances every due room once before any
room can advance again. A stalled or failed client therefore affects only its
own connection queue; slot/fd reuse cannot inherit a stale poll event.

Startup is atomic. If any configured listener fails, all previously started
listeners and client state are closed before exit. Shutdown also walks every
room and connection.

## Configuration and compatibility

The old default and `--port PORT` behavior remain one room on port 9000 with
the same tick, Zombie, brick-map, and lag-test defaults. Multi-room commands add
`--room-count`, `--port-base`, and an exact comma-separated `--room-zombies`
list. Invalid counts, port overflow, incomplete Zombie lists, and ambiguous
`--port`/`--port-base` or `--zombies`/`--room-zombies` combinations fail before
opening listeners. Gameplay payloads did not change; listener port is the room
identity.

## Verification

- The existing one-room gameplay and TCP suite passes against the refactor.
- A two-room live test checks different Zombie masks and names, removes the
  same mapped brick in only one room, and confirms the two periodic full maps
  remain different.
- The test leaves a flooding room-0 client unread and verifies room 1 keeps its
  20 Hz snapshot budget, then disconnects and reconnects through slot/fd reuse
  while room 1 continues.
- CLI validation covers room limits, port overflow, conflicting compatibility
  forms, malformed per-room Zombie lists, invalid tick rate, and invalid lag.
- The two-room isolation test passes under AddressSanitizer and
  UndefinedBehaviorSanitizer.

## Required user checkpoint

Run the default one-room command with the existing Atari/FujiNet plus emulator
pair and confirm normal movement, combat, names, HUD, Zombie handoff, and brick
updates are unchanged. Then run two rooms on consecutive ports, place one
client in each, and confirm activity or disconnects in one room do not appear
or stall the other.

After acceptance, use **08-03 — `gpt-6-astra`, high reasoning** for the
authoritative match-end and round-reset synchronization contract. This
recommendation remains conditional until the 08-02 mixed test passes.
