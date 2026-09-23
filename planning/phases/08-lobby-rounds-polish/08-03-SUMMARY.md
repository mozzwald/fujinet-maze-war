# 08-03 — Authoritative round protocol and reset

Status: complete and hardware-accepted 2026-09-12. Round, reconnect,
sprite-redraw, shot-lifecycle, and idle-sound behavior passed mixed real
Atari/FujiNet and emulator testing.

## Versioned round contract

Every connection now begins with `HELLO` protocol version 1. The server sends
`WELCOME` with the current round, phase, and kill limit, rejects malformed or
incompatible clients before they occupy a gameplay seat, and sends no gameplay
state before the handshake. All gameplay packets carry a modulo-256 round ID,
so delayed traffic from an earlier round cannot mutate current state.

The Atari sends only HELLO until WELCOME and retries it at the regular 10 Hz
transmit cadence. The server ignores valid non-HELLO pre-handshake frames while
still rejecting an explicit malformed or incompatible HELLO. This repairs a
FujiNet-PC localhost reconnect loop in which its host TCP socket could be
replaced after the first HELLO, leaving NAME or DELTA as the first frame on the
replacement connection and causing the strict server to close it repeatedly.

Reliable packets now include tagged full-map baselines and the two round
events. ACKs beyond the highest revision sent are rejected, and a full reliable
queue disconnects only the stalled peer instead of silently dropping required
state. The largest reliable map frame, including CRC, COBS overhead, and
delimiter, remains within the Atari 60-byte receive buffer.

## Authoritative match end and reset

The first score mutation reaching the validated 1..10 kill limit is clamped
and ends combat immediately. The server freezes one self-contained
`MATCH_END`: winner, active and Zombie masks, historical Zombie participation,
scores, kill limit, and four normalized display names. Retransmissions and
late joins during the intermission receive the same immutable result.

During results the server keeps transport liveness active but suppresses input
and simulation mutations. At the deadline it increments the round ID, restores
the canonical brick map, zeros scores, retires shots and echoes, clears
respawns and input queues, assigns valid spawns, and retains TCP sessions,
names, seats, and reliable revision streams.

Each client resumes play only after receiving three matching pieces in any
order: reliable `ROUND_START` authorization, a fully applied reliable map
baseline, and a fresh authoritative snapshot. Direct map repair alone cannot
open a round.

## Client behavior

The Atari, SDL, and terminal clients implement the same handshake, epoch
filtering, frozen-result cache, intermission heartbeat, transient reset, and
readiness barrier. Input, prediction, firing, respawn requests, and map changes
remain disabled until readiness is complete. NAME and SEATS traffic cannot
rewrite a displayed frozen result.

Atari keeps its VBI and network loop running with names and scores frozen
during intermission. The temporary `WIN` marker was removed because it
overlapped the shirt-colour missile; the vaporization, fade, and full results
presentation remain 08-04 work. SDL and the terminal client show the round
winner and limit.

Round reset now carries a separate forced-redraw mask into the VBI. After the
first matching authoritative snapshot commits, every occupied, live actor is
repositioned and redrawn even when its spawn cell equals its previous cell.
Previously, snapshot staging overwrote the attempted redraw request with a
coordinate-difference test after the reset had already erased all PM graphics.
That could leave player and Zombie sprites hidden, and the same dead/erase mask
then suppressed bullets and vaporization.

The first mixed retest passed the same-host emulator connection and repeated
round reset on one emulator, one real Atari, and one server Zombie. Bricks,
players, Zombies, scores, and resumed play all behaved correctly. It exposed
one older intermittent Atari artifact: a bullet could remain painted until an
actor walked over its cell.

Atari shot rendering now owns a separate per-slot drawn mask instead of using
the actor's shared `ACTFLAG` byte. Death/effect cleanup can therefore no longer
forget a still-painted projectile. Active authoritative updates also refresh a
60-VBI watchdog, which erases the glyph after about one second if all transient
clear repeats are lost. MATCH_END clears every drawn shot on the next VBI.

The focused real-hardware retest passed: bullets no longer remain stuck on the
screen.

The last pre-08-04 defect was an inherited low-pitched POKEY tone that continued
whenever no intentional effect was playing. It was the original walk shuffle
left latched on channel 1 or 2. Phase 4 reduced `MOVRATE` to one visual phase per
VBI, making the original `CHKTIME` quiet-frame branch unreachable; an even walk
phase or completed move could therefore leave `AUDC=$04`/`AUDF=$20` active
indefinitely. Idle actors and the alternating quiet walk phase now call
`SND_OFF` explicitly. Cold game start, local-seat reassignment, and remote-role
release also clear their game channels so no tone can become ownerless.
`SND_OFF` releases the shared channel-2 claim when it silences it.

The allocator implementation moved into the guarded `$8400..$87FF` high-code
reserve to pay for the added VBI paths without consuming the core's required
`$100` display-buffer margin. Game audio remains confined to POKEY channels
1-2; NetStream retains channels 3-4, `AUDCTL`, and `SKCTL`.

The Atari additions reuse the 08-01 UI reserve at `$8042..$81A4`. The expanded
dispatcher, shot-lifecycle helpers, and sound allocator occupy free RAM at
`$8400..$85E3`, after the maze data ending at `$8377`. Loaded core, display
buffers, zero page,
NetStream state, and maze data remain nonoverlapping and guarded by the
memory-layout smoke test.

## Automated verification

- All server, Atari, terminal, and SDL builds and the full 38-test smoke suite
  pass.
- A live ten-round test checks deterministic match results, a late join during
  results, complete reset, canonical brick restoration, stale-round input
  rejection, recoverable pre-handshake traffic, incompatible handshake
  rejection, invalid reliable ACK handling, and the complete
  authorization/map/snapshot barrier.
- Existing transport, combat, lifecycle, room-isolation, memory, COBS/CRC, and
  client parity checks remain in the full regression suite.
- A shot-render lifecycle check pins independent glyph ownership, active-update
  expiry refresh, VBI-only erase, and immediate intermission cleanup.
- The sound-channel guard now pins cold-start silence, idle and alternating
  walk-phase silence, owner-release cleanup, and the channel 1-2/3-4 boundary.
- A managed Atari800/FujiNet-PC session connected to the real server binary.
  After movement stopped, POKEY reported `AUDC1=0`, `AUDC2=0`, and
  `AUDCTL=$28`; the full smoke suite and memory-layout guard also passed.

## User checkpoint

The same-host connection, mixed round-reset, and focused bullet checkpoints
passed. The final real Atari/FujiNet audio test also passed: the low continuous
grumble is gone while idle, completing the last pre-08-04 regression check.
The interim Atari HUD intentionally has no winner marker; 08-04 owns that
presentation.

Next use **08-04 — `gpt-5.6-sol`, high reasoning** for the interrupt-safe
Atari/SDL round-end presentation. Pause here for the user's model switch before
implementation begins.
