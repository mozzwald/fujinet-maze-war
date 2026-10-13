# Input and reliable-event recovery

2026-10-12. The previous movement improvements are committed as `8b2b1b5`.
This follow-up repairs server retry/admission behavior and adds bounded
same-sequence retransmission in the Lynx client. No firmware changes.

## Changes and compatibility

- Reliable-event ACKs: duplicate ACK fast resends respect the existing 75 ms
  limit; stale ACKs cannot resend anything. Advancing an ACK still sends the
  next queued event immediately, and the 300 ms ordinary timeout remains.
- Input admission: a six-entry queue rejection does not consume the input's
  sequence. It may be retried later; admitted or applied duplicates remain
  ignored. Neutral keepalive folding and one-command-per-tick application
  remain intact.
- Lynx retries: when unable to send fresh input, resend the newest pending
  input with its original sequence/joy/round. Timeout is twice measured
  applied-ACK age plus 200 ms, clamped to 500 ms..2 s. No new prediction,
  motion event or sound occurs. Reliable ACKs and sendable fresh turns and
  release take priority. Retry traffic is at most two frames per second.

These changes preserve packet formats and legacy-client behavior. The client
retry works with the old server for an input lost before admission. Repair
of queue-full rejection additionally requires deploying the updated server.
The live `fujinet.online` server has not been deployed by this work.
Follow-up: the user subsequently deployed this server version and confirmed
Atari 8 compatibility. Lynx lag remained, prompting the client-only work in
[TRANSPORT-RESULTS.md](TRANSPORT-RESULTS.md).

## Handy loss reproduction

The existing emulator runs production ROMs with a private peer implementing
sequence filtering, six-entry admission and 10 Hz application. The test
drops every new directional input until the window fills, admits a retry
of the same sequence, and checks movement resumes without another press.
The fault ends before the usual rendering/input checks.

| During a 3.4-second Right hold | Committed ROM | New ROM |
| --- | ---: | ---: |
| New directional inputs deliberately dropped | 4 | 4 |
| Authoritative movement steps during hold | 0 | 17 |
| Transmissions of dropped tail | 1 | 2 |
| Applications of dropped tail | 0 | 1 |

The new actor reached the corridor's far wall. Seventeen is evidence of
recovery in this finite corridor, not a general speedup or movement-rate
measurement. Both runs stopped after release and pending input drain.
Raw selected results and ROM hashes:
[input-recovery.json](results/input-recovery.json).

Reproduce separately, preserving an older ROM before building:

```sh
make -C clients/lynx test all SERVER_HOST=fujinet.online SERVER_PORT=5025
python3 clients/lynx/tests/handy_render_smoke.py --recovery-checks
python3 clients/lynx/tests/handy_render_smoke.py --recovery-checks --expect-stall --rom /path/to/8b2b1b5.lnx
python3 clients/lynx/tests/handy_render_smoke.py --movement-checks --input-lag-ms 300
bash tests/server_recovery_smoke.sh
```

## Other validation

The full Lynx host suite passed, including new checks for bounded retry
rate, no duplicate movement/sounds, priority of turns/release, full replay
ring, timeout cap, phase/leave barriers and clock wrap.
The production ROM also passed Handy's 300 ms delayed-input movement checks:
60/120/160 ms taps each moved one cell, and the overlapping Up-to-Right turn
reached the expected cell. Those results are included in the JSON record.

`server_recovery_test.c` exercises the real server functions, including four
duplicate ACKs spaced 10 ms apart inside the fast-retry limit: zero resends
instead of the previous four. It also checks valid advancement, stale/unsent
ACKs, revision wrap, rejected input retry, deduplication before/after
application, neutral folding, input sequence wrap and the round barrier.

Existing reliable-event, input-queue, round-protocol, combat-world-authority
and TCP transport smoke tests passed against the updated server.

## Limits and next measurements

This addresses loss-induced stalls and unnecessary resend traffic. It does
not establish that either is the dominant source of continuous live lag.
The emulator peer does not reproduce hardware one-wire turnaround or WAN
jitter. Legacy cumulative ACKs still cannot recover an earlier lost action
after a newer admitted sequence has passed it; authoritative reconciliation
is required. Already transmitted inputs can still finish after release.

Next traffic work should measure and reduce redundant combat/metadata bursts,
beginning with same-tick shot-clear duplication, while preserving recovery
for the Atari and Linux clients. Compare frame interval and applied-ACK age
in live diagnostics to separate rendering cost from feedback delay.
