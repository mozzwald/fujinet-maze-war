---
phase: 3
slug: combat-and-world-authority
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-04-08
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | shell-script smoke checks plus repo build targets |
| **Config file** | none |
| **Quick run command** | `bash tests/combat_ordering_smoke.sh && make build/maze-war-server build/maze-war-client` |
| **Full suite command** | `make all && bash tests/transport_normalize_smoke.sh && bash tests/transport_counters_smoke.sh && bash tests/reconciliation_snapshot_ack_smoke.sh && bash tests/reconciliation_replay_smoke.sh && bash tests/reconciliation_correction_bound_smoke.sh && bash tests/reconciliation_fire_input_smoke.sh && bash tests/combat_ordering_smoke.sh && bash tests/combat_world_authority_smoke.sh && bash tests/combat_client_parity_smoke.sh` |
| **Estimated runtime** | ~30 seconds on focused path, ~60 seconds full suite |

---

## Sampling Rate

- **After every task commit:** Run the active task's `<automated>` command; prefer the focused smoke path unless the task changes all three targets.
- **After every plan wave:** Run `bash tests/combat_ordering_smoke.sh && bash tests/combat_world_authority_smoke.sh`.
- **Before `$gsd-verify-work`:** Full suite must be green.
- **Max feedback latency:** 30 seconds on the focused task path.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | COMB-03 | smoke/doc | `bash tests/combat_ordering_smoke.sh && make build/maze-war-server` | ❌ Wave 0 | ⬜ pending |
| 03-01-02 | 01 | 1 | COMB-01 / WRLD-01 | smoke/integration | `bash tests/combat_ordering_smoke.sh && bash tests/combat_world_authority_smoke.sh && make build/maze-war-server` | ❌ Wave 0 | ⬜ pending |
| 03-02-01 | 02 | 2 | COMB-02 | smoke/build | `bash tests/combat_client_parity_smoke.sh && make build/maze-war-client` | ❌ Wave 0 | ⬜ pending |
| 03-02-02 | 02 | 2 | WRLD-02 | smoke/build | `bash tests/combat_world_authority_smoke.sh && make build/maze-war-client` | ❌ Wave 0 | ⬜ pending |
| 03-03-01 | 03 | 3 | COMB-04 / WRLD-01 | smoke/build | `bash tests/combat_client_parity_smoke.sh && bash tests/combat_ordering_smoke.sh && make build/maze-war-client build/maze-war-client-sdl` | ❌ Wave 0 | ⬜ pending |
| 03-03-02 | 03 | 3 | COMB-02 / COMB-04 / WRLD-02 | smoke/manual Atari checkpoint | `bash tests/combat_ordering_smoke.sh && bash tests/combat_world_authority_smoke.sh && bash tests/combat_client_parity_smoke.sh && make all` | ❌ Wave 0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/combat_ordering_smoke.sh` — exercises authoritative move/turn/fire ordering, immediate adjacent hit, and fire-into-brick behavior against the real server.
- [ ] `tests/combat_world_authority_smoke.sh` — proves score/respawn/brick outcomes come from server-authoritative events, not client-local derivation.
- [ ] `tests/combat_client_parity_smoke.sh` — verifies Linux and Atari code paths consume the frozen combat/world contract symbols and packet semantics consistently.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Atari bullets visibly originate from the currently visible wizard position and facing | COMB-02 | The repo has no automated Atari visual-origin oracle | Run a mixed session with 1 Atari client, 1 Linux client, and 2 AI zombies; repeat move-then-fire and turn-then-fire sequences on Atari and confirm bullets never appear to spawn from a stale cell or stale facing |
| Atari and Linux show the same visible combat outcome for identical input sequences | COMB-04 | Human comparison is needed for visible parity across two different frontends | In the mixed session, reproduce the same move-then-fire and turn-then-fire sequence on each client in turn, then confirm shot direction, hit/no-hit outcome, and brick destruction match |
| Score, death, and respawn results stay aligned with authoritative world outcomes during live play | WRLD-02 | Real-session observation is needed to confirm no stale dead/alive or scoreboard artifacts remain on Atari | During combat exchanges, confirm that kills increment the shooter score once, victims hide on pending respawn, and final respawn returns them at the server-selected cell with no stale body/shot debris |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency <= 30s on the focused task-level validation path
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending Phase 3 execution and the real Atari/FujiNet mixed-session checkpoint.
