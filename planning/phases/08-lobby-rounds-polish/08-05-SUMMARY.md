# 08-05 — Leave, no-human grace, and shared session teardown

Status: implementation complete on 2026-09-12; the first mixed real
Atari/FujiNet and emulator checkpoint passed leave/rejoin in two simultaneous
rooms, both with and without Zombies. Two join presentation/placement defects
were found and fixed afterward, so final acceptance is pending a focused
retest. GRCE-01, SWCH-01, and plan 08-05 remain open until that checkpoint
passes.

## Implemented server behavior

`LEAVE_ROOM ($56)` and `LEAVE_ACK ($57)` are two-byte COBS/CRC frames carrying
an echoed leave sequence. The authenticated sender socket is the only identity.
The server queues the ACK, removes the client from its gameplay slot, resets
that slot's score/input/shot/name ownership, and makes the seat reusable in the
same poll pass.

A released socket moves into one of four bounded, seatless departing transport
objects for at most one second while queued output drains. Those objects accept
only duplicate leave frames and repeat their ACK; they cannot mutate gameplay
or refer to a reused slot. ACK completion, EOF, error, or deadline closes the
socket. Plain EOF, socket failure, handshake timeout, and 15-second silence
timeout remain unexpected departures.

Rooms now start clean and dormant. A valid first HELLO activates the room and
anchors a clean round. Voluntary departure of the final human resets directly
to a dormant next round without grace. Unexpected loss of the final human
starts the independent 60-second `no_human_deadline_ms`, immediately backfills
configured Zombies, and preserves the current round, brick map, actors, and
Zombie schedules. A human HELLO during grace cancels the deadline and receives
a fresh seat and zero score in the preserved round. Expiry resets the room once
and returns it to dormant state.

Grace does not replace `ROUND_PLAYING` or `ROUND_OVER`. If the final human is
lost during results, the unobserved intermission is resolved immediately while
the original grace deadline remains unchanged. In-flight shots are cleared as
the room becomes human-free and existing Zombie AI still targets humans only,
so a Zombie-only grace window cannot generate a synthetic victory.

`--no-human-grace-ms` supports short deterministic tests; the production
default is 60000 ms. Dormant wake-up gives the joining client one full server
tick to deliver its first input and rebases repair timers so an empty NAME
rotation cannot overtake the client's initial name.

## Client teardown

On Atari, OPTION begins the temporary direct-connect leave flow. Gameplay,
name transmission, prediction, and pending input stop; inbound parsing and
reliable ACK service continue. The client sends one `$56` frame, accepts only a
matching `$57`, and returns to host/port/name setup after the ACK or a PAL/NTSC
one-second bound. Phase 08-10 can invoke the same state machine from its room
menu.

`NET_SESSION_RESET` clears the connection-scoped framing, TX, reliable,
prediction/replay, snapshot/map/readiness, name/role, shot/respawn, watchdog,
round/presentation, and actor/render transients used by a discarded session.
It preserves the typed host, port, username, validated port bytes, and future
menu selection storage. Leave, failed join, and reconnect all return through
START and this shared path; authoritative round-only reset remains separate.

Both Linux clients send the same leave exchange on normal quit and wait at most
750 ms. A dead server therefore cannot trap their shutdown.

## Checkpoint follow-up: safe and immediately visible joins

The first hardware checkpoint confirmed that voluntary leave and rejoin work
on real Atari/FujiNet and emulation, in parallel rooms configured both with and
without Zombies. It exposed two defects in the reused-seat path:

- A vacant seat retained its previous coordinates. Because vacant actors do
  not participate in collision, another actor could enter that cell and the
  next client assigned to the seat would overlap it.
- Atari `NET_VACANT_UPDATE` cleared the dead/erase masks when a seat filled but
  did not request a player/missile redraw. The joining name therefore appeared
  in the HUD while the erased sprite stayed absent until movement changed its
  render state.

