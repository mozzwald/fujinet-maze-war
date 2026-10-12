# Maze War Lynx client (early playable view)

See the [Suzy maze-cache benchmark results](bench/RESULTS.md) for measured
renderer alternatives and the [benchmark guide](bench/README.md) to reproduce
them. The benchmark cartridge is separate from the live client.

This cartridge is an early playable view with a fixed server endpoint and
name. It initializes the cc65 ComLynx serial driver at 62500 baud, 8 data bits, odd
parity, one stop bit; sends FujiNet device `$70` command `$F0` with raw TCP
flag `$01`; and retries the Maze War v1 `HELLO` until it receives `WELCOME`,
`REJECT`, or a bounded timeout. After `WELCOME`, it sends `NAME`, cumulative
reliable ACKs, and paced `DELTA` inputs. The Lynx D-pad maps to cardinal
movement and A fires. Button changes send a `DELTA` immediately; snapshots
pace held-input repeats. The local wizard predicts sent movements and replays
unacknowledged inputs over each authoritative snapshot. A short muzzle cue
appears on A press; hit and score results still come from the server. It
applies snapshots, seats, names, shots, brick maps,
respawns, match results, and round starts. The entire 20×19 maze is visible
at once, with 8×5 textured brick and border cells, colored side-view wizards,
shots, and one compact score line beneath the map. Player slots use the Atari 8
shirt hues in order: green, blue, purple, orange (the closest Lynx palette
entries). Hold B for serial and protocol diagnostics. In that view, `A` is the
applied reliable revision
and `G:0007` means round authorization, reliable map, and fresh snapshot are
all present. `B` counts rejected frames, `C` counts CRC failures, and `F`
shows the last frame-error code (3 means malformed COBS). There is no setup
menu, editable name, lobby, or leave flow yet.

The current Handy preview is [art/handy-full-map-preview.png](art/handy-full-map-preview.png).
It is an art iteration for hardware review, not final graphics approval.

Build with cc65 and run the portable frame and protocol checks:

```sh
make -C clients/lynx test
make -C clients/lynx SERVER_HOST=127.0.0.1 SERVER_PORT=9000
```

