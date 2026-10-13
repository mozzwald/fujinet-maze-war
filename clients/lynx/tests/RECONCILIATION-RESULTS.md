# Prediction/server disagreement — 2026-10-13

Hardware reports frequent corner snaps despite pacing/smoothing improvements.
Tests link the actual server admission/movement/snapshot functions with the
actual Lynx protocol module. No firmware or emulator source was changed/built.
The initial investigation made no additional server changes. The subsequent
human-only hardware traces prompted the opt-in input-history extension below;
that extension requires an updated server to provide its protection.

## Reproductions

1. **Duplicate final RESPAWN teleports the moved player.** Direct, reliable and
   re-stamped repair copies each returned the actor to the spawn cell and cleared
   pending inputs, even after a new move/turn. A negative control with the old
   handler fails the moved-player/pending-corner assertion; the fix passes.
2. **A lost older input can be skipped by a newer cumulative ACK.** Right then
   Up from (5,5) predicts (6,4). Dropping Right but admitting Up leaves the server
   at (5,5), because (5,4) is a wall. ACKing Up discards both pending inputs.
   Differential trace: `P0406 S0505 E0406 R0505`. The same delayed corner with
   no loss has zero disagreements. This proves a mechanism, not hardware loss.
3. **Changed remote occupancy alters pending replay.** A new snapshot can put
   a live opponent in an unacknowledged move's destination. Replay then blocks
   that move; this is a legitimate reason for a correction.

The lost-precursor case also reproduces in the actual 6502 ROM in Handy with
300 ms application delay: `P1006 S1105 E1006 R1105 A0109 J0001`, `C0001`.
Prediction (6,16) rejoins (5,17); ACK 09 skipped the missing precursor and one
neutral was still pending. An injected spawn echo was ignored, preserving the
queued corner/evidence. Artifacts `/tmp/maze-suzy-game-sl6_u9xl`.

## Fix and limits

RESPAWN copies now use a short per-slot stamp (time, sequence, flags, cell).
Within a one-second repair horizon, matching copies and older sequence copies
are ignored. Final echoes cannot teleport a moving actor; an old reliable hide
cannot undo a newer placement. Genuine new transitions and clock/sequence wrap
are tested. The short window bounds global eight-bit sequence inference.

Guaranteed deduplication of arbitrarily delayed copies still needs a respawn
generation identifier. The current wire format is unchanged. **No new server
deployment is needed for this fix/trace.**

The cumulative input ACK cannot recover an earlier missing action after a newer
one was admitted. The existing tail retry is insufficient for that case. Stronger
negotiated input/history semantics would be the next protocol fix if hardware
traces support that diagnosis. Smoothing cannot repair that authoritative path.

## Reading the hardware trace

Immediately after a snap, hold **B** and photograph the entire screen. The small
row at y=88 retains the last local snapshot reconciliation:

| Field | Meaning |
| --- | --- |
| P | Predicted cell before applying the snapshot |
| S | Raw server cell, before replay |
| E | Forecast endpoint of the newly ACKed input; FFFF if unavailable |
| R | Cell after discarding ACKed inputs and replaying remaining ones |
| A | High byte: remaining pending count; low byte: applied-input sequence |
| J | Ignored RESPAWN repair copies, across all slots |
| K | High byte: prediction result; low byte: acknowledged input joy |

P/S/E/R use hexadecimal `$YYXX`: `0406` means x=6, y=4. They are model cells,
not interpolated pixels. The lower C remains the local snapshot-correction
count. The trace persists until another correction or game reset. It replaces
the small A/G line; large serial diagnostics and U/S/W/D/Q/F/C/I remain.

Current ROM packs the seven fields without spaces. K result codes are:
0 moved, 1 not ready, 2 off board/respawning, 3 fire, 4 neutral, 5 bounds,
6 brick/wall, 7 live player. Joy low nibbles: 0E Up, 07 Right, 0D Down,
0B Left, 0F neutral; bit 10 is fire. KFFFF means no newly ACKed expectation.
For example, K060E means the client's last forecast blocked Up on a brick,
while K000E means it forecast a successful Up. Differential tests distinguish
stale brick and stale occupancy from a lost precursor using this field.

E differing from S indicates an applied result different from the latest
forecast: missing input or different collision/world state are possible causes.
E=FFFF gives no newly ACKed expectation to compare. Changed P/R with matching
E/S points toward pending replay/context changes. A visual jump without a new C
may instead be rendering or an explicit placement. The trace cannot prove
electrical loss. Include the room and whether the snap followed a respawn.

## Validation and ROM

Full host suite and production server/client differential tests pass. Handy
passes lost-corner trace injection, delayed/burst taps and overlapping turn,
actual local two-zombie server integration, and noisy-ACK AppKey/Lobby startup.
The actual-server local run had zero snapshot corrections in that session;
this is not a WAN or hardware performance result.

