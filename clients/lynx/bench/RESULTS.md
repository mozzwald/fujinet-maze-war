# Suzy maze-cache benchmark results

Measured October 12, 2026 using the installed Handy SDL emulator. The
prototype improves gameplay cadence and avoids the receive losses observed
when the current renderer rebuilds the maze after a brick hit. It is still
too slow for consistently smooth animation under the heavier receive load.

## Complete frame cycles

Times below are mean milliseconds per complete iteration, including serial
parsing, rendering, and display swap waits. Lower is better. Brick samples
include ordinary actors plus updating a destroyed cell on both display pages.

### 50 packets/second: 550 incoming bytes/second

| Renderer | One player, one shot | Four players, three shots | Brick updates | Longest parser gap during brick updates |
|---|---:|---:|---:|---:|
| Current CPU | 62.3 | 163.7 | 571.2 | 570.3 |
| CPU, changed cells only | 64.3 | 163.1 | 174.6 | 123.3 |
| Suzy, batch 4 | 46.7 | 100.4 | 113.4 | 46.3 |
| Suzy, batch 8 | 39.5 | 96.3 | 109.5 | 51.4 |
| Suzy, batch 16 | 38.7 | 92.1 | 106.0 | 51.1 |

The current renderer recorded 12 malformed packets and 65 missing sequence
numbers during brick updates. The changed-cell CPU prototype and all three
Suzy variants recorded neither in gameplay phases. All Suzy phases, including
initial painting, also had zero malformed/missing packets, serial errors, or
software-ring saturation.

### 100 packets/second: 1100 incoming bytes/second

| Renderer | One player, one shot | Four players, three shots | Brick updates | Longest parser gap during brick updates |
|---|---:|---:|---:|---:|
| Current CPU | 134.3 | 403.2 | 771.1 | 703.7 |
| CPU, changed cells only | 136.4 | 403.9 | 432.0 | 220.2 |
| Suzy, batch 4 | 103.7 | 256.3 | 285.2 | 98.9 |
| Suzy, batch 8 | 94.0 | 241.2 | 269.1 | 105.3 |
| Suzy, batch 16 | 93.6 | 232.7 | 273.6 | 99.6 |

The current renderer recorded 35 malformed packets and 671 missing sequence
numbers during brick updates. The changed-cell CPU prototype had zero in
gameplay phases; all Suzy variants had zero across every phase, including
initial painting. Native variants also had zero serial errors and ring
saturation. Counts describe this injected sequence, not production traffic.

## Findings and recommendation

- Updating changed cells is the main fix for the large brick-hit stall.
  At the lighter load it reduces that cycle from 571 to 175 ms even with CPU
  drawing. Clearing shots without changing the maze did not cause this rebuild.
- Native Suzy restoration and sprites further reduce ordinary multiplayer
  cycles from 164 to 96 ms and brick cycles to 110 ms with batches of eight.
  These are approximately 1.7× and 5.2× improvements respectively.
- Start production integration with batches of eight. Sixteen was slightly
  faster for ordinary movement, but eight kept nominal multiplayer parser
  gaps shorter (36 versus 59 ms). These single runs do not establish a
  statistically reliable ranking for small differences between batch sizes.
- Initial painting is slower in this prototype. Painting both pages took
  1.36 seconds with the current CPU renderer and 1.93 seconds with Suzy batch
  eight at the lighter load; at the heavier load, 1.56 versus 4.87 seconds.
  The CPU lost packets during startup, whereas Suzy processed the offered
  traffic, so the heavier comparison includes unequal successful parser work.
- Suzy batch eight still averages about 10.4 frames/second with four players
  at the lighter load and 4.1 at the heavier load. C sprite preparation and
  packet processing need further profiling before promising smooth movement.

## Serial concurrency correction

The current cc65 `tgi_sprite()` call starts the SCB list and sleeps the CPU
until drawing completes. Foreground serial parsing cannot run inside that
call. UART interrupts can wake the CPU to buffer received bytes; the runtime
then resumes sleep. This benchmark polls immediately before and after short
SCB batches. Its longest batch-eight hardware call was 0.426 ms in the
lighter run and 0.507 ms in the heavier run. Longer foreground polling gaps
therefore arise outside those hardware calls, including C preparation and
packet parsing.

Relevant implementation references on this machine are
`~/build/cc65/libsrc/lynx/tgi/lynx-160-102-16.s` (`draw_sprite`, around line 379),
`~/build/handy-sdl/src/handy-0.95/mikie.cpp` (CPUSLEEP, around line 2094), and
`~/build/handy-sdl/src/handy-0.95/c65c02.h` (sleep restoration after IRQ, around
line 649). No emulator or firmware modification was needed.

## Correctness and scope

The native prototype uses cached tiles on both display pages, tracks brick
changes separately per page, restores cells covered by previous actors, and
draws transparent player/shot SCBs. All maze rows use this path, including the
bottom border. Production movement, collision logic, and artwork are shared.

The host pixel test passed 240 comparisons covering both pages, moving
actors, cleared shots, brick removals, last-row feet, and textured borders.
Final Handy screenshots had zero maze-pixel differences between each
alternative and the reference renderer at both loads. Existing Lynx host
tests and the benchmark build also passed.

Each final run used the same ROM and fixed renderer order, with 60 samples
for each steady movement scenario, 24 brick-update frames representing 12
brick removals, and 12 shot-clear frames. These are emulator measurements,
not hardware or live-server measurements. The direct ComLynx peer exercises
the real serial driver, framing, CRC, and packet parser, but bypasses
FujiNet firmware, WAN latency, gameplay transmission/ACK traffic, and
electrical half-duplex contention. See [the benchmark guide](README.md)
for the exact workload, timers, and reproduction commands.

A separate 200-packet/second overload probe was deliberately stopped after
the current receive path showed sustained loss even in the one-player
scenario. Its partial results do not establish native-renderer performance
at that load; they show that rendering alone is insufficient to resolve
receive-path overload.

The live production ROM was not replaced. This prototype still needs live
input/transmission, firing flash, round/menu integration, and hardware
acceptance before use as the game renderer. No FujiNet firmware was changed.

## Reproducible evidence

- [Nominal timings](results/nominal/table.md), [per-frame data](results/nominal/results.json),
  [run identity](results/nominal/metadata.json), [pixel comparisons](results/nominal/pixel-comparison.json).
- [Heavy timings](results/heavy/table.md), [per-frame data](results/heavy/results.json),
  [run identity](results/heavy/metadata.json), [pixel comparisons](results/heavy/pixel-comparison.json).
- [Partial overload probe](results/overload-probe/partial-results.json).
- [Source hashes](results/source-sha256.json).

Compiler: `cl65 V2.19 - Git f54780cfb`.
Benchmark ROM SHA-256:
`6084cb426e95cf20ea470022c009bc69010161e0f530bcbde96678da603bc9bc`.
Unchanged production ROM SHA-256:
`94629c8130155eb2058b49896f624bf63a105140e300b7a097e4962108d76623`.