The cartridge is `clients/lynx/build/maze-war-lynx.lnx` and the linker map is
`clients/lynx/build/maze-war-lynx.map`. A local `clients/lynx/config.mk` can
hold `SERVER_HOST` and `SERVER_PORT`; it is ignored by Git. Endpoint changes
always rebuild the ROM. The host must be 1–62 ASCII hostname/IP characters;
the TCP port must be 1–65535. For hardware tests, the current local config
builds for `fujinet.online:5025` (the production Lobby's Humans Only room).
Use a separate `TARGET` under `/tmp` when building a localhost Handy ROM so
the hardware-test artifact retains its live endpoint.

Use the local-only
`planning/phases/09-lynx-netstream-transport/09-HANDY-TESTING.md` runbook for
Handy SDL plus FujiNet PC testing. Start a Maze War server on the same port
compiled into the ROM. The first TCP payload must decode as `HELLO [46,01]`.
In the 2026-10-09 emulator run, server port 18101 and FujiNet PC BOIP port
18100 produced `G:0007`, revision 15, no bad frames or serial errors, 15
reliable ACKs, and server-accepted input values `0F` (neutral), `0E` (Up),
`1E` (Up+A), and `1F` (A). This checks the link and input path in Handy;
real ComLynx timing still needs hardware testing.

The first renderer integration run on 2026-10-09 showed the maze, a moving
local player, scores, and a complete B diagnostics screen. Pumping the link
between maze rows restored server input cadence to roughly 8–10 inputs per
second under redraw. A 21-second sample reached reliable revision 24 with
one malformed COBS frame, no CRC failures, no RX-ring overflows, and no
serial status errors. The cause of the occasional malformed frame needs
investigation before calling the display path reliable.

The movement renderer now caches the maze and restores only old player and
shot areas on each of the two display pages. It advances the local wizard by
small pixel steps between server snapshots while checking walls and keeping
within two cells of the authoritative position. The local sprite no longer
teleports for an ordinary large snapshot correction. In an isolated Handy
open-map capture, the original full repaint showed 5 visible position changes
over about 2.4 seconds; the new renderer showed 25 over 1.6 seconds of held
movement, with the sprite present in all 44 sampled frames. Those samples are
roughly 36 ms apart and can skip displayed frames, so their largest observed
jump is not a per-frame step. The server still requires one fresh DELTA per
movement tick; snapshots pace those packets. WAN and real ComLynx feel need
physical testing.
The local wizard now checks its entire 8×5 visual footprint before every
predicted pixel step. This stops the sprite before any part enters a brick,
including at corners and during turns; it can still move a pixel at a time
through open cells. Wizard and muzzle pixels are clipped to walkable maze
cells as a second safeguard. Corrections toward a server position use the
same footprint check. Host playfield tests cover blocked horizontal and
vertical steps, an L-shaped open path, wall pixel preservation, and an
obstructed correction. A 45-frame Handy corridor capture showed movement up
to a wall with zero green sprite pixels on wall cells.
The server position also takes priority when prediction diverges: a new
authoritative cell that moves away from the visual sprite triggers a safe
rejoin when the gap exceeds one cell or conflicts with the held direction.
If a wall blocks a smaller correction, the sprite rejoins at the server's
walkable cell. Tests cover held input, firing from the authoritative area,
and a server move across the held direction.
The firing frame also aligns the local sprite with the server cell, where
the shot actually originates.

For the input pacing change, an isolated Handy/FujiNet-PC run used a local
10 Hz server, an open test map, and six 160 ms Right presses separated by
240 ms releases. A temporary build with the old snapshot-only scheduling
reached the server a median 109 ms after each press; the updated scheduling
reached it in 34 ms. Median press-to-authoritative-application time was
141 ms versus 123 ms in those same runs. The server applied movement and
never reported a full input queue; the updated run's largest observed queue
was two inputs. These are emulator and localhost measurements of one sample,
not one-wire hardware or remote-server latency measurements. Local movement
prediction should improve the perceived response further, but its visible
timing still needs measurement on the physical Lynx.
The updated Handy run's B diagnostics showed 3 rejected frames and 2 CRC
failures, with no serial-status errors or RX-ring overflow. Earlier runs also
showed intermittent frame errors; this remains a real-hardware acceptance
check before changing the input rate again.

The full-map art pass keeps all twenty columns and nineteen rows on screen,
following the Atari 8 client's composition. After the player's October 9
screenshots showed the seven-pixel wizard crossing four-pixel maze rows, the
rows grew to five pixels and the wizard was redrawn to fit one cell. A custom
3×4 score font fits all eight name characters plus a two-digit score for each
of four players on the HUD strip. The CPU tile painter writes only the safe
top 89 pixel rows of page 0. It stages row 89, including the wizard's feet,
for TGI to draw on that page; TGI also draws the exact textured background
for rows 90–94. Page 1 is ordinary RAM and receives all rows directly. A
Handy probe found that CPU writes into `$FC00` disabled TGI drawing, so the
89-row limit is covered by a host regression test. A bottom-row Handy capture
confirmed the wizard's foot pixels on row 89 and both blue shades in the
border, with 19 sampled movement changes and no sprite pixels on walls. A
later 21-second local session showed the
textured full map, side-view wizard, score line, movement,
and fire. It still recorded intermittent frame errors (3 bad frames, with
the latest classified as COBS and 1 counted as CRC; no RX-ring or serial-status
overflow), so link
health remains an acceptance item. Physical Lynx display timing remains to be
checked.

The existing firmware has no Lynx-reachable stop command once raw NetStream
is active. This client does not claim stop/reconnect support; closing its
serial driver or sending a normal `$F0 STOP` packet would leave FujiNet in
raw mode. The protocol module can build `LEAVE_ROOM` and recognize
`LEAVE_ACK`, but the UI does not expose leave while raw-mode teardown is
unresolved. Firmware changes are outside the user's authorization. The
stop/reconnect requirement remains a gate before a complete client.

`src/lynx.cfg` is adapted from the FujiRealm Lynx client's MIT-licensed
linker layout. See [THIRD_PARTY.md](THIRD_PARTY.md) for its notice.
