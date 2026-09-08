---
phase: 01-transport-normalization-and-observability
verified: 2026-04-08T16:40:00Z
status: passed
score: 5/5 must-haves verified
human_verification: []
---

# Phase 01: Transport Normalization and Observability Verification Report

**Phase Goal:** Clients reach gameplay simulation through one canonical transport path, with enough observability to separate framing faults from sync faults.
**Verified:** 2026-04-08T16:40:00Z
**Status:** passed
**Re-verification:** No - initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Server routes Linux and Atari-originated DELTA input through one canonical normalization boundary before mutating gameplay input state. | ✓ VERIFIED | [`server/main.c` line 456](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L456) decodes through `transport_decode_delta_for_slot()`, and only then writes gameplay input at [`server/main.c` line 493](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L493). Byte ingress is funneled through `transport_rx_push_byte()` at [`server/main.c` line 549](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L549). |
| 2 | Canonical DELTA, swapped seq/pid DELTA, and extra-leading-0x41 NetStream DELTA all decode into the same internal packet shape. | ✓ VERIFIED | [`server/transport_normalize.c` lines 110-132](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_normalize.c#L110) map primary, swapped, and extra-`0x41` packets into one `struct transport_delta_packet`; the smoke script covers all three forms at [`tests/transport_normalize_smoke.sh` lines 39-42](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_normalize_smoke.sh#L39). |
| 3 | An automated smoke script exists for the canonical ingress path and fails if normalization stops accepting the supported variants. | ✓ VERIFIED | [`tests/transport_normalize_smoke.sh` lines 27-59](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_normalize_smoke.sh#L27) launches the real debug server, injects packets, and greps exact `transport accepted` markers. `bash tests/transport_normalize_smoke.sh` passed during verification. |
| 4 | A tester can separate transport normalization problems from later gameplay or reconciliation problems by reading counters and summary lines. | ✓ VERIFIED | Summary logging is emitted from [`server/transport_stats.c` lines 41-55](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_stats.c#L41), wired on cadence/shutdown in [`server/main.c` lines 1194-1197](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L1194) and [`server/main.c` line 1233](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L1233). Counter semantics are documented in [`doc/protocol.md` lines 104-116](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L104). |
| 5 | The transport contract and debug workflow are documented in the repo so later phases do not reintroduce implicit framing assumptions. | ✓ VERIFIED | [`doc/protocol.md` lines 76-116](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L76), [`README.md` lines 154-163](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md#L154), and [`tests/README-transport-validation.md` lines 1-24](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/README-transport-validation.md#L1) define the canonical DELTA form, counters, smoke scripts, and manual capture flow. |

**Score:** 5/5 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `server/transport_normalize.h` | Transport normalization contracts and canonical DELTA types | ✓ VERIFIED | Exports `transport_rx_push_byte`, `transport_rx_take_resync_count`, and `transport_decode_delta_for_slot` at [`server/transport_normalize.h` lines 7-42](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_normalize.h#L7). Imported and used from [`server/main.c` lines 14-15](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L14). |
| `server/transport_normalize.c` | Byte-stream framing repair and DELTA normalization logic | ✓ VERIFIED | Implements extra-`0x41` handling, resync shift, swapped decode, and joy validation at [`server/transport_normalize.c` lines 36-136](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_normalize.c#L36). Compiled into the server via [`Makefile` lines 24-25](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile#L24). |
| `tests/transport_normalize_smoke.sh` | Automated DELTA normalization smoke coverage | ✓ VERIFIED | Covers `primary`, `swapped`, and `extra-0x41` payloads and greps exact acceptance markers at [`tests/transport_normalize_smoke.sh` lines 33-59](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_normalize_smoke.sh#L33). |
| `server/transport_stats.h` | Transport counter structures and summary logging API | ✓ VERIFIED | Defines the required counters and API at [`server/transport_stats.h` lines 10-27](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_stats.h#L10). Imported by [`server/main.c` line 14](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L14). |
| `tests/transport_counters_smoke.sh` | Automated counter/log verification | ✓ VERIFIED | Starts the real debug server and asserts summary counters at [`tests/transport_counters_smoke.sh` lines 27-65](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_counters_smoke.sh#L27). `bash tests/transport_counters_smoke.sh` passed during verification. |
| `doc/protocol.md` | Documented canonical ingress contract and debug counters | ✓ VERIFIED | Documents canonical DELTA form and transport summary counters at [`doc/protocol.md` lines 91-116](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L91). Linked from [`README.md` line 152](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md#L152). |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| `server/main.c` | `server/transport_normalize.c` | `process_client_bytes` and DELTA dispatch | ✓ WIRED | `process_client_bytes()` calls `transport_rx_push_byte()` at [`server/main.c` line 549](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L549), then `handle_client_packet()` calls `transport_decode_delta_for_slot()` at [`server/main.c` line 456](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L456). |
| `tests/transport_normalize_smoke.sh` | `build/maze-war-server` | debug server launch plus UDP packet injection | ✓ WIRED | The script builds and launches `build/maze-war-server --port "$PORT" --zombies 0 --debug` at [`tests/transport_normalize_smoke.sh` lines 27-30](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_normalize_smoke.sh#L27). |
| `server/main.c` | `server/transport_stats.c` | counter increments and periodic summary logging | ✓ WIRED | Raw-byte counters increment at [`server/main.c` lines 1185-1186](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L1185), format/drop/accept counters update at [`server/main.c` lines 472-476](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L472) and [`server/main.c` lines 495-496](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L495), and summaries emit every 2000 ms at [`server/main.c` lines 1194-1197](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L1194). |
| `tests/transport_counters_smoke.sh` | server debug output | grep for counter names and values | ✓ WIRED | The script requires `transport summary slot=0`, `delta_swapped=1`, `delta_extra_41=1`, `drop_bad_joy=1`, `drop_stale_seq=1`, and `accepted_delta=2` at [`tests/transport_counters_smoke.sh` lines 58-65](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_counters_smoke.sh#L58). |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| `TRAN-01` | `01-01-PLAN.md` | Server accepts one canonical client input packet format and normalizes FujiNet NetStream framing before gameplay logic runs. | ✓ SATISFIED | DELTA ingress is normalized before `players[pid].joy` mutation in [`server/main.c` lines 456-493](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L456), and the supported variants are exercised by `bash tests/transport_normalize_smoke.sh`. |
| `TRAN-02` | `01-02-PLAN.md` | Server exposes enough packet/debug counters or logs to distinguish transport-framing problems from gameplay reconciliation problems during mixed-session testing. | ✓ SATISFIED | Counter structures, summary logging, and docs exist in [`server/transport_stats.h` lines 10-27](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_stats.h#L10), [`server/transport_stats.c` lines 35-55](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/transport_stats.c#L35), [`doc/protocol.md` lines 104-116](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L104), and `bash tests/transport_counters_smoke.sh` passed. |

All requirement IDs declared in plan frontmatter for this phase are accounted for in [`REQUIREMENTS.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/REQUIREMENTS.md). No additional orphaned Phase 1 requirement IDs were found beyond `TRAN-01` and `TRAN-02`.

### Anti-Patterns Found

No blocker or warning anti-patterns were found in the scanned phase files. The placeholder/TODO scan only matched `mktemp` filename templates in the smoke scripts, which are not implementation gaps.

### Human Verification Approval

Human verification was completed on 2026-04-08 using a real mixed session and captured server output in `debug.log`.

- Mixed Atari and Linux ingress: approved. The log shows both slot `0` and slot `2` reaching normal gameplay input flow with repeated `transport accepted slot=` lines, including Atari/FujiNet traffic decoded as `format=extra-41` and Linux traffic decoded as `format=primary`.
- Transport vs sync fault isolation: approved. The same log includes periodic `transport summary slot=` lines with per-slot and aggregate counters, which were sufficient to distinguish live transport normalization behavior from later gameplay behavior during the playable mixed session.

### Gaps Summary

No code or wiring gaps were found against the phase `must_haves`. Automated verification passed for both smoke scripts and the relevant build targets, and the required mixed-session Atari/FujiNet human validation is now approved.

---

_Verified: 2026-04-08T16:40:00Z_  
_Verifier: Claude (gsd-verifier)_
