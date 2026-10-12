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
movement and A fires. Sampled presses are latched and sent between Suzy
batches; a 60 Hz VBL clock paces held repeats independently of snapshots. The local wizard predicts sent movements and replays
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

### Movement and turn control

Each sampled directional press contributes one discrete input, even if it is
released before an outbound slot is available. A four-entry intent FIFO
preserves short taps and quick turns in order. The first step is immediate;
a held direction waits 250 ms before repeating at 100 ms intervals. Late
polling sends one repeat, never a burst to catch up. A new direction is sent
without waiting for the repeat timer. If D-pad directions overlap, the newly
pressed direction wins instead of a fixed Up/Right priority.

Snapshot arrival no longer grants another movement input. Held repeats stop
when two movement inputs are awaiting server acknowledgement, bounding the
old-direction backlog on a slow connection. Explicit taps/turns retain their
order. A full eight-entry replay ring waits for acknowledgements instead of
clearing prediction history. A released button cancels future held repeats;
inputs already transmitted still have to be applied by the server.

The local sprite interpolates only toward the cell predicted by transmitted
inputs, then settles on that cell. It no longer drifts two additional cells
based only on the held direction. This keeps the visible stopping cell aligned
with the cell from which the server will accept a turn. The entire 8×5
footprint is checked against walls and occupied cells; wizard and muzzle
pixels are also clipped to walkable maze cells. If a wall blocks correction,
the sprite rejoins the server's walkable target cell. Firing settles unfinished
interpolation at the predicted shot origin.

The input sampler and bounded transmitter run between Suzy batches and while
waiting for a display swap. Clock ticks come from cc65's VBL counter, so slow
rendering does not turn a 100 ms repeat interval into a frame-count delay.
Host input tests cover latched taps, snapshot/press races, stop, overlapping
turns, stalled acknowledgements, a full replay ring and clock rollover.

The movement emulator mode runs a controlled peer that applies one queued
input per 10 Hz tick and acknowledges only applied inputs:

```sh
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 150
```

The final ROM passed both runs: 60/120/160 ms taps each moved one cell;
release stopped further movement; narrow-column entry and the overlapping
Up-to-Right turn reached their expected cells. See the
[nominal record](tests/results/movement-nominal.json) and
[150 ms delay record](tests/results/movement-delay150.json). Handy's host event handling and
throttling affect short wall-clock key timing; these checks supplement the
clock-controlled host tests and do not prove hardware or WAN latency.

### Death animation

A server death notice replaces the wizard at its last known cell with a
four-stage burst, puff, smoke cloud, and fading wisps. Each stage lasts eight
VBL ticks (about 533 ms total). The pending notice's placeholder coordinates
are ignored. Duplicate notices do not restart the effect; a snapshot can also
start it if the pending notice arrives late. Joining while a player is already
dead produces no effect without a known death location.

The victim immediately stops occupying its cell. Other players can walk
through the animation and draw over it. Only the server's final respawn
placement restores the victim; stale live snapshots cannot restore a blocking
body during the death interval. Local pending movement is cleared on death.

Clouds use the existing Suzy batches, with their previous footprint restored
from maze tiles on each display page. Pixels are clipped around walls and the
HUD. Host tests cover all stages, nonblocking movement, player layering,
duplicate notices, respawn, clock rollover, and cleanup of both pages,
including bottom-row and edge cases. Run the production ROM in Handy with:

```sh
python3 clients/lynx/tests/handy_render_smoke.py --death-checks
```

This uses a controlled local ComLynx peer to check visible effects, walking
through an active cloud, cleanup, duplicate suppression, and final respawn.
It does not exercise FujiNet firmware, the live server, or physical serial
timing. See the [emulator record](tests/results/death-animation.json).

### Earlier timing measurements

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
of four players on the HUD strip.

The production renderer now uses native Suzy SCB chains capped at eight
sprites. Both display pages cache their maze independently; a brick change
patches only that cell, and old player/shot footprints are restored with maze
tiles before transparent actors are drawn. All 95 maze rows, including the
textured bottom border and last-row feet, use the same path. The seven HUD
rows retain their existing TGI drawing and per-page change detection.
The CPU no longer writes the maze bitmap across hardware registers.

Serial interrupts buffer bytes during Suzy drawing; foreground input and
packet polling runs before and after each batch. Each frame copies game state
before drawing so packets received between batches cannot mix map revisions
or player positions within that frame. Round changes, diagnostics and status
transitions invalidate the appropriate page caches. Movement/collision logic,
player colors, local shoulder marker and firing flash are preserved.

Run the production renderer tests with `make -C clients/lynx test`. These
include movement/wall checks and comparison with the frozen CPU reference
across 60 frames, both pages, destroyed bricks, cleared shots and firing
flashes, plus serial mutation during drawing and cache lifecycle checks.
The original benchmark's 240 pixel comparisons remain independently runnable
with `make -C clients/lynx/bench test`.

For an isolated emulator smoke test of the production live-endpoint ROM:

```sh
python3 clients/lynx/tests/handy_render_smoke.py
```

This requires the installed Handy/boot image, Xvfb, ImageMagick, Python Xlib
and Pillow. It uses a private display, temporary config and ephemeral
localhost ComLynx peer; it does not forward the ROM's live endpoint or use
FujiNet credentials. It checks reliable ACKs, brick removal, feet/border
pixels, movement/fire transmission, diagnostics restoration and new-round
maze restoration with 10 Hz snapshots. Screenshots, hashes and results are
written under `/tmp/maze-suzy-game-*`, and children are stopped on exit.
The October 12 integrated production ROM passed these checks; its
[smoke-test record](tests/results/suzy-integration.json) includes ROM/emulator
hashes and acknowledged revisions. This is not
physical half-duplex or WAN validation; live hardware testing remains needed.

The existing firmware has no Lynx-reachable stop command once raw NetStream
is active. This client does not claim stop/reconnect support; closing its
serial driver or sending a normal `$F0 STOP` packet would leave FujiNet in
raw mode. The protocol module can build `LEAVE_ROOM` and recognize
`LEAVE_ACK`, but the UI does not expose leave while raw-mode teardown is
unresolved. Firmware changes are outside the user's authorization. The
stop/reconnect requirement remains a gate before a complete client.

`src/lynx.cfg` is adapted from the FujiRealm Lynx client's MIT-licensed
linker layout. See [THIRD_PARTY.md](THIRD_PARTY.md) for its notice.
