---
phase: 02-reconciliation-contract
plan: 01
subsystem: networking
tags: [udp, reconciliation, snapshots, protocol, testing]
requires:
  - phase: 01-transport-normalization-and-observability
    provides: canonical DELTA ingress and server-first transport debug seams
provides:
  - 20-byte SNAPSHOT contract with recipient-specific ack publication
  - authoritative server tracking for applied local DELTA sequence per recipient
  - real-server smoke coverage proving slot 0 and slot 1 can receive different ack_seq values
affects: [phase-02-replay, atari-client, linux-client, server, protocol-docs]
tech-stack:
  added: [posix-shell, python3]
  patterns: [recipient-specific snapshot metadata, real-server UDP smoke harness]
key-files:
  created: [tests/reconciliation_snapshot_ack_smoke.sh]
  modified: [server/main.c, clients/linux/main.c, clients/linux/sdl_main.c, doc/protocol.md]
key-decisions:
  - "SNAPSHOT stays a single packet type and exposes reconciliation state via flags bit7 plus byte 19 instead of adding a second acknowledgement packet."
  - "Server publishes ack_seq from authoritative applied-input progress after each tick rather than reusing transport receipt state."
patterns-established:
  - "Recipient-specific snapshot metadata: shared snapshot body plus per-recipient flags and ack byte in the send loop."
  - "Protocol verification uses the real debug server with direct UDP packet injection before Atari-side replay work."
requirements-completed: [RECN-01]
duration: 3m 6s
completed: 2026-04-08
---

# Phase 02 Plan 01: Reconciliation Contract Summary

**20-byte SNAPSHOT packets now carry recipient-specific authoritative ack_seq state, and a real debug-server smoke harness proves different clients can receive different ack bytes in one run**

## Performance

- **Duration:** 3m 6s
- **Started:** 2026-04-08T22:01:51Z
- **Completed:** 2026-04-08T22:04:57Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments

- Froze the SNAPSHOT wire contract at 20 bytes with `ack_valid` in flags bit7 and `ack_seq` in byte 19.
- Added authoritative `applied_input_seq` tracking on the server and Linux-side debug decoding for the new ack fields.
- Added `tests/reconciliation_snapshot_ack_smoke.sh` to validate recipient-specific ack publication against the real debug server.

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend SNAPSHOT to a 20-byte ack-aware contract** - `ced1132` (feat)
2. **Task 2: Add a real-server smoke test for recipient-specific ack_seq** - `d9c195a` (test)

## Files Created/Modified

- `server/main.c` - Publishes per-recipient authoritative `ack_seq` in 20-byte snapshots.
- `clients/linux/main.c` - Safely decodes snapshot ack state and logs `snapshot ack pid=` in debug mode.
- `clients/linux/sdl_main.c` - Mirrors the Linux debug decode and logging for the SDL client.
- `doc/protocol.md` - Freezes the 20-byte SNAPSHOT contract and documents `ack_valid`/`ack_seq` semantics.
- `tests/reconciliation_snapshot_ack_smoke.sh` - Runs a two-socket UDP smoke check against the real debug server.

## Decisions Made

- SNAPSHOT remains the reconciliation carrier, with ack state embedded in existing packet flow instead of introducing a new packet type.
- Published ack state comes from authoritative applied-input progress after `step_players(...)`, so it reflects simulation progress rather than raw receipt order.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- RECN-01 is now anchored by a frozen wire contract and executable server-side proof.
- Phase `02-02` can build Atari pending-input discard and replay on top of explicit `ack_valid` and `ack_seq` semantics.

## Self-Check: PASSED

- Verified `.planning/phases/02-reconciliation-contract/02-01-SUMMARY.md` exists.
- Verified task commits `ced1132` and `d9c195a` exist in git history.

---
*Phase: 02-reconciliation-contract*
*Completed: 2026-04-08*
