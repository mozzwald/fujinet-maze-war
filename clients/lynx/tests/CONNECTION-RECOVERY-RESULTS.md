# Lynx connection recovery and diagnostics — 2026-10-13

## Contract and scope

This is a client change. Firmware, emulator and server sources/binaries were
not modified or rebuilt for this work. Read-only inspection of the existing
ComLynx NetStream firmware established that it reconnects TCP to its configured
endpoint after EOF/failure. The current server requires HELLO on a new socket;
ordinary input traffic cannot reopen a session. The existing LEAVE command
retires a surviving old socket after its bounded drain period.

Recovery therefore stays in raw mode and alternates existing LEAVE/HELLO
messages, at most one every thirty 60 Hz ticks. It never issues another F0,
HTTP or AppKey command. A new WELCOME resets protocol session state and retains
the configured name. A fresh reliable round/map and snapshot are required
before movement resumes. A rejoin may assign a different player seat.

Thresholds use the independent 16-bit VBL clock, with unsigned wrap handling:

| Condition | Behavior |
| --- | --- |
| Initial HELLO unanswered for 10 seconds | TIMEOUT; Option 1 retries |
| Playing snapshot progress stops for 3 seconds | WAITING FOR SERVER; drop pending inputs and pause prediction |
| Applied snapshot returns before 10 seconds | Resume the existing session |
| No progress for 10 seconds | REJOINING ROOM; bounded LEAVE/HELLO exchange |
| Recovery takes 30 seconds | LINK LOST; Option 1 retries the same room |

Metadata alone cannot hide missing playing snapshots or a missing reliable
map. Intermission uses accepted game traffic because the real server stops
snapshots while continuing seat/name traffic. ROUND_START grants the next
bootstrap a new deadline, independent of the previous round's last snapshot.
Initial command-mode handoff time is outside the HELLO timeout.

All transmissions retain the half-duplex RX quiet/partial-frame gate.
Option 1 uses Suzy's raw joystick bit because cc65 joy_read masks Option keys;
it is sampled between drawing batches as well as between frames.

Changing endpoints, returning to Lobby or editing AppKeys after raw mode starts
still needs future firmware STOP support. Current TNFS ROM advertisement is
only a Lobby reference; Lynx FujiNet cannot load ROMs directly into the system.
There is no TNFS download/hash acceptance gate for this client.

## Diagnostics

Read-only inspection of cc65's lynx-comlynx driver established that SerialStat
is sticky until OPEN. E now counts polls introducing any previously unseen
masked bits; S retains the OR mask. E is not a physical error count/rate and
does not count repeated occurrences of a latched flag. G on the U/S/M/G/J line
counts failed get/status API calls; J counts recovery attempts. O remains
application-ring capacity observations, distinct from driver RX overflow in
S bit 80. B/C/F remain frame failures/CRC failures/last frame error.

Host fixtures inject 04, 08, repeated 08, zero and 08 again: E stays 2 and S
stays 0C. Additional unseen bits increment E once for that poll. API failures
increment G without changing E. CRC and ring diagnostics remain independent.

## Reproduction

```sh
make -C clients/lynx test
make -C clients/lynx all SERVER_HOST=fujinet.online SERVER_PORT=5025
python3 clients/lynx/tests/handy_server_smoke.py --connection-checks
python3 clients/lynx/tests/handy_startup_smoke.py --new-name --ack-noise
python3 clients/lynx/tests/handy_render_smoke.py --round-checks --hud-checks
```

Use the installed ~/lynx-pc/handy_sdl and lynxboot.img. Each harness copies the
cartridge into an isolated /tmp directory, uses private Xvfb/config/local
ports and terminates its own processes. The production cartridge retains its
live endpoint; the private command peer checks fujinet.online:5025 and forwards
raw game frames to the local actual server instead. It contacts no live room.
The server test takes approximately two minutes including outage deadlines.

The bridge can drop only snapshots while metadata continues, suppress all
downlink while keeping old TCP alive, discard the first reconnect WELCOME,
and stop/restart the actual server. It reconnects backend TCP like the existing
firmware and discards outbound frames while that socket is unavailable.
Screen assertions decode the editable 4×6 glyphs and B counters. Keep B held
long enough for its redraw to complete before capturing diagnostics.

The host suite covers pause/resume, discarded pending intents, same-room
control-only recovery, fresh bootstrap/name retention, initial/recovery
deadlines, manual retry, clock rollover, intermission/new-round deadline reset,
missing reliable map despite flowing snapshots, sticky/API counters and
existing contiguous-frame/half-duplex tests. Gameplay, motion, cache, death,
round, sound, startup, framing and art suites remain part of the same gate.

