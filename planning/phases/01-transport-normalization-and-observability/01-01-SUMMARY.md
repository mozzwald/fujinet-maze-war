---
phase: 01-transport-normalization-and-observability
plan: 01
subsystem: networking
tags: [udp, transport, observability, c, testing]
requires: []
provides:
  - Canonical DELTA ingress normalization before gameplay input mutation
  - Smoke coverage for primary, swapped, and extra-leading-0x41 DELTA variants
  - Debug acceptance logs that identify normalized DELTA format per slot
affects: [phase-01-plan-02, reconciliation, mixed-session-validation]
tech-stack:
  added: []
  patterns: [dedicated transport normalization module, smoke-script transport regression checks]
key-files:
  created:
    - server/transport_normalize.h
    - server/transport_normalize.c
    - tests/transport_normalize_smoke.sh
    - tests/README-transport-validation.md
  modified:
    - server/main.c
    - Makefile
key-decisions:
  - "Transport framing repair and DELTA compatibility decoding now live in a dedicated server module so gameplay handlers only see canonical DELTA packets."
  - "Transport regression coverage is anchored on exact debug acceptance markers from the real server binary, not on a synthetic unit harness."
patterns-established:
  - "DELTA ingress is normalized before any players[pid].joy mutation."
  - "Mixed-session transport validation keeps a captured server log alongside smoke or manual session output."
requirements-completed: [TRAN-01]
duration: 9 min
completed: 2026-04-08
---

# Phase 01 Plan 01: Transport Boundary Summary

**Canonical DELTA ingress normalization with smoke coverage for primary, swapped, and extra-leading-0x41 FujiNet variants**

## Performance

- **Duration:** 9 min
- **Started:** 2026-04-08T10:54:00Z
- **Completed:** 2026-04-08T11:03:15Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Added `tests/transport_normalize_smoke.sh` to launch the debug server, inject supported DELTA variants, and fail if canonical acceptance markers disappear.
- Documented the smoke log path and the manual Atari mixed-session capture flow in `tests/README-transport-validation.md`.
- Extracted byte-stream framing repair and DELTA decoding into `server/transport_normalize.c`, with `server/main.c` logging `transport accepted slot=` immediately before gameplay input state changes.

## Task Commits

1. **Task 1: Create Wave 0 transport normalization smoke coverage** - `7217687` (feat)
2. **Task 2: Extract canonical DELTA normalization into a dedicated server module** - `c62a6f5` (feat)

## Files Created/Modified

- `server/transport_normalize.h` - Transport framing and canonical DELTA contracts.
- `server/transport_normalize.c` - Byte-stream packet assembly, resync, extra-`0x41` handling, and DELTA decoding.
- `server/main.c` - Server wiring for transport ingress and canonical acceptance logging.
- `Makefile` - Server target updated to compile the new transport module.
- `tests/transport_normalize_smoke.sh` - Automated DELTA ingress smoke harness.
- `tests/README-transport-validation.md` - Smoke log usage and manual Atari mixed-session validation notes.

## Decisions Made

- Moved DELTA compatibility parsing behind `transport_rx_push_byte()` and `transport_decode_delta_for_slot()` so higher-level server logic no longer owns FujiNet framing quirks.
- Kept slot identity authoritative in `server/main.c`; DELTA packets are accepted only when the normalized decode matches the slot receiving bytes.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed low-sequence extra-`0x41` DELTA detection**
- **Found during:** Task 2
- **Issue:** The first transport module pass misclassified `[0x41][0x41][seq][pid][joy]` packets as resynced primary DELTAs when `seq < MAX_PLAYERS`.
- **Fix:** Tightened the framing repair heuristic so a second leading `0x41` plus a slot-range pid keeps the frame in five-byte mode and decodes as `extra-41`.
- **Files modified:** `server/transport_normalize.c`
- **Verification:** `bash tests/transport_normalize_smoke.sh`
- **Committed in:** `c62a6f5`

**2. [Rule 3 - Blocking] Updated the server build target for the new transport module**
- **Found during:** Task 2
- **Issue:** Extracting normalization into `server/transport_normalize.c` required the server target to compile an additional translation unit.
- **Fix:** Updated `Makefile` so `build/maze-war-server` links both `server/main.c` and `server/transport_normalize.c`.
- **Files modified:** `Makefile`
- **Verification:** `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **Committed in:** `c62a6f5`

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both fixes were required to make the planned transport boundary executable and verifiable without changing scope.

## Issues Encountered

- Task 1's smoke script could not pass against the pre-refactor server because the required `transport accepted slot=` markers did not exist yet. The server refactor supplied those markers and cleared the verifier.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 1 now has a frozen DELTA ingress seam and a repeatable smoke check for the supported transport variants.
- Plan `01-02` can build observability counters and summaries on top of the canonical acceptance boundary instead of debugging raw gameplay handlers.

## Self-Check: PASSED

- FOUND: `.planning/phases/01-transport-normalization-and-observability/01-01-SUMMARY.md`
- FOUND: `7217687`
- FOUND: `c62a6f5`