The aligned TGI bitmap now precedes its small state variables, removing a
wasted padding page. Font guards and placement checks are retained. There are
**27 unallocated bytes before the full reserved 2 KB stack**, not a 27-byte
stack. Further additions require memory planning; video RAM is not overlapped.

`clients/lynx/build/maze-war-lynx.lnx`: **42,804 bytes**, SHA-256
`0c1b909be7efe7da8f86afd34d03864662643b73f23eba8473352bba6544b0a2`.
Live Lobby and `fujinet.online:5025` retained. Hardware acceptance of this ROM
and attribution of the dominant hardware cause remain pending.
Raw results: [reconciliation.json](results/reconciliation.json).

```sh
bash tests/lynx_reconciliation_smoke.sh
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
python3 clients/lynx/tests/handy_render_smoke.py --reconcile-checks --input-lag-ms 300
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300 --snapshot-burst 3
python3 clients/lynx/tests/handy_server_smoke.py
python3 clients/lynx/tests/handy_startup_smoke.py --ack-noise
```

## Hardware screenshots and remote-remnant fix

User supplied `ref/screenshots/IMG_20261013_063729.jpg` (remote remnants) and
`IMG_20261013_063814.jpg` (B diagnostics). The latter decodes as:

- P1112 / E1112: prior prediction and ACK forecast both (18,17).
- S1012 / R1012: server position and replayed endpoint both (18,16).
- A010E: ACK sequence 0E, one remaining pending input at that correction.
- J0047: 71 ignored respawn copies across all players.
- B0038: 56 malformed received frames; upper C0004: four CRC failures.
- E0003: three observed serial-status changes, not exactly three bad bytes.
- S001C: sticky parity, hardware overrun and framing flags; O0000 does not
  negate hardware overrun (O is the client software-ring counter).
- Lower W0004/D001C/Q0000/C000D/I0002: window 4, ACK age about 467 ms,
  current pending 0, 13 snapshot corrections and two input retries.

The difference between pending=1 in the latched A and current Q=0 illustrates
that the game continued while B was held. The trace remains a real correction,
although a later correction can replace it. Diagnostic F0008 is not gameplay
frame timing. These counters demonstrate receive-path damage but do not prove
upstream input loss or identify the cause of this particular mismatch. Stale
brick/player state can produce the same P/S/E/R pattern. The new K field
separates those prediction outcomes for the next hardware capture.

Remnants regression: a remote's first draw can occur inside an already-buffered
segment. That interpolation branch set its coordinates but not its valid flag,
so the page cache did not remember/erase it. Local actors normally passed
through initial placement and avoided the bug. Marking every interpolated draw
valid fixes both pages. A pixel-level regression failed before this fix and now
passes; Handy verifies cleanup after a diagnostics-triggered cold maze rebuild.
The warm-cache jitter measurement runs separately from that rebuild, since
initial attempts could not sample motion meaningfully while rebuilding. This
fix does not claim to eliminate network or visual jitter.

Removed an unused authoritative-position array and its clears to make room.
Graphics layout checks retain the full stack and 27 bytes before it. Updated
ROM: 42,932 bytes, SHA-256
`a603d9cb63b105a8f539fc3d9f77aa85747d739cfc1e79fc477d8cd935f7371d`,
live fujinet.online:5025. Host suite, actual-server differential cases for
stale brick/player collision, Handy remote cleanup and Handy K trace pass.
No additional server deployment is needed. Hardware acceptance of this new ROM
and a new full B-screen photo after a snap are pending. Raw results are in
[hardware-snap-and-remnants.json](results/hardware-snap-and-remnants.json).

## Human-only room traces and ordered input history

Hardware feedback: snapping persists in the human-only room. These photos were
decoded from upright crops to distinguish the small C/D and pending-count glyphs:

| Photo | P | S | E | R | A | J | K |
| --- | --- | --- | --- | --- | --- | --- | --- |
| IMG_20261013_071000 | 0C06 | 0C04 | 0C06 | 0C04 | 0055 | 0007 | 0007 |
| IMG_20261013_071048 | 0C0C | 0D0C | 0D0C | 0D0C | 02F7 | 0002 | 0607 |

The first has a successful Right forecast at (6,12), versus server (4,12),
with no pending inputs left at the correction. This is consistent with skipped
older input(s); a different server collision state remains possible. The second
has matching applied/forecast endpoints (12,13), but changes the pre-snapshot
prediction (12,12) when two remaining inputs replay. Its ACKed Right forecast
was brick-blocked. That second trace is **not** evidence of an ACKed command
moving to a different cell than predicted. Concurrent human occupancy was not
confirmed when writing this report; human-only excludes zombies, not humans.

