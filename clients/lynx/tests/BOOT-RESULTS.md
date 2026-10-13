# Hardware startup regression: working build confirmed

2026-10-12: user reports the latest ROM boots in Handy, but real hardware
intermittently freezes black after a few FujiNet bus flashes or reaches a
corrupted frozen menu. The subsequent graphics fix below now boots successfully
on real hardware, as confirmed by the user. Its individual changes have not
been isolated to identify a single root cause.

Follow-up hardware feedback: user confirms both the byte-identical previous
ROM and the boot-test ROM boot successfully; the diagnostic is slow as
expected. User noticed 42.5 kB for the failing normal ROM and 42.3 kB for
the previous ROM. Exact local sizes are 42,537 and 42,262 bytes respectively;
the 275-byte increase is expected from the latest complete-frame RX-quiet
guard and two-cell visual retargeting changes. The diagnostic is larger
still, at 42,746 bytes. File size alone does not identify the failure.
A fresh normal rebuild reproduces the same d18322 SHA below. The user retested
that version and confirmed another frozen lobby on hardware. The photo
`IMG_20261012_211240.jpg` shows readable compact text, a missing large title
and a corrupted horizontal selection marker. Successful previous/diagnostic
boots did not establish that this version was stable or that the SD copy
was corrupt.

## Hardware-confirmed build: graphics driver and placement

The normal cartridge has now been replaced by a hardware-tested graphics fix:

- Vendor the matching cc65 TGI driver locally and restore its omitted text
  scanline fill byte. Each 1-bit literal text row now ends with a background
  byte for the Suzy last-pixel workaround, within the existing 177-byte buffer.
- Align TGI control blocks and text data; allocate the eight game control
  blocks plus compact-text control block together in one assembly-aligned
  page. Avoid the documented palette-at-`$xxFA` erratum. The failing ROM's
  palettes did not hit `$xxFA`, so this is preventive, not a confirmed cause.
- Enforce these placements at build time. Page containment is a conservative
  constraint, not a claim that Lynx forbids all SCBs from crossing pages.
- Remove the obsolete miniature-maze fallback to pay for alignment without
  reducing the stack. A video-page probe failure now displays diagnostics.

Normal ROM: **40,948 bytes**, SHA-256
`1e7f5077541f79e646ba27ea8003042f1a11de560284c4d3731c8dcee42a2ac4`.
Live Lobby and `fujinet.online:5025` are retained. No checksum scan overhead.

TGI SCBs are `$9F81/$9FA1/$9FC1/$9FE1/$A001`; TGI text bitmap `$B000`;
game SCBs `$B100..$B1B7`; compact-text SCB `$B1B8..$B1CE`.
BSS ends at `$B41D` exclusive, leaving 1,051 bytes before the unchanged
2 KB stack at `$B838..$C037`.

Full host suite and graphics layout gate pass. Injected bad map placements
(crossing control block, stack overlap and shifted compact-text block) are
rejected. Installed unmodified Handy passes startup with noisy ACKs and
gameplay movement/rendering, including diagnostics and return to the maze.
Artifacts: `/tmp/maze-lynx-startup-qeplmsur` and
`/tmp/maze-suzy-game-7abs3g5e`. Name editing verification is recorded in the
JSON results alongside the initial automation timing failure.

**Hardware feedback:** the user confirms this build works again without a
freeze. This is direct hardware evidence beyond the emulator checks. Repeated
boot counts, name-edit coverage and live latency measurements were not reported.

## Inspection

- Startup/AppKey/HTTP code is unchanged by the latest movement/RX-quiet work.
  Those new transport and movement paths start after the menu. This does not
  rule out an indirect memory/layout regression.
- Normal ROM BSS ends at `$B820`; reserved software stack is `$B838..$C037`.
  Only 23 unused bytes separate BSS from that reservation, but the entire
  2 KB stack remains available. This is not evidence of stack exhaustion.
- TGI's collision buffer defaults to `$A058`, inside program RAM. Startup
  explicitly disables collision detection before any screen drawing; crt0
  already sets Suzy's global collision-disable bit. Generic `tgi_init` does
  not draw/clear the screen. No initialization collision wipe was found.
- No firmware or emulator source was modified or built.

## Cartridges

All three retain live HTTPS Lobby and `fujinet.online:5025` default room.

