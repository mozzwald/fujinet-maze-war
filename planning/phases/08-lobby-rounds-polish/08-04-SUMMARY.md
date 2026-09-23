# 08-04 — Nonblocking Atari round-end presentation

Status: complete and hardware-accepted on 2026-09-12.

## Implemented behavior

The Atari client now turns the first accepted `MATCH_END` into a VBI-owned,
nonblocking presentation:

`BEGIN -> LOSER_EVAP -> WINNER_DANCE -> WINNER_EVAP -> FADE -> RESULTS -> WAIT_START`

`NET_POLL` remains the only main loop. Reliable ACKs, receive parsing,
watchdog refresh from frozen snapshots, and the neutral DELTA heartbeat
continue while the VBI advances one bounded presentation step. The VBI checks
the authoritative round phase before applying respawns or normal actor
movement, and the first presentation step discards old-round respawn latches
and clears every drawn shot.

`MATCH_END` freezes active and Zombie masks, historical Zombie participation,
round ID, winner, kill limit, four names, and four binary scores. A duplicate
reliable delivery cannot restart an active/completed presentation. Active,
visible losers use the existing `EVAPRTE` mushroom cloud; dead, absent, or
invalid-position actors are skipped. The winner is repositioned to the latest
authoritative cell, rotates through the existing four directional still
frames for about five seconds, and then uses the same vaporization primitive.
The 16-bit timer uses OS `PALNTS` so it is 300 frames on NTSC and 250 on PAL
without changing gameplay cadence.

The fade reduces only the eight palette shadow registers from `PCOLR0` through
`COLOR3`, one luminance step every four frames. DLI setup and the unused DLI
routine were removed; `NMIEN` stays `$40`. Presentation sound writes only
POKEY channels 1 and 2, leaving NetStream's channels 3 and 4 untouched.

After the fade, the VBI clears `HOSTSCR`, selects the ROM charset and
`HOSTDISP`, and publishes the completed text screen. It distinguishes a human
win over Zombies (`NAME BEATS ZOMBIES`) from human-only and Zombie wins
(`NAME WINS`). Server-frozen slot-qualified fallbacks keep duplicate Zombies
clear. Each active final occupant gets a row with slot, eight-character name,
and a two-digit score. A late join that has no live board skips directly to
this screen.

The screen remains visible after `ROUND_START` while the new reliable map and
fresh snapshot gate fills. Restore clears effect/shot state and stale respawn
latches, silences channels 1/2, restores palette shadows, and leaves PM output
off. The existing first-ready VBI then restores the embedded game charset,
`GAME` display list, player/missile graphics, HUD color swatches, and black
border together. No local timer starts a round.

The SDL client now freezes the final active mask and Zombie history and shows
the same outcome wording, final participant names, two-digit scores, and
`NEXT ROUND` status in a larger synchronized overlay.

The server's default intermission is 15 seconds. On both NTSC and PAL this
leaves a little over eight seconds for the completed result screen after the
five-second dance, both vaporization passes, and palette fade. Explicit
`--intermission-ms` values remain available for short protocol tests and
custom rooms.

## Memory layout

- Fixed base core ends at `$6EEB`, below the `$6F00` guard.
- Reclaimed title/result allocation is fully used at `$8042-$81EF`; `MAZEDAT`
  remains fixed at `$81F0`.
- The isolated high-code reserve was extended by one page to `$8400-$88FF`;
  presentation code ends at `$88F1`.
- Persistent NetStream/result state ends at `$7EE5`, below the `$7F00` guard.
- Zero page remains unchanged at `$00E9`; no new per-player zero-page data was
  added.
- Display buffers remain `$7000-$7707` and do not overlap a loaded segment.

`tests/memory_layout_smoke.sh` enforces these boundaries. The extension stays
in the same RAM region already used by this XEX's fixed `$8000` display/maze
data and remains below the `$A000` BASIC ROM window. It does not move the
handler, PM graphics, display buffers, display lists, maze, or charset.

## Verification completed

- `make all` builds the Atari XEX/NetStream XEX, server, ncurses client, and SDL
  client without warnings or assembler errors.
- The complete repository `make test` suite passes, including ten repeated
  authoritative rounds, late join, stale-round rejection, multi-room tests,
  memory bounds, DLI/sound guards, shot cleanup, respawn, and slot lifecycle.
- New `round_presentation_smoke.sh` guards the VBI-before-respawn branch,
  atomic frozen-result publication, complete state chain, PAL/NTSC timer,
  NetStream POKEY isolation, authoritative interruption/restore, DLI removal,
  and SDL frozen-result parity.
- Atari800 executed the assembled `RP_SHOW_RESULTS` routine with injected
  frozen state. `HOSTDISP` decoded as `ROUND OVER`, `ALPHA BEATS ZOMBIES`, four
  active rows with `05/02/04/01`, and `NEXT ROUND`, using the ROM charset with
  `NMIEN=$40` and player/missile output disabled.
- The managed BIN-loader setup cannot exercise the embedded NetStream handler
  as a normal connected game, so effect timing and authoritative restart still
  require the physical checkpoint below.

## Hardware acceptance

The user tested the final timing and round outcomes and confirmed:

1. all active losers vaporize, the winner turns/dances for about five seconds,
   then the winner vaporizes and the maze fades;
2. the result screen is stable and readable, with the right human/Zombie
   wording, participant names, and two-digit final scores;
3. the next server round replaces the result screen only after its map and
   actors are ready, with sprites, bullets, HUD missiles, colors, and sound
   restored;
4. repeated rounds have no flicker, black screen, stale sprites/bullets,
   reconnect, movement, or network regression;
5. both humans and Zombies can win correctly.

ROND-03 and 08-04 are complete. The user switched to the recommended
**08-05: `gpt-5.6-sol`, high reasoning** configuration before continuing.
