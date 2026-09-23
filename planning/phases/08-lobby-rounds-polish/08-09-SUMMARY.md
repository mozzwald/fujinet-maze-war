# 08-09 — Atari QA Lobby room browser

Status: complete and accepted on physical Atari/FujiNet and emulation against
the QA Lobby on 2026-09-13. LOBY-02 and plan 08-09 are closed.

## Implementation

The Atari title now enters a room browser when a nonzero Maze War AppKey is
configured and no valid stored room triggers automatic join. With NetStream
stopped, it issues direct FujiNet network-device SIO to the configured QA
`/view` endpoint using binary format 1, AppKey filtering, four-record pages,
and a bounded page index.

The browser reads the three-byte header and one fixed 189-byte record at a
time. It rejects invalid counts, reserved header bytes, partial records, extra
response bytes, malformed fixed fields, mismatched AppKey/game, offline rooms,
impossible occupancy, wrong host/scheme, and ports outside the generated room
range. It uses the established selected-room URL validator and AppKey writer,
so browser and boot-time validation cannot diverge.

The current record buffer aliases `NET_MAP_CELLS` only while the game and
NetStream are inactive. Browser counters and four compact validated ports
alias inactive network state, while visible names and occupancy live directly
in `HOSTSCR`. The build guard allows the reclaimed high-code area only below
the `$A000` BASIC ROM window and checks that the 189-byte scratch remains
inside network state.

Up/down selects a room, left/right pages through at most eight four-record
pages, Return or joystick fire joins, `R` refreshes, and OPTION opens Direct
Connect. Empty and error states remain interactive. A shared response timer
bounds the complete read, each SIO operation has a finite device timeout, and
all fetch exits close the network channel. A selected URL is persisted before
join; a failed AppKey write is reported but does not prevent the current
session from joining.

## Validation completed

The fixture smoke covers zero, one, four/full, and second-page responses plus
offline, wrong AppKey/game/host/port, impossible occupancy, malformed padding,
truncation, extra bytes, and invalid headers. Static and XEX checks cover the
raw ASCII query, direct SIO path, shared validator/writer, buffer aliases,
record extent, page bounds, and high-code ceiling.

An MCP-managed Atari XL and FujiNet-PC queried a controlled HTTP fixture over
the actual `$71` network-device path. It read the 3-byte header and two separate
189-byte records, rendered `FIXTURE NORTH` and `FIXTURE SOUTH` with occupancy
`1/4` and `2/4`, and changed the inverse-video selection with joystick input.
Selecting the second room placed port 9101 in the live NetStream arguments and
wrote exactly `tcp://127.0.0.1:9101` to AppKey 3 before connection.

The user accepted the real QA endpoint checkpoint on physical Atari/FujiNet
and emulation. Room browsing, direct launch, persistence, OPTION routing, and
normal gameplay after joining behaved correctly.

Next: **08-10: `gpt-6-astra`, high reasoning** for full transport, menu,
AppKey, and repeated-switch lifecycle integration.
