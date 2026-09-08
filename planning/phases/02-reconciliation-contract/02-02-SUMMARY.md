---
phase: 02-reconciliation-contract
plan: 02
subsystem: gameplay
tags: [atari, reconciliation, ack, replay, assembly, testing]
requires:
  - phase: 02-reconciliation-contract
    provides: "SNAPSHOT byte 19 ack_seq contract and recipient-specific ack semantics"
provides:
  - "Atari pending-input ring for transmitted local DELTAs"
  - "Ack staging/live storage at snapshot apply and stage commit"
  - "Ack discard and replay helpers wired at NET_STAGE_COMMIT"
  - "Replay smoke coverage for Atari reconciliation plumbing"
affects: [phase-02-03, atari-client, movement-reconciliation]
tech-stack:
  added: []
  patterns: ["authoritative stage-commit replay", "bounded pending-input ring"]
key-files:
  created: [tests/reconciliation_replay_smoke.sh]
  modified: [clients/atari/maze-war.asm]
key-decisions:
  - "Replay runs from NET_STAGE_COMMIT after authoritative state is committed, not from a parallel simulation path."
  - "The Atari client keeps an eight-entry pending ring and discards entries with modulo-256 seq <= ack_seq."
patterns-established:
  - "Snapshot ack metadata is staged first and copied live only at the VBI commit seam."
  - "Local reconciliation resets from authoritative slot state, then replays only still-pending local DELTAs."
requirements-completed: [RECN-02]
duration: 6m
completed: 2026-04-08
---

# Phase 02 Plan 02: Reconciliation Replay Summary

**Atari-side pending DELTA history with staged ack handoff and authoritative replay of only unacknowledged local inputs**

## Performance

- **Duration:** 6 min
- **Started:** 2026-04-08T22:07:07Z
- **Completed:** 2026-04-08T22:13:27Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Added staged/live ack storage and widened snapshot collection so the Atari client carries `ack_seq` through the existing snapshot pipeline.
- Captured transmitted local DELTAs in an eight-entry pending ring and discarded only entries covered by the authoritative ack.
- Replayed remaining local inputs from the authoritative stage-commit seam and added a smoke script that proves the replay hooks exist there.

## Task Commits

Each task was committed atomically:

1. **Task 1: Add ack staging and a compact pending-input ring to the Atari client** - `2b08c21` (feat)
2. **Task 2: Replay only unacknowledged inputs at the stage-commit boundary** - `f120125` (feat)

## Files Created/Modified
- `clients/atari/maze-war.asm` - Adds ack staging/live state, pending-input storage, ack discard, and local replay wiring.
- `tests/reconciliation_replay_smoke.sh` - Builds the client and greps for the required ring and replay hooks.

## Decisions Made
- Replay is anchored on `NET_STAGE_COMMIT` so authoritative slot state is committed before local prediction is rebuilt.
- Ack discard uses modulo-256 forward comparison and removes only pending entries covered by `NET_ACK_SEQ`.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

- `clients/atari/maze-war.asm` already had unrelated worktree edits, so task commits were created from clean `HEAD` trees with git plumbing to avoid committing user changes.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase `02-03` can now replace threshold-only local correction with the new ack-driven discard/replay path.
- Manual Atari/FujiNet validation is still needed later for visible correction size and movement cadence.

---
*Phase: 02-reconciliation-contract*
*Completed: 2026-04-08*

## Self-Check: PASSED

- FOUND: `.planning/phases/02-reconciliation-contract/02-02-SUMMARY.md`
- FOUND: `2b08c21`
- FOUND: `f120125`
