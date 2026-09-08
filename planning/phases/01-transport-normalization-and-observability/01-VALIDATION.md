---
phase: 1
slug: transport-normalization-and-observability
status: draft
nyquist_compliant: true
wave_0_complete: false
created: 2026-04-07
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | shell-script smoke checks plus existing repo builds |
| **Config file** | none — Wave 0 installs the first phase-specific smoke scripts |
| **Quick run command** | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` |
| **Full suite command** | `make all` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **After every plan wave:** Run `make all`
- **Before `$gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | TRAN-01 | smoke/manual integration | `bash tests/transport_normalize_smoke.sh` | ❌ created by task | ⬜ pending |
| 01-01-02 | 01 | 1 | TRAN-01 | smoke + build regression | `bash tests/transport_normalize_smoke.sh && make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ created by task | ⬜ pending |
| 01-02-01 | 02 | 2 | TRAN-02 | build/wiring verification | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ created by task | ⬜ pending |
| 01-02-02 | 02 | 2 | TRAN-02 | counter smoke + full regression | `bash tests/transport_counters_smoke.sh && make all` | ❌ created by task | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/transport_normalize_smoke.sh` — scripted server + Linux canonical client smoke run for DELTA acceptance
- [ ] `tests/transport_counters_smoke.sh` — verifies debug summary contains normalization/drop counters after synthetic input
- [ ] `tests/README-transport-validation.md` — notes for capturing Atari or FujiNet emulator runs alongside server logs

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Atari NetStream input reaches the canonical server ingress path without join or input failure | TRAN-01 | Real Atari or emulator path is not yet scripted in repo automation | Run server with `--debug`, connect Atari path plus one Linux client, move on both clients, and confirm server logs show accepted canonical DELTAs for both paths |
| Debug output cleanly distinguishes normalized framing quirks from later sync issues in a mixed session | TRAN-02 | Requires observing live counters/logs while mixed traffic is active | Run mixed Atari plus Linux session, trigger normal movement, then inspect debug summaries for accepted, normalized, resync, stale-seq, and drop counters without needing to infer behavior from raw event spam |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
