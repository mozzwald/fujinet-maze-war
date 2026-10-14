# Lynx memory validation — 2026-10-13

Checkpoint: `3f8e2a6`. Production ROM SHA-256:
`cc62e765a2383c5b513146e01280a9482bbc3728aafb149fa12cfcfb3f7d24f4`.
The ROM was not rebuilt or patched for the positive tests. Firmware and the
installed Handy binary/source were not modified. The observer was built from
a separate copy of local Handy source under `/tmp/maze-memory-handy-2`.

## Results

| Scenario | Software stack, conservative / access / written | Hardware stack | Guard/code faults |
| --- | --- | --- | --- |
| First-use name, noisy ACKs, failed save, HTTP 404/default fallback | 312 / 233 / 233 bytes | 38 bytes | 0 |
| Saved name, edit at eight-character limit, cancel, delete/save, Lobby | 312 / 233 / 233 bytes | 38 bytes | 0 |
| Suzy maze/HUD, diagnostics, smoke/dance/results, next round | 312 / 268 / 268 bytes | 45 bytes | 0 |
| Actual server/two zombies, movement/fire, snapshot loss, lost WELCOME, restart, timeout/manual retry | 312 / 240 / 240 bytes | 44 bytes | 0 |

All four functional harness runs passed. Together they produced 203,948,032
CPU-update observations and 825 integrity scans. The observation count includes
CPU Update calls that return while asleep; it is not an executed-instruction
count or a timing benchmark. Memory is observed before every CPU Update, and
every hardware PUSH is separately observed, including interrupt entry.

The conservative software peak leaves **1,736 of the reserved 2,048 bytes**
unused in these runs. The hardware peak leaves **211 of 256 bytes** unused.
The deepest confirmed software access and written watermark both reached
268 bytes. No stack exhaustion or guard/immutable-code corruption was detected.
Stack capacity is not a current pressure point in the exercised paths.

The separate **27-byte gap between BSS and the reserved stack** is still the
tight code/data layout margin. It is not available stack capacity. Keep the
full 2 KB reservation and alignment checks; these measurements do not authorize
shrinking the stack or treating untested states as proven safe.

Machine-readable measurements, hashes, checkpoints and fault evidence:
[results/memory-validation.json](results/memory-validation.json).

## Observer and interpretation

`tools/build_handy_memory_probe.py` copies source and Makefile from
`~/build/handy-sdl` into a new output directory, adds the test-only observer,
and builds there. Existing harnesses accept `MAZE_HANDY_BINARY`; without it,
they still use `~/lynx-pc/handy_sdl`.

The generated observer is bound to the ROM/map's stack, BSS, zero-page and code
addresses. It checks the loaded CODE/RODATA hash before arming. Immediately
after crt0 initializes c_sp, before BSS initialization and constructors, it
fills unused software stack, hardware stack, BSS-to-stack padding and upper
unused zero page with address-dependent canaries. There is no instrumentation
code, stack frame or additional BSS in the Lynx cartridge.

cc65 updates its 16-bit stack pointer with separate byte writes. Between them,
an interrupt can see a torn word. The observer therefore records:

- **Conservative:** minimum raw pointer seen before any CPU Update, including
  intermediate byte-update values. This provides the more cautious headroom.
- **Access:** minimum pointer at foreground call/return/jump and indirect
  software-stack access boundaries. Assembly IRQ control flow is excluded:
  the linked clock, TGI and serial handlers do not use the software stack.
- **Written:** lowest non-canary byte found by periodic stack scans. This
  alone could miss reserved but untouched locals or coincidental canary values;
  it is not the sole source for the headroom conclusion.

Hardware PUSH is observed individually rather than inferred from the pointer
once per frame, so interrupt-entry pushes and short-lived call depth count.
Trying to consume the final hardware-stack byte is treated as exhaustion.

CPU writes to protected padding, unused upper zero page or CODE/RODATA abort
the diagnostic emulator. Periodic scans and first-visit checkpoints also inspect
guards and compare immutable bytes, including writes that bypass CPU_POKE, such
as Suzy DMA. Those scans can miss a transient DMA overwrite restored between
scans; this is not a proof of every RAM access or all hardware timing.

### Fault-injection validation

