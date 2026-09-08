# Phase 2: Reconciliation Contract - Research

**Researched:** 2026-04-08
**Domain:** Recipient-specific input acknowledgement, Atari client prediction/replay, and mixed-session reconciliation validation
**Confidence:** HIGH

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| RECN-01 | Server snapshots tell each client which local input sequence has been authoritatively applied for that recipient. | Extend `SNAPSHOT` with one recipient-specific ack byte and track `last_applied_input_seq` separately from ingress freshness in [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L343) and [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L49). |
| RECN-02 | Atari client stores pending local inputs and replays only unacknowledged inputs after applying an authoritative correction. | Add a compact input ring beside existing net runtime state in [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L4606) and feed replay from the existing staged-authoritative commit seam in [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1603). |
| RECN-03 | Atari local wizard movement remains visually smooth under normal play and corrects by no more than one maze cell when reconciliation is required. | Preserve the current movement renderer and only change when `NET_P_PENDING` is set for the local slot in [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2704); move correction policy from threshold-only snap logic to ack-driven replay. |
| RECN-04 | Atari client preserves original Maze War movement and turning animation cadence while using client prediction. | Keep `MOVRATE`, `MOVCLOK`, `MOVEST`, `INITMOVE`, `MOVEIM`, and `SETSTIL` as the animation engine in [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L2787) and avoid replacing them with a new smoothing path. |
</phase_requirements>

## Summary

Phase 2 does not need a new gameplay model. The server already filters client DELTA sequence freshness per slot, but it stops at ingress and never reports back which sequence actually made it into an authoritative snapshot ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L343), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L1213)). The current snapshot remains 19 bytes and uses only `flags` for `valid`, recipient `pid`, and zombie mask ([`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md#L49)). That is the missing contract for real reconciliation.

The Atari client is closer to the target architecture than it looks. It already separates mainline packet staging from VBI-owned live state, stages authoritative per-slot positions and joy bytes, and marks per-slot pending corrections before movement code consumes them ([`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1322), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1603)). What it does not have is a pending-input history. Local correction is still threshold-based against authoritative tile coordinates, so it can only hold, follow, or snap; it cannot rebuild present-time local state from authoritative truth plus unacknowledged input.

The planning implication is to keep the phase narrow. Add one ack field to snapshots, add a server-side applied-seq tracker, add an Atari input ring plus ack-driven discard/replay, and use the Linux clients as protocol/debug twins that prove the ack contract before manual FujiNet validation. Do not change shot ordering, remote interpolation strategy, or render-state architecture here.

**Primary recommendation:** Make `SNAPSHOT` a 20-byte packet with `ack_seq` for the recipient, keep Atari movement rendering intact, and implement reconciliation as authoritative-local reset plus replay of only unacknowledged DELTAs.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ISO C / POSIX sockets | Repo-local toolchain | Authoritative server and wire protocol changes | The server already owns tick timing, input freshness, and snapshot construction in plain C. |
| MADS assembler | Repo-local toolchain | Atari prediction/reconciliation implementation | The Atari client already contains the required staging and animation seams; Phase 2 should extend them, not migrate away. |
| Linux C protocol clients (`ncurses` and SDL 1.2) | Repo-local toolchain + system libs | Canonical DELTA sender and ack/debug twin | They already send the canonical 4-byte DELTA and parse snapshots without FujiNet framing ambiguity. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Existing shell smoke scripts | Repo-local | Fast server/protocol regression checks | Keep build and transport smoke green while adding ack-aware tests. |
| Gabriel Gambetta reconciliation pattern | 2026 article revision | Reference model for seq+ack+replay flow | Use only to validate the contract shape, not to replace repo-specific movement code. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Extend `SNAPSHOT` to 20 bytes | Overload `flags` or add a separate ack packet | `flags` has only one spare bit; a separate ack packet adds ordering/loss problems this phase does not need. |
| Server-reported ack only | Threshold retuning on Atari | Retuning does not satisfy RECN-01 or RECN-02 and keeps correction heuristic-only. |
| Linux as debug twin | Atari-only validation | Slower feedback and harder to isolate protocol bugs from FujiNet transport/runtime issues. |

**Installation:**
```bash
make all
```