A completed gameplay HELLO now distinguishes an in-place live-Zombie handoff
from a genuinely vacant or hidden seat. Live Zombie takeover preserves the
actor cell; every other join uses `pick_spawn()` before marking the slot
occupied. Every gameplay join also broadcasts and reliably queues an immediate
final `RESPAWN`, including an in-place handoff. On Atari, vacant-to-filled now
sets `NET_REDRAW_MASK`, so the VBI places the PM actor even when its coordinates
match the latest snapshot.

`join_spawn_smoke.sh` makes the overlap deterministic in an open maze: one
client leaves, the observer walks onto the vacant seat's historical cell, and
the seat is rejoined. It requires a distinct authoritative spawn, an immediate
final `RESPAWN` on the observer, matching snapshot coordinates on the newcomer,
and the Atari redraw request. Existing respawn/reliable tests were updated to
recognize the new join-time final spawn as session baseline traffic.

## FujiNet close audit

The vendored handler's `NS_EndConcurrent_Impl` restores IRQ vectors, disables
serial interrupts, and deasserts MOTOR, but does not issue a firmware close.
Current FujiNet firmware stops NetStream and closes TCP when COMMAND is next
asserted. Atari teardown now installs the menu-safe VBI, calls `NS_END`, then
issues the harmless `$70/$3F` Fuji high-speed-index SIO query. The command edge
performs firmware cleanup before another `NS_INIT`. `NET_FW_OPEN` tracks the
interval after successful firmware initialization so partial setup is cleaned
even before the concurrent-handler active flag is set. Both RESTART and
NET_HOSTRET establish the safe VBI before this ordinary SIO transaction; SEI is
not treated as protection against VBI/NMI use of `$82/$83`.

## Memory and verification

- Fixed base core ends at `$6EDA`, below the `$6F00` guard.
- Persistent NetStream state ends at `$7EED`, below `$7F00`.
- Session/leave code extends the isolated high-code segment to `$8A37`; the
  enforced reserve is now `$8400-$8AFF`, still below the `$A000` BASIC window.
- Zero page remains `$00E9`, and display buffers remain clear of loaded code.
- `leave_grace_smoke.sh` exercises duplicate voluntary leave, matching ACK,
  immediate close and dormant reset, unexpected EOF grace, reconnect
  cancellation, and expiry against the real framed TCP server with a shortened
  grace interval.
- `session_teardown_smoke.sh` verifies the Atari state ordering, bounded leave,
  ACK decoder, shared reset coverage, menu-safe firmware SIO close, and Linux
  parity.
- The complete 42-test `make test` suite passes, including the established
  gameplay, round, reliable-event, room-isolation, transport, rendering, and
  memory tests.

## Required hardware checkpoint

1. Join a room on a real Atari/FujiNet, press OPTION during play, and confirm
   host/port/name setup returns within about one second. The server should log
   `client left`, release the seat immediately, and avoid no-human grace for a
   voluntary final leave.
2. Rejoin the same room, leave again, then join another configured room without
   rebooting Atari or FujiNet. Confirm the server sees prompt EOF each time and
   no stale map, actors, names, shots, result screen, or sound survives.
3. With another client present, leave and confirm its view promptly changes the
   seat to a configured Zombie or vacancy and another human can take the seat.
4. Cause an unexpected final-human loss by closing an emulator or interrupting
   the client without OPTION. Reconnect within 60 seconds and confirm the same
   round/map remains and the server logs grace cancellation. If practical, use
   `--no-human-grace-ms 5000` to confirm expiry creates a clean next round.
5. Repeat leave/rejoin several times on real hardware and confirm no hang,
   flicker, input leak, stale reliable event, or need to power-cycle FujiNet.
6. With two clients visible, leave and rejoin a seat several times. Confirm the
   joining sprite appears immediately with its HUD name, before it moves, and
   never shares a cell with another live actor. Exercise both a vacant seat and
   a configured Zombie takeover if practical.

After acceptance, mark GRCE-01, SWCH-01, and 08-05 complete. The conditional
next recommendation is **08-06: `gpt-5.6-terra`, medium reasoning** for
generated build configuration and the title/direct-connect UI. Pause for the
user's model switch before starting it.
