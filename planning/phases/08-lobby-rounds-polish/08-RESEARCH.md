# Phase 8 research: rounds, rooms, Lobby, and Atari memory

## Source of requirements

The product and behavior requirements come from
`ref/mazewar_lobby_rounds_implementation_plan.md`. This document reconciles
that proposal with the code now merged into `a8-net-fix`.

Reviewed again 2026-09-11. The numbered plans and `08-PROTOCOL.md` include the
round-boundary, teardown, and memory corrections below. Model recommendations
and end-of-step switching rules are maintained in `08-MODELS.md`.

## Corrections to the reference proposal

1. The shipped realtime path is TCP, not UDP. `server/main.c` listens and
   accepts TCP sockets; both Linux clients use TCP; Atari sets
   `NET_FLAGS=$05`. Every public room URL and validation rule must therefore
   use `tcp://`.
2. Packet IDs `$54`, `$55`, and `$56` are free after the current `$40-$45` and
   `$50-$53` definitions. Reserve `$57` for `LEAVE_ACK` unless implementation
   research finds a stronger reason to carry the acknowledgement elsewhere.
3. The reliable-event stream is server-to-client. `RELIABLE_ACK` is its
   client-to-server acknowledgement, not a general bidirectional reliable
   envelope. A small idempotent `LEAVE_ROOM`/`LEAVE_ACK` exchange is the
   narrowest fit for TCP.
4. `NO_HUMAN_GRACE` is not a mutually exclusive round state. Keep
   `round_state` (`DORMANT`, `PLAYING`, `ROUND_OVER`, `RESETTING`) and a
   separate `no_human_deadline_ms`.
5. HTTPS Lobby publication may block. It must run outside the 10 Hz simulation
   path and be opt-in.

## Current server seams

The current single room is spread across locals in `main()` and helper
arguments: `players`, `shots`, `clients`, `bricks`, `last_input_ms`, packet
sequence, timers, and the listening socket. Room-specific static state also
exists, including respawn/brick echo compatibility queues. Phase 8 first moves
all state that can differ between rooms into `struct room` while leaving only
process lifecycle, configuration, and optional Lobby publishing global.

Each room owns one TCP listener and four client sockets. The process poll set
contains all listeners and every active client. A listener selects a room; no
room ID is added to gameplay packets.

## Round invariants

- The server alone detects a win.
- The first scoring event in the existing deterministic combat processing
  order that reaches `kill_limit` wins; later same-tick combat is suppressed.
- The winning score is clamped to `kill_limit`.
- `MATCH_END` freezes winner, final scores, final occupants/roles, Zombie history,
  and display names for the result presentation.
- Frozen snapshots continue throughout intermission. This keeps TCP alive and
  prevents Atari's approximately 13-second `NET_WAIT_TICK` watchdog from
  firing during the initial 15-second intermission; every accepted frozen
  snapshot resets that watchdog.
- Neutral client heartbeats keep the server's idle timer alive during
  intermission. Gameplay inputs cannot mutate actors, bricks, shots, or scores,
  and discarded prediction is not falsely acknowledged as applied movement.
- A late join during `ROUND_OVER` does not enter a partially synchronized
  game. The server sends current frozen round metadata and the client waits for
  the next `ROUND_START`.
- Full maps, snapshots, actions, and inputs carry round identity. Reveal play
  only after reliable round authorization, matching map application, and a
  matching fresh snapshot. Old readiness is invalidated at the transition.
- `round_id` comparisons use explicit modulo-256 forward/stale rules and
  duplicate `MATCH_END` never restarts animation.

## Participation and names

Keep final active/role masks separate from a sticky historical Zombie mask.
The history answers whether Zombies participated, not who occupies that slot
at the win. A human who replaced a Zombie must still win as a human. Four result
rows represent the final occupants; they cannot represent an arbitrary history
of humans and Zombies that reused those seats.

Freeze four eight-byte names at `MATCH_END`. A later disconnect, Zombie
replacement, NAME clear, or new occupant must not rewrite the displayed final
results. An unnamed human freezes as `WIZARD n`; a final Zombie occupant renders
as `ZOMBIE n`. Define late-join behavior separately because a late client did
not observe the historical NAME stream.

The current reliable sender transmits only its oldest queued event, while
snapshots/maps bypass that queue. Enqueueing ROUND_START beside a direct map
reset is not an ordering barrier. Queue saturation currently silently drops
new events, and NAME's exact length check shares `REL_PKT_MAX`. These are
explicit 08-03 work items, with recovery tests in `08-PROTOCOL.md`.

## Atari code and memory inventory

### Reclaimable game-over code

