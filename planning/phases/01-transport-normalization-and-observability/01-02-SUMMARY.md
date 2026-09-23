---
phase: 01-transport-normalization-and-observability
plan: 02
subsystem: networking
tags: [udp, transport, observability, c, testing]
requires:
  - phase: 01-01
    provides: canonical DELTA normalization boundary and acceptance smoke coverage
provides:
  - Per-slot and global transport counters for accepted, repaired, stale, and dropped DELTAs
  - Periodic and disconnect-time `transport summary` logging in the real server
  - Counter smoke coverage and documented canonical DELTA/debug workflow
affects: [phase-02, mixed-session-validation, debugging]
tech-stack:
  added: []
  patterns: [transport summary counters, real-server log verification]
key-files:
  created:
    - server/transport_stats.h
    - server/transport_stats.c
    - tests/transport_counters_smoke.sh
  modified:
    - server/main.c
    - server/transport_normalize.h
    - server/transport_normalize.c
    - Makefile
    - tests/README-transport-validation.md
    - doc/protocol.md
    - README.md
key-decisions:
  - "Transport observability stays server-first: counters live beside the canonical ingress path and are emitted as stable summary lines instead of ad hoc event spam."
  - "Counter verification is anchored on the real debug server binary, with the stale-sequence case intentionally exercising an extra-41 DELTA so accepted_delta stays distinct from format counters."
patterns-established:
  - "Transport debugging starts with `transport accepted` and `transport summary` prefixes before any Atari mixed-session investigation."
  - "Per-slot and global transport counters are updated at ingress, then dumped on cadence and slot teardown."
requirements-completed: [TRAN-02]
duration: 6 min
completed: 2026-04-08
---

# Phase 01 Plan 02: Transport Observability Summary

**Per-slot/global transport counters with timed summary logging and smoke-verified DELTA debug workflow**

## Performance

- **Duration:** 6 min
- **Started:** 2026-04-08T11:04:30Z
- **Completed:** 2026-04-08T11:10:29Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments

- Added `server/transport_stats.*` and wired `server/main.c` to track raw datagrams, normalized DELTA formats, resyncs, stale drops, bad-joy drops, and accepted DELTAs per slot plus globally.
- Emitted `transport summary slot=` lines every 2000 ms in `--debug` mode and again when slots time out or the server shuts down.
- Added `tests/transport_counters_smoke.sh` plus protocol and README updates so transport debugging now starts from repeatable smoke checks and frozen counter names.

## Task Commits

1. **Task 1: Add per-slot transport counters and periodic debug summaries** - `309ad11` (feat)
2. **Task 2: Add counter smoke coverage and freeze the transport contract in docs** - `8c8ce2d` (feat)

## Files Created/Modified

- `server/transport_stats.h` - Public counter structure and summary logging API.
- `server/transport_stats.c` - Counter helpers and stable `transport summary` formatting.
- `server/main.c` - Counter updates, timed summary cadence, disconnect dumps, and DELTA drop accounting.
- `server/transport_normalize.h` - Minimal resync counter exposure for observability.
- `server/transport_normalize.c` - Parser-side resync tracking consumed by the server counters.
- `Makefile` - Server target updated to link the new transport stats module.
- `tests/transport_counters_smoke.sh` - Real-server smoke harness for exact summary counter values.
- `tests/README-transport-validation.md` - Debug capture flow updated to require both smoke scripts and both transport prefixes.
- `doc/protocol.md` - Canonical DELTA form and supported transport summary counters documented.
- `README.md` - Repo-level transport debug workflow updated.

## Decisions Made

- Kept observability in the server ingress path rather than adding client-side diagnostics first, because Phase 1 only needed a hard line between framing faults and later gameplay faults.
- Counted DELTA format selection separately from `accepted_delta`, so repaired packets that later fail stale-sequence filtering remain visible in summaries.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Exposed parser resync counts from the transport normalizer**
- **Found during:** Task 1
- **Issue:** `server/main.c` needed an exact `delta_resync` signal without duplicating or re-implementing the normalizer's 4-byte window shift heuristic.
- **Fix:** Added a minimal resync counter to `struct transport_rx_state` and exported `transport_rx_take_resync_count()` for the server loop.
- **Files modified:** `server/transport_normalize.h`, `server/transport_normalize.c`
- **Verification:** `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **Committed in:** `309ad11`

**2. [Rule 3 - Blocking] Updated the server build target for the new stats module**
- **Found during:** Task 1
- **Issue:** Adding `server/transport_stats.c` required the server target to link another translation unit.
- **Fix:** Updated `Makefile` so `build/maze-war-server` compiles `server/transport_stats.c` alongside the existing server sources.
- **Files modified:** `Makefile`
- **Verification:** `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **Committed in:** `309ad11`

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes were required to make the planned observability layer measurable and buildable without changing phase scope.

## Issues Encountered

- One timeout callsite still used the pre-observability `reap_timed_out_clients()` signature during Task 1 wiring. Updating that call restored a clean build before the task commit.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 1 now has both canonical DELTA ingress and durable transport counters, so Phase 2 can focus on reconciliation instead of packet framing ambiguity.
- Mixed-session testers can reproduce transport acceptance and drop behavior locally before any Atari manual validation run.

## Self-Check: PASSED

- FOUND: `.planning/phases/01-transport-normalization-and-observability/01-02-SUMMARY.md`
- FOUND: `309ad11`
- FOUND: `8c8ce2d`