Six deliberate failures were detected with exit status 86 and the expected
reason: ROM/map mismatch, software stack below its reservation, hardware stack
exhaustion, padding write, unused-zero-page write and immutable-code write.
`tests/memory/test_probe_faults.py` changes only temporary ROM copies. For its
intentional patches it explicitly enables `MAZE_MEMORY_ALLOW_PATCHED_ROM` in
those child processes; the mismatch case and normal measurements keep the hash
check enabled. Do not enable that bypass for ordinary validation.

An earlier observer incorrectly classified an assembly IRQ's torn c_sp value
as an overflow. IRQ entry/RTI tracking corrected that before these recorded
runs. The initial rapid-key username-edit fixture also missed a menu action;
the existing `--slow-menu` allowance completed the test without changing game
code. Other final harness runs used their normal timing.

## Static layout and lifetime checks

- Full software stack: `$B838..$C037`; video begins at `$C038`.
- BSS ends at `$B81D` exclusive; protected padding is `$B81D..$B837`.
- Runtime/IRQ zero page ends at `$2B`; APPZP occupies `$2B..$7F` (85 bytes).
  The observer protects the remaining `$80..$FF` (128 bytes).
- CODE/RODATA integrity range: `$02B5..$A84F`. DATA is intentionally excluded:
  cc65's assembly IRQ dispatcher legitimately modifies its DATA-resident
  dispatch instruction. Startup/ONCE bytes are outside this integrity range.
- SCBs remain aligned at `$B500`, compact text at `$B400`. The existing layout
  checker passes with the full stack reservation and Suzy palette/row guards.
- No heap allocator is present in game sources or the linked map. Foreground
  APPZP temporaries are disjoint and assigned before use; the linked assembly
  IRQ handlers do not access them. Their owners must remain non-reentrant.
- Command/playfield/text union scratch retains its phase discipline. Text Suzy
  work finishes synchronously before a callback can reuse the scratch. These
  are maintained source invariants, not a claim that arbitrary in-bounds
  overwrites are detected by the canaries.

## Reproduce

Choose a **new, nonexistent** observer output directory. Install the same local
Handy build dependencies already used by its Makefile; no firmware build is
involved. Run from the repository root:

```sh
python3 clients/lynx/tools/build_handy_memory_probe.py \
  --output /tmp/maze-memory-handy

MAZE_HANDY_BINARY=/tmp/maze-memory-handy/handy_sdl \
  python3 clients/lynx/tests/handy_startup_smoke.py \
  --new-name --ack-noise --fail-save --http404
MAZE_HANDY_BINARY=/tmp/maze-memory-handy/handy_sdl \
  python3 clients/lynx/tests/handy_startup_smoke.py --edit-checks --slow-menu
MAZE_HANDY_BINARY=/tmp/maze-memory-handy/handy_sdl \
  python3 clients/lynx/tests/handy_render_smoke.py --round-checks --hud-checks
MAZE_HANDY_BINARY=/tmp/maze-memory-handy/handy_sdl \
  python3 clients/lynx/tests/handy_server_smoke.py --connection-checks
python3 clients/lynx/tests/memory/test_probe_faults.py \
  --emulator /tmp/maze-memory-handy/handy_sdl

python3 clients/lynx/tools/check_graphics_layout.py \
  clients/lynx/build/maze-war-lynx.lnx clients/lynx/build/maze-war-lynx.map
python3 clients/lynx/tests/memory/summarize.py \
  /tmp/maze-lynx-startup-XXXX /tmp/maze-suzy-game-YYYY \
  /tmp/maze-handy-server-ZZZZ --output /tmp/memory-runs.json
```

Replace XXXX/YYYY/ZZZZ with the printed run directories.
The harnesses print their own artifact directories and keep immutable ROM
copies, screenshots, wire observations and `handy.log` observer records there.
Use a map from the same build and regenerate the observer after any ROM change.
The production ROM retains `fujinet.online:5025`; harness forwarding uses local
fixtures instead of contacting a live room. Instrumentation adds host work,
so these runs are for memory behavior, not latency measurements.

This closes emulator memory validation for the recorded paths. It does not
replace the planned 30–60 minute mixed-client real-hardware endurance session.