| File in `clients/lynx/build` | SHA-256 | Purpose |
| --- | --- | --- |
| `maze-war-lynx-previous.lnx` | `78e8c44083876f63a4a7b92e5f47d53ff684fc8985e18f2cf28efdb529e1fd9b` | Byte-identical reconstruction of the previous hardware-tested version; comparison/control only |
| Historical failing normal ROM | `d18322efe1d594112c9b68a61ba0e3e0db718b0dc797a53dc343a8428e61444a` | Repeat hardware freeze confirmed; replaced by the candidate above |
| `maze-war-lynx-boot-test.lnx` | `7526b173777f34c156405a4677e30756b36ed591bb3cb11f99e1fa944743b328` | Latest code with startup checks; separate file, not a claimed fix |

Control build source is in `/tmp/maze-lynx-boot-control-wfdzv_m8` for this
session. It removes only the last complete-frame RX-quiet addition and the
two-cell remote/correction retarget condition; matching the prior SHA
establishes the exact reconstruction. Temporary files are not permanent
source history.

## Diagnostic operation

`BOOT_GUARD=1` adds an assembly checksum over immutable CODE and RODATA.
`tools/seal_boot_guard.py` writes the expected 16-bit byte sum into DATA after
linking. The diagnostic checks 41,924 bytes, sum `$973C`. Verify sealing with:

```sh
python3 clients/lynx/tools/seal_boot_guard.py \
  clients/lynx/build/maze-war-lynx-boot-test.lnx \
  clients/lynx/build/maze-war-lynx-boot-test.map --verify
```

On a match, tiny top-edge white ticks mark the checkpoint on both video
pages. On a mismatch, the CPU displays full-height white bars and halts,
bypassing Suzy/TGI and disabling interrupts. Count ticks/bars in a photo:

| Count | Checkpoint |
| --- | --- |
| 1 | Entry to `main`, before graphics/serial installation |
| 2 | Before starting a menu screen |
| 3 | After clearing the menu draw page |
| 4 | After drawing the menu title |
| 5 | After drawing the rest of the screen, before requesting display swap |
| 6 | After loading the saved name or completing name entry |
| 7 | After fetching the HTTP Lobby |

Checks intentionally slow menu redraws. Hold buttons longer during this
diagnostic; normal builds have no check/scan overhead. Interrupts remain
enabled during scans so receive IRQs can continue. Marker CPU writes touch
only the first 16 bytes of each video page, below hardware register overlays.

To fit with the unchanged 2 KB stack, the diagnostic stores two presentation
history points per actor instead of eight. The bounded renderer reads only
the newest point and its predecessor; pending-input queues are unchanged.
All host tests pass with this compact history. Diagnostic BSS ends `$B801`.

Limitations: this checksum covers CODE/RODATA, not mutable DATA/BSS, zero
page, stack or all startup instructions. An additive checksum cannot detect
every corruption. Diagnostic size/layout/timing differ from the failing
normal ROM and can mask a timing/layout-sensitive fault. Success is not
proof of a fix. A photo of the frozen diagnostic and comparison with the
byte-identical previous ROM are still needed.

## Emulator verification

```sh
python3 clients/lynx/tests/handy_startup_smoke.py --ack-noise
python3 clients/lynx/tests/handy_startup_smoke.py \
  --rom clients/lynx/build/maze-war-lynx-boot-test.lnx \
  --slow-menu --ack-noise --edit-checks
python3 clients/lynx/tests/handy_startup_smoke.py \
  --rom clients/lynx/build/maze-war-lynx-boot-test.lnx --boot-guard-fault
make -C clients/lynx test CC='cc -DGAME_MOTION_POINTS=2'
```

Passed with installed, unmodified Handy and private command peer:

- Normal latest ROM: saved name, noisy ACK scanning, HTTP Lobby waits for
  selection, selected live endpoint, HELLO/WELCOME and retained NAME.
  Artifacts `/tmp/maze-lynx-startup-mbx2dm26`.
- Guarded ROM: same plus eight-character name limit, cancel/delete/save and
  connection after editing. Artifacts `/tmp/maze-lynx-startup-h1z7ug66`.
- Deliberately changed checksum in a private ROM copy: detected before
  graphics/serial, one full-height CPU-drawn white bar, no commands/frames.
  Artifacts `/tmp/maze-lynx-startup-zzj00341`.
- Full host suite with two presentation history points, including six-cell
  burst wrap, movement, walls, protocol, cache, death/round and sound checks.

The harness copies controlled-peer ROMs before boot and accepts `--rom`;
fault injection never modifies the hardware-test or normal cartridges.
No electrical half-duplex behavior or actual hardware freeze was reproduced.