## Memory constraints

The baseline was near its MAIN limit. Disjoint foreground protocol/text
temporaries now use APPZP ($2B..$7F), after runtime/IRQ zero-page allocation.
These are not startup-cleared; every value is assigned before use. The owning
functions are non-reentrant and do not invoke callbacks while their scratch
is live. The linker and layout checker bound APPZP and preserve hardware
alignment, literal row guards and the full reserved 2 KB stack. The final
layout leaves 27 bytes before that reserved stack; this is not stack capacity.

## Recorded checks

- Full `make -C clients/lynx test`: passed, including added freshness, missing
  bootstrap, intermission/round transition, recovery/wrap and diagnostics cases.
- Actual server plus Handy, final cartridge: all four fault scenarios passed;
  see [machine-readable results](results/connection-recovery.json). The final
  diagnostic sample was M=0001, G=0000, J=0004. A short snapshot gap kept J=0;
  the four recovery attempts include one-way loss, server restart, a second
  outage exhausting automatic recovery, and the Option 1 retry.
- Handy rounds/HUD: passed smoke, dance, duplicate results, score screen,
  diagnostics restoration and next-round reliable ACKs. This used cartridge
  `7e786c6d77f52c594a7d08f718016f99f1529e6477080ff6093218c0159e768e`,
  before only the final Option 1 sampling relocation; foreground scratch and
  watchdog/round handling were identical.
- Handy startup with first-use name and noisy ACKs: passed. This earlier run
  used cartridge `1b7ab5bc00a3bdba2f906d15cc71e5fd6c08219666f39e28b0e2bebd115ee546`;
  later changes were raw-mode freshness/round deadlines and Option 1 sampling.
- Final live-endpoint build and graphics/zero-page/stack layout gate: passed.
  Final ROM SHA-256:
  `cc62e765a2383c5b513146e01280a9482bbc3728aafb149fa12cfcfb3f7d24f4`.
  Actual tested server SHA-256:
  `45748b8b666a8b9ade21744c9ecc91fe3306706459f72740ccd03e6d2faed50b`.

Local raw evidence directories: `/tmp/maze-handy-server-c_ok6auv`,
`/tmp/maze-suzy-game-cm19gfh1`, `/tmp/maze-lynx-startup-pcyxqub1`.
These are temporary artifacts, not required to reproduce the tests. Earlier
failing test runs exposed and fixed Option-key masking/sampling, the round
transition deadline, screenshot timing and the silent intermission fixture.

## Physical acceptance — user confirmed 2026-10-13

The user confirmed all three interruption/recovery scenarios on real hardware.
They isolated FujiNet's Wi-Fi by wrapping it in conductive fabric as a Faraday
cage, leaving Lynx and FujiNet powered throughout the test.

| Scenario (suggested duration) | User-reported result |
| --- | --- |
| Short interruption (~5 seconds) | WAITING FOR SERVER, then automatic resumption: passed |
| Longer interruption (~15–20 seconds) | REJOINING ROOM, then automatic recovery: passed |
| Recovery timeout (~45 seconds) | LINK LOST; restore connectivity and press Option 1: passed |

Durations above are the suggested procedure, not measured timings. The user's
confirmation did not include diagnostic counter captures, board/firmware
revision details or an independently checked SD-card ROM hash. The supplied
build and its hash are recorded above. This closes hardware acceptance for
these three Wi-Fi interruption scenarios; the broader mixed-client endurance
matrix and extended diagnostic measurements remain separate work.

### Reproduction on hardware

Copy clients/lynx/build/maze-war-lynx.lnx to the flash-cart SD card. The default
room is fujinet.online:5025; Lobby permits the other live room ports. During
play, interrupt network access briefly and confirm WAITING FOR SERVER and
paused movement, followed by automatic resumption. For a longer interruption,
confirm REJOINING ROOM and eventual name/maze recovery without old shots or
movement. After automatic recovery expires, restore connectivity and press
Option 1 to retry the same room. Check B's J attempt count and G API failures;
record E/S as sticky indicators rather than an error rate. A rejoin can change
seat/color. Also cross a normal results screen/new round.

If leaving the game to browse again, stop NetStream through FujiNet's web
configuration or reset FujiNet before restarting Lynx. Resetting only Lynx
does not establish command mode.
