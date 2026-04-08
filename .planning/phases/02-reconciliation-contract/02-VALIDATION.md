---
phase: 2
slug: reconciliation-contract
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-08
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | shell-script smoke checks plus repo build targets |
| **Config file** | none |
| **Quick run command** | `make build/maze-war-client && bash tests/reconciliation_correction_bound_smoke.sh` |
| **Full suite command** | `make all && bash tests/transport_normalize_smoke.sh && bash tests/transport_counters_smoke.sh` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run the active task's `<automated>` command; prefer the focused smoke path (`make build/maze-war-client && bash tests/reconciliation_correction_bound_smoke.sh`) unless the task explicitly changes server/Linux targets.
- **After every plan wave:** Run `make all && bash tests/reconciliation_snapshot_ack_smoke.sh`
- **Before `$gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | RECN-01 | smoke | `bash tests/reconciliation_snapshot_ack_smoke.sh` | ❌ Wave 0 | ⬜ pending |
| 02-01-02 | 01 | 1 | RECN-01 | build/smoke | `bash tests/reconciliation_snapshot_ack_smoke.sh && make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ Wave 0 | ⬜ pending |
| 02-02-01 | 02 | 2 | RECN-02 | smoke/integration | `bash tests/reconciliation_replay_smoke.sh && make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ Wave 0 | ⬜ pending |
| 02-02-02 | 02 | 2 | RECN-02 | smoke/build | `bash tests/reconciliation_replay_smoke.sh && make build/maze-war-client` | ❌ Wave 0 | ⬜ pending |
| 02-03-01 | 03 | 3 | RECN-04 | smoke/build | `bash tests/reconciliation_correction_bound_smoke.sh && make build/maze-war-client` | ❌ Wave 0 | ✅ green |
| 02-03-02 | 03 | 3 | RECN-03 | smoke/manual Atari checkpoint | `bash tests/reconciliation_snapshot_ack_smoke.sh && bash tests/reconciliation_replay_smoke.sh && bash tests/reconciliation_correction_bound_smoke.sh && make all` | ❌ Wave 0 | ❌ red |
| 02-04-01 | 04 | 4 | RECN-03 | smoke/build | `bash tests/reconciliation_correction_bound_smoke.sh && bash tests/reconciliation_replay_smoke.sh && make build/maze-war-client` | ❌ Wave 0 | ✅ green |
| 02-04-02 | 04 | 4 | RECN-03 / RECN-04 | smoke/manual Atari checkpoint | `bash tests/reconciliation_snapshot_ack_smoke.sh && bash tests/reconciliation_replay_smoke.sh && bash tests/reconciliation_correction_bound_smoke.sh && make all` | ❌ Wave 0 | ❌ red |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/reconciliation_snapshot_ack_smoke.sh` — launches the real server, injects canonical DELTAs, and asserts per-recipient `ack_seq`
- [ ] `tests/reconciliation_replay_smoke.sh` — drives deterministic local input sequences through Linux client or scripted UDP sender and checks ack discard behavior
- [ ] `tests/reconciliation_correction_bound_smoke.sh` — exercises stale input and correction paths and enforces bounded divergence markers plus preserved collision hooks for wall/corner replay
- [ ] Linux debug output for ack state in `clients/linux/main.c` and optionally `clients/linux/sdl_main.c`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Atari movement cadence remains recognizably on the original move/turn pipeline while prediction is active | RECN-04 | The repo has no automated Atari animation oracle | Run a mixed Atari session after Phase 2, move and turn repeatedly, and confirm cadence still follows the original movement engine rather than a new interpolation loop |
| Correction magnitude stays visually bounded in real Atari play and replay respects authoritative maze walls/corners | RECN-03 | Human observation is needed to judge visible snap distance and collision fidelity in the real client | Force temporary divergence during mixed-session movement near walls/corners, then confirm corrections settle without large teleports, remain within about one maze cell in normal play, and never clip through maze boundaries during replay |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency <= 30s on the focused task-level validation path
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** still failed on 2026-04-08 after the `02-04` retry. The stale sprite ghosting blocker was cleared, walls were respected, and visible correction stayed within about one cell, but RECN-04 was not approved because movement still feels too fast. The checkpoint also surfaced a follow-up gameplay regression: holding fire while steering toward the shot direction can make the local player jump into the next cell and then snap back while firing.
