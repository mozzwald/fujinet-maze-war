# ACK window and elapsed-time motion measurements

Measured 2026-10-12. Numeric results and ROM hashes are in
[latency-window-animation.json](results/latency-window-animation.json).

## Changes

1. Held movement adapts from two to four outstanding inputs using measured
   send-to-applied-ACK age. It shrinks after an ACK stall and preserves replay
   capacity for explicit turns and release.
2. Sprite motion follows recorded cell steps using the 60 Hz VBL clock,
   taking 100 ms per cell. Render frequency no longer controls travel time.
   Turns follow the recorded corner; corrections and firing rejoin their
   predicted/server cell. No packet format or firmware changes are needed.

## Handy comparison

Both ROMs ran in the existing Handy SDL emulator with the same private
ComLynx peer. The peer applies one queued input per 10 Hz tick, with 300 ms
of additional input application delay. The player walks along an empty
corridor with the other players inactive. The harness holds Right for about
1.2 seconds, releases, drains pending input, and checks that movement stops.

| Measurement | Before | After |
| --- | ---: | ---: |
| Hold duration | 1220 ms | 1221 ms |
| Movement inputs sent | 6 | 10 |
| Movement steps applied by peer | 6 | 10 |
| Largest gap between held repeat sends | 216.3 ms | 180.6 ms |
| Distinct sampled robe positions | 12 | 14 |

The old ROM is the hardware-validated state at commit `96dab0b`; the new ROM
contains both improvements. Each run also passed the harness's normal map,
sprite, input and page restoration checks. A separate new-ROM run at the
same 300 ms delay passed 60/120/160 ms single-cell taps, release, and the
overlapping Up-to-Right turn.

This is one before/after run per ROM. Host scheduling, screenshot capture
and emulator throttling affect wall-clock timings. The step counts show
less feedback-window starvation in this workload; the pixel count alone
does not establish a frame rate or quantify perceived smoothness. The peer
does not reproduce firmware one-wire turnaround or Internet jitter, and
300 ms input application delay is not a measured network RTT.

Reproduce with:

```sh
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
python3 clients/lynx/tests/handy_render_smoke.py --latency-checks --input-lag-ms 300
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300
```

To compare against an older ROM, pass `--rom /path/to/older.lnx` to the first
command. Save the older ROM before rebuilding. Each run prints an artifact
directory containing `results.json`, input events and screenshots.

## Deterministic host checks

The separate input model uses production protocol code with a synthetic
60 Hz input clock and 10 Hz authority. During a 1.5-second hold:

| Simulated propagation RTT | Before steps | After steps |
| --- | ---: | ---: |
| 0 ms | 14 | 14 |
| 100 ms | 14 | 14 |
| 200 ms | 13 | 14 |
| 300 ms | 10 | 14 |
| 500 ms | 6 | 10 |

The model source remains in the local planning directory. At 500 ms, the
four-input cap still causes gaps; increasing the cap further would worsen
old-direction backlog. These numbers are model output, separate from the
Handy comparison above.

Committed `motion_test.c` verifies the same pixel positions at 20 and 60
renders per second, no movement on repeated same-tick draws, a retained
inside-corner path, ACK replay, correction, muzzle alignment and clock wrap.
`input_test.c` checks window growth, its cap, turn/release admission and
timeout shrink. The complete host suite and live-server ROM build passed.

## Live validation

The ROM uses Lobby normally and `fujinet.online:5025` for the default room.
Test sustained walking, single taps, stopping and turning in the two-zombie
room on hardware. The wire protocol and server remain unchanged. Higher
network delay can still leave already transmitted steps to apply after
release; authoritative corrections can still visibly rejoin the server cell.
