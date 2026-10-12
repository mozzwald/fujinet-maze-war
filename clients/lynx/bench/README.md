# Lynx renderer benchmark

This separate cartridge compares the shipped renderer with two prototypes:

1. **CPU current:** unchanged `src/playfield.c` and `src/render.c`.
2. **CPU changed cells:** the same CPU bitmap renderer, patching destroyed
   cells in its background cache and on both display pages.
3. **Native Suzy:** cached maze tiles on both display pages; restore tiles
   touched by old player/shot rectangles, patch changed bricks, and draw
   transparent native sprites. Compare batches of 4, 8, and 16 SCBs.

The production ROM and firmware are unchanged. The benchmark ROM connects
to an isolated measurement peer and is not a live-server client.

## Build and run

```sh
make -C clients/lynx/bench
make -C clients/lynx/bench test
python3 clients/lynx/bench/run.py --traffic-hz 50
python3 clients/lynx/bench/run.py --traffic-hz 100
```

The script requires Xvfb, ImageMagick `import`, Python Pillow, the installed
`~/lynx-pc/handy_sdl`, and `~/lynx-pc/lynxboot.img`. Each run allocates its own
display, ephemeral localhost port, and `/tmp/maze-suzy-bench-*` directory.
It keeps emulator settings there and terminates Handy/Xvfb on exit, including
after failure. Socket/display sandbox restrictions can require elevated
execution. It does not load or copy FujiNet credentials/configuration.

Outputs include ROM/emulator SHA-256 hashes, per-frame timing JSON, a summary
table, phase telemetry, and one screenshot per renderer. Pixel comparisons
cover the full maze, players, shots, last-row feet, and border texture.
The host pixel test additionally compares 240 individual changed-cell CPU
and native-renderer frames against the CPU reference, including both page updates after each
brick removal and restoration after moving/clearing overlays. Its minimal
literal-sprite rasterizer checks pixels and cache ordering, not hardware
timing; the Handy runs validate the sprite encoding on Suzy's model.

## Workload and measurements

The same executable selects all five renderers, sharing the production
movement/collision logic and art. It uses a deterministic maze, fixed HUD
names, and identical positions. It measures initial painting of both pages,
60 moving frames with one player/one shot, 60 with four players/three shots,
12 individual brick removals across 24 frames, and 12 shot-clear frames with
an unchanged maze. Each brick-removal sample includes its ordinary movement
and shot rendering. Both display pages must catch up before the next brick.

Mikey timers 1 -> 3 -> 5 provide a benchmark-only 24-bit microsecond clock;
video uses 0/2 and serial uses 4. Individual durations and polling gaps are
shorter than the clock's 16.78-second wrap period. Phase totals accumulate
individual iteration durations in 32 bits. Timers stay enabled during CPU
sleep, so measurements include Suzy's emulated drawing time. Render duration
includes tile/cache work, sprites, the production HUD/status path, and the
serial polling callbacks inside drawing. The per-sample `frame_us` duration
additionally includes the wait for the display swap. A preparatory serial
poll occurs before that sample timer starts. Therefore use `elapsed_us/count`
for the mean **complete frame cycle**, including preparation and all polling,
when comparing gameplay cadence. Renderer-only times are not a fair measure
of total work when a backend moves packet processing inside its render calls.
The harness also records the maximum interval
between foreground parser polls and maximum native Suzy batch duration.

The localhost peer acknowledges the normal `$70/$F0` setup and sends the
normal WELCOME. During each measured phase it injects CRC-valid inactive
slot-3 SHOT messages through Handy's ComLynx bridge and the unmodified cc65
62500-baud driver. These exercise the actual client framing, CRC and packet
parser without changing the scene. At 50 and 100 messages/second the offered
loads are 550 and 1100 serial bytes/second respectively. Serial errors,
software ring saturation, malformed frames, and gaps in the injected frame
sequence are counted. Sequence gaps detect losses even when the cc65 receive
buffer does not report an overrun. Telemetry transmission happens outside
timed sections; incoming traffic stops during reporting.

## What serial servicing means here

`tgi_sprite()` starts a native SCB chain, sleeps the CPU, and returns when
drawing completes. Foreground packet parsing does **not** execute concurrently
with that call. Serial interrupts can wake the CPU and buffer received bytes;
the runtime then resumes the interrupted sleep. The prototype explicitly
polls before and after each short SCB batch. Its polling-gap measurements
include C code that prepares the sprites, not just the hardware draw time.

This is a renderer and receive-service benchmark. It does not measure WAN
latency, FujiNet firmware scheduling, transmitted gameplay/ACK traffic, real
one-wire electrical contention, or physical Lynx timing. Destroyed bricks and
shot clears are applied deterministically by the harness rather than waiting
for a remote game server. The native implementation is a benchmark prototype;
round/menu transitions, live input transmission, and the local firing flash
need production integration and acceptance testing before replacing the live
renderer.

See [RESULTS.md](RESULTS.md) for the measured comparison.