The net-only main loop at `STRTCN` only calls `NET_POLL`; it never branches to
the old `GAMEOVR`. In the current symbol map, `GAMEOVR=$467C` and
`ENDGOBK=$4776`, so the obsolete block occupies roughly 250 bytes before its
final jump. It is unsuitable for reuse as behavior because it:

- calls `VBIOFF`;
- performs blocking `RTCLOK` and nested delay loops;
- decides and renders the winner locally;
- repurposes all four PM players;
- returns through local `RESTART`.

Remove that routine before adding the new state machine. Reuse useful ideas
only at the primitive level: `EVAPRTE`, existing player frames, palette shadow
registers, and normal text drawing. Record exact XEX segment and symbol changes
in an extended memory-layout test.

### Reclaimable title data

`TITLDISP` begins at `$8000`; `TITLES` begins at `$8060`; `MAZEDAT` begins at
`$81F0`. Source comments already state that this title screen is unreachable
because net-only restart goes directly to `START`. The approximately 400-byte
`TITLES..MAZEDAT` region and the unused title-display-list bytes should be
replaced by the new reachable title/menu/results constants, not retained beside
them.

Keep `HOSTDISP` and `GAME`: both are live. Keep `HOSTSCR`, `GAMESCR`,
`BOTSCRN`, and `SCORE` at `$7000` and preserve their ANTIC page constraints.

### Reuse by lifetime

Large buffers should be aliased only across mutually exclusive states:

- while NetStream is stopped in `ROOM_MENU`, inactive network/gameplay storage
  is a candidate for a 189-byte record overlay, only after proving contiguous
  capacity and that no VBI/IRQ accesses it; scattered frame buffers alone do
  not establish that capacity;
- `HOSTSCR` is the live 40x24 text surface for title, results, setup, errors,
  and room browsing;
- the selected URL must be reduced to validated host + numeric port before
  NetStream state is cleared/reused;
- frozen round names/scores/masks must remain resident through results and
  therefore cannot alias buffers that reliable network receive still uses.

The current symbol map ends named persistent data at `NAMEBUF=$7EA6`, leaving
roughly 337 bytes before `$8000`, but that gap is not permission to allocate
blindly. Add a generated memory report that measures:

- main-code high-water mark below `$7000`;
- persistent data end below `$8000`;
- reclaimed obsolete code/data bytes;
- display-list 1K boundaries;
- screen-buffer 4K boundaries;
- loaded-segment overlap;
- zero-page end, which is already close to NetStream's `$EE` usage.

Do not add more per-player zero-page arrays. Put round/menu state in non-zero
page memory.

## Atari switching seam

The client already defines `NS_END=NS_BASE+3`, implements `NET_ENDC`, and uses
it on restart/failure. Room switching must call that supported path before
ordinary FujiNet SIO. COMMAND-assert shutdown in current firmware is a safety
behavior, not the primary API.

Create `NET_SESSION_RESET` from the existing `NET_STATE_CLEAR` behavior plus
all connection-scoped framing, reliable revision, packet staging, mask,
prediction, reconciliation, shot, respawn, name, watchdog, and display-gate
state. Preserve username, build configuration, and menu selection only.
Implement this in 08-05 before menu/AppKey work; 08-10 integrates and stress-tests
the same path. `NS_EndConcurrent_Impl` in the sibling NetStream handler only
stops serial IRQs/restores vectors/deasserts motor; it does not issue a firmware
network-close command. Verify actual remote EOF and the required close/reopen
sequence. A menu-safe VBI must protect shared `$82/$83` during SIO/NS_INIT;
SEI does not mask that NMI.

## Lobby contract

The current official Lobby client uses creator `$0001`, app `$01`, username
key `$00`, stores a selected server URL under the selected game's type, and
defines a three-byte response header plus fixed 189-byte server records. Treat
this as a versioned external contract and pin the tested schema in protocol
documentation and tests.

The current XEX's embedded NetStream handler does not establish that an N: CIO
handler is resident. 08-09 must select direct network-device SIO or deliberately
package a compatible handler. Default AppKey reads need the two-byte count plus
up to 64 payload bytes; reserve 67 bytes when adding a local terminator, and
pin the chosen firmware mode. Validate the external Lobby boot artifact in QA,
including any required disk/container, before production publication.

Maze War needs a registered production game type before production promotion.
QA may use an explicitly documented temporary value only if the QA service
owners approve it. Production host, port range, appkey, client URL, and Lobby
base are build/deployment configuration.

## Initial supported limits

- Four seats per room.
- `kill_limit` 1..10. Higher values wait for a multi-digit active HUD.
- `max_zombies` 0..3.
- Initial intermission 15 seconds: about five seconds of winner dance plus
  vaporize/fade time, followed by at least eight seconds of completed results.
- Initial no-human grace 60 seconds.
- Room count is configuration, constrained only by ports/file descriptors and
  test coverage, not by packet format.