**Version verification:** No new package-managed libraries are required for this phase. The active build contract is the repository `Makefile` in [`Makefile`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile#L1).

## Architecture Patterns

### Recommended Project Structure
```text
server/
├── main.c                  # Add applied-seq tracking and per-recipient snapshot ack
├── transport_normalize.*   # Keep ingress normalization unchanged
└── transport_stats.*       # Keep transport counters unchanged

clients/atari/
└── maze-war.asm            # Add pending-input ring, ack discard, replay, and debug counters

clients/linux/
├── main.c                  # Add ack decode/logging and deterministic validation hooks
└── sdl_main.c              # Add ack decode plus optional visual/debug overlay only

doc/
└── protocol.md             # Freeze 20-byte snapshot contract and ack semantics

tests/
├── transport_normalize_smoke.sh
├── transport_counters_smoke.sh
└── reconciliation_*.sh     # New Phase 2 smoke coverage
```

### File Ownership
| Area | Files | Phase 2 Ownership |
|------|-------|-------------------|
| Wire contract and authoritative ack | [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c), [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md) | Primary. Define and emit recipient-specific `ack_seq`. |
| Local prediction and replay | [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) | Primary. Add pending-input storage and replay without replacing movement cadence code. |
| Ack-validation twins | [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c) | Secondary. Parse extended snapshots, surface ack state, and support deterministic validation. |
| Regression coverage | [`tests/transport_normalize_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_normalize_smoke.sh), [`tests/transport_counters_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/transport_counters_smoke.sh) | Secondary. Keep existing transport checks green and add Phase 2 smoke scripts next to them. |

### Pattern 1: Recipient-Specific Ack in SNAPSHOT
**What:** Keep one authoritative world snapshot packet, but append one byte that means “last input sequence processed for you.”
**When to use:** Every snapshot to a connected human client.
**Example:**
```c
// Source: recommended extension to server/main.c build_snapshot/send loop
// byte 19 is recipient-specific and written per send, not once globally.
pkt[0] = PKT_SNAPSHOT;
pkt[1] = snapshot_seq;
pkt[2] = (uint8_t)(0x01u | (recipient_pid << 1) |
                   ((zombie_mask & 0x0Fu) << 3) |
                   (have_ack ? 0x80u : 0x00u));
pkt[19] = applied_input_seq[recipient_pid];
```

### Pattern 2: Track Applied Seq Separately From Freshness
**What:** Do not reuse ingress freshness as the published reconciliation truth. Keep one field for “latest fresh DELTA received” and one for “latest DELTA whose input state was used for an authoritative tick.”
**When to use:** In the server tick path right before snapshot emission.
**Example:**
```c
// Source: server/main.c current freshness gate at #L343
if (delta_seq_is_fresh(&clients[slot], delta.seq)) {
  players[slot].joy = delta.joy;
  clients[slot].latest_input_seq = delta.seq;
}

// During tick:
applied_input_seq[slot] = clients[slot].latest_input_seq;
```

### Pattern 3: Atari Replay Hooks at the Existing Stage Commit Boundary
**What:** Treat staged snapshot commit as the point where local authoritative state is refreshed, then rebuild predicted-local state from pending inputs newer than `ack_seq`.
**When to use:** Only for `NET_LOCAL_PID` after a newer snapshot is committed.
**Example:**
```asm
; Source seam: clients/atari/maze-war.asm#L1603
JSR NET_STAGE_COMMIT          ; authoritative target arrays become live
JSR NET_LOCAL_ACK_DISCARD     ; drop pending inputs <= ack_seq
JSR NET_LOCAL_REPLAY_PENDING  ; replay only unacknowledged local inputs
```

### Pattern 4: Preserve Original Movement Cadence
**What:** Replay should drive the same movement primitives already used by local prediction, not a new instantaneous movement layer.
**When to use:** Always for RECN-04.
**Example:**
```asm
; Source seams: INITMOVE/MOVEIM/SETSTIL around clients/atari/maze-war.asm#L3088
; Replay may update DIR / pending target selection, but it should not bypass
; MOVRATE, MOVCLOK, MOVEST, or SETSTIL.
```

### Anti-Patterns to Avoid
- **Acking `last_delta_seq` blindly:** That is ingress truth, not authoritative tick truth.
- **Hard-snap tuning as the main fix:** `NET_RECON_P0` and `NET_HARD_P0` are guard rails, not the reconciliation contract.
- **Replaying combat as part of Phase 2:** Shot ordering belongs to Phase 3 even if trigger bits remain in queued input bytes.
- **Changing remote smoothing behavior here:** Remote `REMOTE_FOLLOW` logic can remain as-is; this phase is about the local slot contract.
- **Adding Linux-only protocol shortcuts:** The Linux clients should validate the same snapshot extension the Atari client consumes.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Ack transport | Separate ack packet stream | Extend `SNAPSHOT` with one byte | Reuses existing authoritative cadence and avoids extra packet ordering/loss state. |
| Local correction | Threshold-only snap heuristics | Ack-driven discard plus replay | Heuristics cannot identify which local inputs are still outstanding. |
| Movement presentation | New smoothing engine in Phase 2 | Existing `INITMOVE`/`MOVEIM` cadence | RECN-04 requires original Maze War feel. |
| Protocol validation | FujiNet-only manual testing | Linux canonical sender as control path | Faster iteration and cleaner protocol fault isolation. |
| Server input truth | Trust payload `pid` or client-declared ack | Server-owned per-slot applied-seq state | Slot identity is already authoritative on the server. |

**Key insight:** The hard part is not storing one more byte in a packet; it is ensuring the ack byte represents authoritative simulation progress and that Atari replay reuses the existing movement cadence instead of bypassing it.

## Common Pitfalls

### Pitfall 1: Acking Received Input Instead of Applied Input
**What goes wrong:** The client discards inputs that the server has not actually simulated yet.
**Why it happens:** The current server only stores ingress freshness in `last_delta_seq`.
**How to avoid:** Add a dedicated `applied_input_seq` path that is published only from the tick/snapshot path.
**Warning signs:** A snapshot carries an ack for an input received after the movement step that produced the snapshot positions.

### Pitfall 2: Breaking Snapshot Compatibility by Assuming Exact 19 Bytes Everywhere
**What goes wrong:** Clients silently ignore or misread the new ack byte.
**Why it happens:** The protocol doc says 19 bytes today, and packet handlers key off `n >= 19`.
**How to avoid:** Update the protocol doc first, parse `ack_seq` only when `n >= 20`, and use `flags` bit7 as `ack_valid`.
**Warning signs:** One client reads `buf[19]` unconditionally while another still treats `SNAPSHOT` as exactly 19 bytes.

### Pitfall 3: Replaying Input by Teleporting the Local Actor
**What goes wrong:** Corrections satisfy alignment but violate RECN-04 because the local wizard no longer moves like Maze War.
**Why it happens:** Replay is easier to implement as direct `LOCX/LOCY` mutation.
**How to avoid:** Reset authoritative-local base state, then reapply pending inputs through the normal movement path or a compact equivalent that preserves cadence fields.
**Warning signs:** Replay writes `LOCX/LOCY` repeatedly without touching `DIR`, `MOVEST`, or `MOVCLOK`.

### Pitfall 4: Letting Phase 2 Absorb Combat Ordering
**What goes wrong:** Replay of trigger-bearing inputs starts deciding local shot presentation before the authoritative combat contract exists.
**Why it happens:** The queued input byte already includes trigger.
**How to avoid:** Store raw joy bytes in the ring, but keep projectile origin/order authoritative and explicitly defer same-tick fire semantics to Phase 3.
**Warning signs:** Phase 2 plans edit `SHOT`, `RESPAWN`, or `start_shot()` semantics.

### Pitfall 5: Losing the Linux Control Path
**What goes wrong:** Protocol bugs are diagnosed through FujiNet timing only, which slows feedback and muddies root cause.
**Why it happens:** Phase 2 attention shifts entirely to the Atari assembly work.
**How to avoid:** Add Linux-side ack logging and at least one deterministic reconciliation smoke harness before manual Atari testing.
**Warning signs:** The only Phase 2 verification step is “try it on Atari.”

## Code Examples

Verified patterns from repository sources:

### Current Server Snapshot Shape
```c
// Source: /mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L207
static void build_snapshot(uint8_t seq, const struct player_state *players,
                           uint8_t *out, size_t out_len) {
  if (out_len < 19) {
    return;
  }
  out[0] = PKT_SNAPSHOT;
  out[1] = seq;
  out[2] = 0x01;
  // positions, joy bytes, scores...
}
```

### Current Atari Stage Commit Seam
```asm
; Source: /mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1603
NET_STAGE_COMMIT
	LDA	NET_STAGE_SEQ
	STA	NET_RX_TMP
	AND	#$01
	BNE	NSC_X
	LDA	NET_RX_TMP
	CMP	NET_STAGE_APPLYSEQ
	BEQ	NSC_X
```

### Current Linux Canonical DELTA Sender
```c
// Source: /mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c#L326
uint8_t joy = pack_joy(stick, (uint8_t)fire);
pkt[0] = PKT_DELTA;
pkt[1] = seq++;
pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
pkt[3] = joy;
sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&srv, sizeof(srv));
```

### Standard Reconciliation Contract
```text
// Source: https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html
1. Client sends input with seq N and stores it locally.
2. Server snapshot includes last processed seq for that recipient.
3. Client discards pending inputs <= ack_seq.
4. Client resets local authoritative base and reapplies remaining inputs.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Threshold-only local correction | Ack-driven authoritative reset plus replay | Phase 2 target | Eliminates guesswork about which local inputs still matter. |
| One global snapshot layout for all recipients | Same snapshot plus recipient-specific ack byte | Phase 2 target | Preserves packet simplicity while making reconciliation explicit. |
| Local snap/follow without input history | Pending-input ring with discard/replay | Phase 2 target | Enables bounded correction without rewriting movement cadence. |
| Linux as generic client only | Linux as protocol/debug twin for ack validation | Phase 2 target | Faster, deterministic validation before FujiNet manual runs. |

**Deprecated/outdated:**
- Using `NET_RECON_P0` as the primary reconciliation mechanism instead of a guard rail.
- Treating “latest DELTA accepted” as equivalent to “latest input authoritatively applied.”

## Open Questions

1. **Should `ack_seq` be valid before the first accepted DELTA?**
   - What we know: Current snapshots have one spare flag bit and no ack byte.
   - What's unclear: Whether zero is a safe sentinel given the client starts at seq `0`.
   - Recommendation: Use `flags` bit7 as `ack_valid`; parse `ack_seq` only when the bit is set.

2. **Where should the server stamp `applied_input_seq`?**
   - What we know: Input arrives asynchronously, but snapshots are built once per tick after `step_players()`.
   - What's unclear: Whether future per-input simulation changes in Phase 3 will need a stricter “post-move, pre-snapshot” stamp point.
   - Recommendation: Stamp from the tick path now and keep the field separate from ingress freshness so Phase 3 can refine semantics without changing the packet again.

3. **How much of the queued input byte should Atari replay in Phase 2?**
   - What we know: DELTA already includes trigger in bit4.
   - What's unclear: Whether replaying trigger should affect any local-side combat presentation before Phase 3 freezes action ordering.
   - Recommendation: Store raw joy bytes, but constrain Phase 2 replay effects to local movement/turning state only.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | shell-script smoke checks plus repo build targets |
| Config file | none |
| Quick run command | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` |
| Full suite command | `make all && bash tests/transport_normalize_smoke.sh && bash tests/transport_counters_smoke.sh` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RECN-01 | Snapshot includes recipient-specific authoritative `ack_seq` and Linux client can decode it | smoke | `bash tests/reconciliation_snapshot_ack_smoke.sh` | ❌ Wave 0 |
| RECN-02 | Atari/Linux reconciliation discards acked inputs and replays only newer ones | smoke/integration | `bash tests/reconciliation_replay_smoke.sh` | ❌ Wave 0 |
| RECN-03 | Correction magnitude stays bounded under stale/misaligned local input | smoke/manual integration | `bash tests/reconciliation_correction_bound_smoke.sh` | ❌ Wave 0 |
| RECN-04 | Atari movement cadence stays on the original move/turn pipeline while prediction is active | build/manual Atari validation | `make all` | ✅ / manual behavior check |

### Sampling Rate
- **Per task commit:** `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **Per wave merge:** `make all && bash tests/reconciliation_snapshot_ack_smoke.sh`
- **Phase gate:** Full suite green plus one mixed Atari/Linux validation pass before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/reconciliation_snapshot_ack_smoke.sh` — launches the real server, injects canonical DELTAs, and asserts per-recipient `ack_seq`
- [ ] `tests/reconciliation_replay_smoke.sh` — drives deterministic local input sequences through Linux client or scripted UDP sender and checks ack discard behavior
- [ ] `tests/reconciliation_correction_bound_smoke.sh` — exercises stale input/correction paths and enforces bounded divergence markers
- [ ] Linux debug output for ack state in [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c) and optionally [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c)

## Sources

### Primary (HIGH confidence)
- [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md) - Current 19-byte snapshot layout, DELTA semantics, and transport counters
- [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c) - Current DELTA freshness tracking, authoritative tick loop, and per-recipient snapshot send path
- [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) - Existing staging, local prediction, reconcile thresholds, and movement cadence seams
- [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c) - Canonical DELTA sender and minimal snapshot consumer
- [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c) - Graphical protocol twin and current snapshot-driven player animation
- [`Makefile`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile) - Active build and validation entry points
- [`.planning/REQUIREMENTS.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/REQUIREMENTS.md) - Phase requirement definitions
- [`.planning/research/SUMMARY.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/.planning/research/SUMMARY.md) - Prior project-level recommendation that reconciliation should be ack-based

### Secondary (MEDIUM confidence)
- https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html - Reference pattern for seq-tagged input, server ack, discard, and replay

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Phase 2 stays within the existing repo stack and toolchain.
- Architecture: HIGH - The missing contract is directly visible in current server/client code, and the Atari client already exposes the right refactor seams.
- Pitfalls: HIGH - The main risks are concrete boundary mistakes in packet semantics, replay scope, and cadence preservation.

**Research date:** 2026-04-08
**Valid until:** 2026-05-08
