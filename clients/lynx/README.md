# Maze War Lynx client (early playable view)

See the [Suzy maze-cache benchmark results](bench/RESULTS.md) for measured
renderer alternatives and the [benchmark guide](bench/README.md) to reproduce
them. The benchmark cartridge is separate from the live client.

### Real Suzy sprite row encoding

Opaque cached maze tiles use counted sprite packets with an explicit end marker.
Transparent players, shots, smoke and small HUD text use a zero guard byte on
every literal row. Suzy has an end-of-byte data bug that can omit the final
pixel when its last meaningful bit occupies bit zero of the final byte; see
the [original sprite-engine documentation](https://www.monlynx.de/lynx/lynx6.html).
Unpadded floor rows left one old pixel per cell row, producing movement trails
and remnants of startup text after a full maze repaint on hardware.

Handy's literal decoder only special-cases a zero final pixel and misses the
odd-valued final-pixel case. The host renderer now models that case, reproduces
the old failure, and checks restored pages against the CPU reference. Opaque
tiles have an exact eight-pixel width: padding an opaque literal would otherwise
paint unwanted pixels into the neighboring cached cell. Rendering still uses
Suzy batches of at most eight with serial service between batches.

This cartridge includes a startup username editor and HTTP Lobby browser.
It initializes the cc65 ComLynx serial driver at 62500 baud, 8 data bits, odd
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
shows the last frame-error code (3 means malformed COBS). Return to Lobby
after gameplay awaits a future firmware NetStream STOP mechanism.

### Username and Lobby startup

The client reads its username before displaying Lobby: Maze War creator
`$3022`, application `$03`, key `$00`, with a read-only fallback to the old
Lobby name at `$0001/$01/key $00`. Missing/invalid names open an eight-character
editor. Up/Left advances the character; Down/Right reverses it; A appends,
B deletes, and A on DONE confirms. A separate SPACE choice allows spaces.
Names use uppercase letters, digits, space, hyphen and period. Option 2 cancels
an edit when a previous valid name exists. This interaction follows
[Battleship's Lynx selector](https://github.com/shawnjefferson22/fujinet-battleship/blob/41286cf973bce1032530a20d9f8ca861b0aa2b03/src/lynx/input.c);
the implementation uses Maze War's renderer and input handling.

Confirmed names save only to Maze War's key. Saves are read back and verified.
The client supports the older Lynx WRITE format with a little-endian length
and the newer AppKeyMixin raw-value format. It first sends the older safe
shape; if the newer firmware stores that exact prefix literally, it rewrites
the raw name and verifies again. A failed save shows NAME NOT SAVED and keeps
the chosen name usable for this session. Edit/confirm retries saving. Names
survive game initialization and the WELCOME reset.

Lobby uses FujiNet network device `$71` over HTTPS before raw NetStream starts.
It fetches four compact binary records per page, with bounded reads and eight
pages maximum. The default filter is `platform=atari&appkey=3`, since the live
Maze War rooms currently have Atari registrations; the client ignores their
Atari download URLs and connects to the shared game protocol. Build settings
`LOBBY_BASE`, `LOBBY_PLATFORM`, `ROOM_PORT_BASE` and `ROOM_COUNT` control the
query and allowed endpoint range. Live hardware builds use
`https://lobby.fujinet.online` and `fujinet.online:5025..5028`.

Lobby controls: Up/Down selects a room, Left/Right changes pages, A joins,
B joins the configured default room, Option 1 refreshes, Option 2 edits the
name. Full rooms are unavailable for selection. Missing/invalid listings and
HTTP failures allow refresh or default-room fallback. HTTP closes before
`$F0` activates raw TCP. No HTTP or AppKey commands run during gameplay.

Until firmware provides an in-band STOP mechanism, a failed raw connection
cannot return to this browser or change endpoints. Stop NetStream using
FujiNet's web configuration (or reset FujiNet) before restarting the cartridge
to browse again; resetting only the Lynx does not guarantee command mode.
No firmware modifications are included in this client change.

Command mode follows FujiRealm's ComLynx handoff: before each command it waits
for 1,200 consecutive empty serial polls (resetting on trailing bytes), and
ACK phases scan until ACK or NAK instead of rejecting the first unrelated byte.
The existing transaction deadline bounds both waits. Length, payload and XOR
checksum parsing remain exact; the drain is disabled once NetStream owns serial.

Command failures show a small diagnostic line on the Lobby/default-connection
error screen, for example `D70 CF0 P03 E01 B00 S10`. Fields are hexadecimal:
`D` device, `C` command, `P` phase, `E` error, `B` last received byte, and `S`
raw cc65 Lynx serial status. Phases: 01 quiet drain, 02 transmit, 03 acceptance
ACK, 04 completion ACK, 05 response length, 06 response data, 07 checksum,
08 response ACK. Errors: 01 timeout, 02 NAK, 03 checksum mismatch, 04 oversized
response. Status bits include 80 driver RX overflow, 10 parity, 08 overrun,
04 framing and 02 break. A zero status does not rule out a lost reply.
The line retains the most recent command failure across successful cleanup
commands; it is diagnostic evidence, not a new game protocol message.

Startup checks:

```sh
make -C clients/lynx test
python3 clients/lynx/tests/handy_startup_smoke.py --new-name
python3 clients/lynx/tests/handy_startup_smoke.py --new-name --ack-noise
python3 clients/lynx/tests/handy_startup_smoke.py --new-name --fail-save --http404
python3 clients/lynx/tests/handy_startup_smoke.py --firmware --new-name
python3 clients/lynx/tests/handy_startup_smoke.py --firmware --edit-checks
python3 clients/lynx/tests/handy_startup_smoke.py --firmware --live-browser
```

The default harness uses a controlled command peer and production ROM. Firmware
mode uses the existing FujiNet PC binary, private SD/config and local HTTP/game
fixtures with a separate localhost ROM under `/tmp`. `--sd-from <prior-run>/SD`
copies private test storage to verify a later boot reloads the saved name.
Live-browser mode reads the real production HTTPS Lobby with the hardware ROM
but does not join a live room. These checks supplement physical serial and
SD-card testing; the emulator cannot prove one-wire electrical timing.

The October 12 build passed the [startup test matrix](tests/results/lobby-startup.json),
including actual FujiNet PC AppKey save/reload and the live HTTPS Lobby.
See the [Lobby screenshot](art/handy-lobby-preview.png). On hardware, check
first-time name entry, persistence after restarting with FujiNet back in command
mode, room selection and the chosen name appearing in the game's score line.

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

### Sound effects

Mikey supplies four independent voices, one per player, with remote effects at
half the local player's volume. The effects follow the Atari client's purposes:

- Quiet, brief shuffle noise for a successful movement step.
- A descending shot zip, with immediate feedback for a transmitted local fire
  press and suppression of its subsequent network echo.
- A short noisy impact when an active shot is cleared, plus a burst for a
  destroyed brick. The wire protocol does not identify the cause of a shot
  clear, so shot impacts use one generic sound.
- A swelling/fading noise envelope during death and round-end smoke.
- An alternating high/low warble for respawning and initial placement.

Death and respawn effects take priority over footsteps and shots. Repeated
state packets, snapshot prediction replay and duplicate brick removals do not
restart sounds. The winner dance itself is quiet, as in the Atari sequence;
the score screen and Lobby are silent. Use the Lynx volume control to adjust
the mix.

Envelopes use elapsed VBL ticks and skip missed steps rather than delaying the
game. The foreground service runs alongside serial polling between Suzy batches;
audio uses only Mikey's four audio channels, with independent 8us clocks, and
does not change serial/display timers or install an interrupt. Collision
detection is explicitly disabled: this client uses that RAM for game data,
and all production sprites use non-colliding Suzy drawing.

`make -C clients/lynx test` includes sound event, envelope, priority, duplicate,
clock-wrap and delayed-service checks. To capture the existing Handy binary's
audio while checking movement or round transitions:

```sh
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --sound-checks
python3 clients/lynx/tests/handy_render_smoke.py --round-checks --sound-checks
```

Sound capture additionally needs a host C compiler, SDL2 development headers and
pkg-config. The harness compiles a test-only SDL callback preload under `/tmp`,
uses the dummy audio device and records the exact emulator output as unsigned
8-bit mono PCM at 22050 Hz. It verifies nonzero samples during effects and zero
samples during idle play/results, and writes `sound-preview.wav` plus a JSON
record. No emulator or firmware modifications are required. Speaker character
and the final volume balance still need physical Lynx listening.
See the [audio test record](tests/results/sound-effects.json) and
[compact Handy recording](art/handy-sound-preview.wav). The preview removes
long silent gaps from the captured fixture run.

### Round-end presentation

The accepted MATCH_END freezes the names, occupied slots, roles and final scores.
Losers go up in the existing four-stage smoke effect, and the winner dances
through four poses for five seconds, then disappears in smoke. A separate
ROUND OVER screen shows the winner's full name, four colored score rows and
NEXT ROUND, following the Atari client's wording. A human victory in a round
with Zombies reads BEATS ZOMBIES. Late intermission joins show scores directly
when no playable board was received.

The VBL clock drives the sequence; input/link polling and reliable ACKs continue
between Suzy batches and while the scores are displayed. Duplicate results do
not restart the animation or replace final scores. ROUND_START can interrupt
the sequence, invalidates both pages and restores normal play after the usual
map/snapshot barrier. B diagnostics can still be opened during intermission.

```sh
python3 clients/lynx/tests/handy_render_smoke.py --round-checks
```

This uses a controlled local ComLynx peer with the live-endpoint ROM, including
animation, ACKs, duplicate results, diagnostics and the following round.
The host round test additionally checks clock wrap and an interrupted dance.
See the [Handy test record](tests/results/round-presentation.json) and
[score-screen preview](art/handy-round-scores-preview.png).

### Movement and turn control

Each sampled directional press contributes one discrete input, even if it is
released before an outbound slot is available. A four-entry intent FIFO
preserves short taps and quick turns in order. The first step is immediate;
a held direction waits 250 ms before repeating at 100 ms intervals. Late
polling sends one repeat, never a burst to catch up. A new direction is sent
without waiting for the repeat timer. If D-pad directions overlap, the newly
pressed direction wins instead of a fixed Up/Right priority.

Snapshot arrival no longer grants another movement input. Held repeats use
a window of two to four outstanding movement inputs. The window grows with
measured send-to-applied-ACK delay, including server tick and queue time,
and returns to two after one second without ACK progress. Repeats also stop
when four total inputs are pending, leaving replay capacity for explicit
turns and release. Explicit taps/turns retain their order. A full eight-entry
replay ring waits for acknowledgements instead of
clearing prediction history. A released button cancels future held repeats;
inputs already transmitted still have to be applied by the server.

The local sprite retains the cell path predicted by transmitted inputs and
traverses each step in six VBL ticks (100 ms), independently of render count.
Queued turns begin at the shared cell boundary; drawing cannot substitute a
diagonal shortcut. ACK replay preserves an unchanged predicted path, while an
authoritative correction rejoins the corrected cell. The sprite settles at
the last transmitted step, keeping its stopping cell aligned with the cell
from which the server will accept a turn. The entire 8×5
footprint is checked against walls and occupied cells; wizard and muzzle
pixels are also clipped to walkable maze cells. If a wall blocks correction,
the sprite rejoins the server's walkable target cell. Firing settles unfinished
interpolation at the predicted shot origin.

The input sampler and bounded transmitter run between Suzy batches and while
waiting for a display swap. Clock ticks come from cc65's VBL counter, so slow
rendering does not turn a 100 ms repeat interval into a frame-count delay.
Host input tests cover latched taps, snapshot/press races, stop, overlapping
turns, stalled acknowledgements, a full replay ring and clock rollover.
Motion tests compare identical paths at 20 and 60 renders per second and
check repeated same-tick draws, inside corners, corrections and shot origins.

The final diagnostics line shows hexadecimal `W` (repeat window), `D`
(filtered applied-ACK age), `Q` (pending input count), and `F` (last frame
interval). `D` and `F` are in 60 Hz VBL ticks. Frame/draw timing maxima and
repeat-stall/intent-overflow counters are also available in debugger state.
The earlier `F` field remains the serial frame-error code.

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

For a repeatable held-movement comparison, run this separately from the
movement checks, optionally selecting an older ROM with `--rom`:

```sh
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300
```

The peer delays input application, records transmitted and applied steps,
and samples visible sprite positions. The 300 ms comparison improved from
six to ten steps during a roughly 1.2-second hold; short taps and turns also
passed. See [the measurement report](tests/LATENCY-RESULTS.md) for conditions
and limits. These changes preserve the existing wire protocol.

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
rows retain per-page change detection; compact text uses native Suzy sprites.
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