Receive counters: first 20 bad frames / 3 CRC failures / raw sticky status 1C;
second 57 bad frames / 4 CRC failures / status 9C (also driver software overflow).
Client-ring O=0 does not negate the driver's overflow. Lower correction counts
are 4 and 9; ACK ages 15 and 35 VBLs. Counters and J are live while B is held;
these photos alone cannot establish when the serial errors occurred, nor that
they caused a particular upstream input to disappear.

Implemented the demonstrated missing-prefix fix: after a mode-1 offer and
confirmation (`0x59`), the Lynx sends every still-pending input plus the new
action in one `0x58` frame, oldest first. A single CRC covers all pairs. Losing
an earlier frame therefore does not let a later turn arrive without that
earlier movement. The server ignores previously accepted sequences, applies
each admitted action once, and stops at the first queue-full rejection rather
than admitting its successors. Existing tick pacing/ACK semantics are retained.
Ordinary DELTAs remain the fallback until confirmation; offers retry once per
second. See [protocol.md](../../../doc/protocol.md) for the exact extension.

This needs the **updated server** for protection. No unsolicited extension
packets go to legacy Atari clients; they need no update. Mode activation cannot
undo a predecessor already skipped by an earlier legacy ACK. Death/round
transitions intentionally reset old prediction. History does not fix changing
player occupancy, unknown wall state or electrical errors.

Also fixed a reproducible collision-state issue: a delayed reliable full map
could restore a brick already cleared by a direct delta. Full maps now AND with
the current map within a round. ROUND_START initializes unknown bits to FF,
so a delta arriving before the first full map is preserved; a new round can
restore its new maze. Rendering remains gated until map authorization.

To fit, temporary HTTP/AppKey response/record buffers share a union with the
gameplay render snapshot/dirty bits. Startup completes before gameplay uses
these bytes; the persistent name, endpoint and room list remain separate.
Graphics placement and full reserved stack checks still pass (SCBs B500, text
B400, 27 bytes unallocated before the full 2 KB stack).

Validation:

- All Lynx host tests pass, including actual transport history encoding and
  unchanged legacy framing/quiet handoff.
- Actual-server/client differential: lost corner precursor repaired with zero
  corrections; duplicate history applies once; six-entry queue pressure,
  sequence gaps/wrap, malformed whole-frame rejection and map ordering pass.
- Existing legacy input-queue, TCP transport, reliable-event and ten-round
  protocol tests pass. The reliable test now enqueues a real rename for its
  next revision, since the earlier optimization made periodic NAME direct-only.
- Installed Handy + actual local two-zombie server: deliberately dropped one
  moving history frame; later history repaired it; zero corrections observed
  in this run, M=1. Artifacts `/tmp/maze-handy-server-1m95jyht`.
- Installed Handy + legacy peer ignoring offers: remote cleanup/jitter and
  diagnostics return pass. Artifacts `/tmp/maze-suzy-game-mdktljlw`.

Current ROM: **43,252 bytes**, SHA-256
`e3cf67484343a4baeb44f8be8e6a41bffdfd4c52cf9ad5ff32017150a8f83a21`,
live Lobby and fujinet.online:5025. Current server SHA-256:
`681a0cace9ddc7fd6cced993ac990af72a4469559e0d86c3003447c051499174`.
B now shows **M0001** after confirmation, M0000 during legacy fallback.
Deploy this server and test this ROM together on hardware; compare corner
snaps and photograph B after any remaining correction. These private emulator
tests are not a real ComLynx electrical or WAN performance measurement.
No firmware changes/builds and no live server deployment performed.
Raw results: [human-room-input-history.json](results/human-room-input-history.json).

## Real hardware acceptance after server deployment

The user deployed the updated server, installed the ROM on real Lynx hardware,
and reported movement is "much better". Photo
`ref/screenshots/IMG_20261013_075132.jpg` confirms **M0001** in the middle
small-text row: input-history negotiation succeeded. The reported changing
M0002/M0004 was the **W** field on the bottom row, the expected adaptive
movement window (2..4); the tiny W and M glyphs can look alike.

The captured session shows lower **C0000**, zero snapshot prediction
corrections, with P/S/E/R/A/K all zero (no correction latched), J0007 ignored
respawn copies, W0004, D0014 (20 VBLs, approximately 333 ms), Q0000 and I0004
input retries. This is a confirmed improvement on hardware, not a guarantee
that all collision-related or future network corrections are eliminated.

Serial receive issues remain: B0041 = 65 malformed frames, upper C0008 = eight
CRC failures, E0004 observed status changes, sticky S009C (parity/framing,
hardware overrun and driver software overflow), and client-ring O0000. The
gameplay improvement therefore does not establish that the electrical or
receive-path problem is solved. B keeps gameplay/counters running, so this
photo cannot date those errors relative to the movement test.

Recorded as an accepted checkpoint with existing host, actual-server/client,
Handy loss-recovery, legacy-protocol and graphics-layout validation. ROM and
server hashes above remain unchanged. Screenshot files remain local/ignored;
their names and decoded findings are recorded here and in the raw JSON.
