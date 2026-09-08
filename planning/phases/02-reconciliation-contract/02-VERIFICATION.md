---
phase: 02-reconciliation-contract
verified: 2026-04-09T00:44:14Z
status: passed
score: 4/4 must-haves verified
gaps: []
---

# Phase 2: Reconciliation Contract Verification Report

**Phase Goal:** The Atari client predicts locally, replays only unacknowledged inputs, and stays closely aligned with authoritative movement and maze collisions.
**Verified:** 2026-04-09T00:44:14Z
**Status:** passed
**Re-verification:** Yes — final verification after gap-closure plans `02-04` and `02-05`

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | The local Atari wizard moves immediately on input and remains visually smooth during normal play while still converging back to server truth. | ✓ VERIFIED | The approved `02-05` real Atari/FujiNet checkpoint confirmed smooth local play under ongoing snapshots, and `.planning/phases/02-reconciliation-contract/02-VALIDATION.md` now records Task `02-05-02` green. |
| 2 | When a correction is needed, the Atari wizard settles back to authoritative state without large teleports and with no more than about one maze cell of visible correction in normal play. | ✓ VERIFIED | `bash tests/reconciliation_correction_bound_smoke.sh` passed again, and the approved mixed-session checkpoint confirmed bounded correction and wall/corner respect remain intact on Atari. |
| 3 | Original Maze War movement and turning cadence remains recognizable on Atari while client prediction is active. | ✓ VERIFIED | `clients/atari/maze-war.asm` preserves cadence through `MOVRATE`, `MOVCLOK`, `MOVEST`, `INITMOVE_STEP`, `MOVEIM`, and `SETSTIL`, and the approved real Atari checkpoint explicitly accepted move/turn cadence feel. |
| 4 | Movement disagreements caused by stale local input are resolved by replaying only unacknowledged inputs instead of repeated snap-threshold retuning. | ✓ VERIFIED | `clients/atari/maze-war.asm` stages `ack_seq`, discards pending entries in `NET_LOCAL_ACK_DISCARD`, and replays remaining entries in `NET_LOCAL_REPLAY_PENDING`; `bash tests/reconciliation_replay_smoke.sh` passed again during final verification. |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `server/main.c` | Recipient-specific snapshot ack publication | ✓ VERIFIED | `build_snapshot()` writes byte 19, tick loop copies `last_delta_seq` to `applied_input_seq`, sets bit7 when valid, and sends 20-byte snapshots. |
| `doc/protocol.md` | Frozen 20-byte SNAPSHOT contract with ack semantics | ✓ VERIFIED | Documents `0x40 SNAPSHOT (20 bytes, S->C)`, `ack_valid` in flags bit7, and `ack_seq` in byte 19. |
| `tests/reconciliation_snapshot_ack_smoke.sh` | Real-server ack smoke proof | ✓ VERIFIED | Runs the real debug server, injects slot-0/slot-1 DELTAs, and asserts distinct `ack_seq` values; passed in final verification. |
| `clients/linux/main.c` | Ack decoding for Linux debug client | ✓ VERIFIED | Safely decodes `ack_valid` and `ack_seq` only when `n >= 20` and logs `snapshot ack pid=`. |
| `clients/linux/sdl_main.c` | Ack decoding for SDL Linux client | ✓ VERIFIED | Mirrors the 20-byte decode and debug logging. |
| `clients/atari/maze-war.asm` | Pending-input ring, ack discard/replay, bounded local correction via movement seams | ✓ VERIFIED | Contains `NET_LOCAL_INPUT_PUSH`, `NET_LOCAL_ACK_DISCARD`, `NET_LOCAL_REPLAY_PENDING`, `NET_RECON_P0` guard path, trigger-aware replay gating, and seam reuse through `SETSTIL`/`INITMOVE_STEP`/`MOVEIM`/collision checks. |
| `tests/reconciliation_replay_smoke.sh` | Replay-plumbing smoke test | ✓ VERIFIED | Greps for pending-ring and stage-commit replay hooks; passed in final verification. |
| `tests/reconciliation_correction_bound_smoke.sh` | Bounded-correction/collision-hook smoke test | ✓ VERIFIED | Greps for cadence and collision hooks and rejects direct `REMOTE_FOLLOW`/direct `LOCX`/`LOCY` replay writes; passed in final verification. |
| `tests/reconciliation_fire_input_smoke.sh` | Trigger-bearing replay/cadence smoke test | ✓ VERIFIED | Guards the fire-direction replay contract and passed during final verification. |
| `.planning/phases/02-reconciliation-contract/02-VALIDATION.md` | Final human-verification result | ✓ VERIFIED | Records Task `02-05-02` green and documents the approved real Atari/FujiNet cadence and fire-direction checkpoint. |
| `debug/debug.log` | Real-session transport evidence during final checkpoint | ✓ VERIFIED | Transport and snapshot flow remained healthy while the approved Atari session exercised movement, turn cadence, and fire-direction replay behavior. |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| `server/main.c` | `doc/protocol.md` | 20-byte `PKT_SNAPSHOT` layout | ✓ WIRED | Server sends `uint8_t pkt[20]`, sets bit7 for `ack_valid`, and writes `out[19]`; protocol doc matches that layout. |
| `clients/linux/main.c` | `server/main.c` | Snapshot ack decode/logging | ✓ WIRED | Linux client reads `buf[19]` only for 20-byte snapshots and logs `snapshot ack pid=`. |
| `clients/linux/sdl_main.c` | `server/main.c` | Snapshot ack decode/logging | ✓ WIRED | SDL client mirrors the same decode path and debug output. |
| `clients/atari/maze-war.asm` | `NET_STAGE_COMMIT` | Authoritative commit then discard/replay | ✓ WIRED | `NET_STAGE_COMMIT` copies staged ack state into live storage, then calls `NET_LOCAL_ACK_DISCARD` and `NET_LOCAL_REPLAY_PENDING`. |
| `clients/atari/maze-war.asm` | `NET_TX_BUILD_DELTA` | Pending ring capture at DELTA construction | ✓ WIRED | `NET_TX_BUILD_DELTA` calls `NET_LOCAL_INPUT_PUSH` after writing the transmitted seq and joy bytes. |
| `clients/atari/maze-war.asm` | `INITMOVE_STEP` / `MOVEIM` / `SETSTIL` / `CHKXDIR` / `CHKYDIR` / `GETAHEDM` | Replay/correction path preserves cadence and collision hooks | ✓ WIRED | Replay and local correction route through the existing movement engine rather than direct coordinate stepping, with trigger-bearing replay gated away from spurious movement. |
| Implemented reconciliation code | Phase 2 goal | Real Atari/FujiNet approval | ✓ WIRED | The user approved the `02-05` mixed Atari/FujiNet checkpoint after the cadence/fire replay fix, closing the remaining Phase 2 user-visible blockers. |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| RECN-01 | `02-01-PLAN.md` | Server snapshots tell each client which local input sequence has been authoritatively applied for that recipient. | ✓ SATISFIED | `server/main.c`, `doc/protocol.md`, and `tests/reconciliation_snapshot_ack_smoke.sh`; smoke passed with slot 0 `ack_seq=7` and slot 1 `ack_seq=33`. |
| RECN-02 | `02-02-PLAN.md` | Atari client stores pending local inputs and replays only unacknowledged inputs after applying an authoritative correction. | ✓ SATISFIED | `clients/atari/maze-war.asm` implements the pending ring, ack discard, and replay at `NET_STAGE_COMMIT`; replay smoke passed. |
| RECN-03 | `02-03-PLAN.md` | Atari local wizard movement remains visually smooth under normal play and corrects by no more than one maze cell when reconciliation is required. | ✓ SATISFIED | `.planning/phases/02-reconciliation-contract/02-VALIDATION.md` records Task `02-05-02` green after approved real Atari verification, with bounded correction and wall/corner respect confirmed. |
| RECN-04 | `02-03-PLAN.md` | Atari client preserves original Maze War movement and turning animation cadence while using client prediction. | ✓ SATISFIED | The approved `02-05` checkpoint explicitly accepted original-feeling move/turn cadence on real Atari after trigger-aware replay gating. |

All requirement IDs declared in Phase 2 plans are accounted for, and `REQUIREMENTS.md` maps no extra orphaned Phase 2 requirements beyond `RECN-01` through `RECN-04`.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| `clients/atari/maze-war.asm` | 572 | `placeholder` comment | ⚠️ Warning | Comment indicates temporary net-only coordinates during boot/respawn, but it does not affect the now-verified reconciliation contract. |

### Human Verification

Phase 2 required real Atari/FujiNet approval for the final two user-visible properties, and that approval has now been recorded. The final checkpoint reran the automated gate (`bash tests/reconciliation_snapshot_ack_smoke.sh`, `bash tests/reconciliation_replay_smoke.sh`, `bash tests/reconciliation_correction_bound_smoke.sh`, `bash tests/reconciliation_fire_input_smoke.sh`, `make all`) before the mixed-session Atari test, and the user then approved cadence, fire-direction behavior, and bounded correction.

### Verification Summary

Phase 2 now meets its full goal. The server publishes recipient-specific `ack_seq`, Linux clients decode it, the Atari client stores pending local inputs, discards acknowledged entries, and replays only remaining inputs through the original movement/collision seams. The final gap-closure fix also separates trigger-bearing replay from movement replay so firing while steering no longer causes a transient jump and snap-back. All four reconciliation smoke checks passed during final verification, `make all` is green, and the blocking real Atari/FujiNet checkpoint is approved.

---

_Verified: 2026-04-09T00:44:14Z_
_Verifier: Claude (gsd-verifier)_
