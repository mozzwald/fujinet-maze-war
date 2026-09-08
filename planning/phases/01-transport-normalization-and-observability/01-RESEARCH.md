# Phase 1: Transport Normalization and Observability - Research

**Researched:** 2026-04-07
**Domain:** FujiNet NetStream input normalization, packet ingress, and mixed-session transport observability
**Confidence:** HIGH

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|-----------------|
| TRAN-01 | Server accepts one canonical client input packet format and normalizes FujiNet NetStream framing before gameplay logic runs. | Server already has a byte reassembler plus Atari-specific DELTA fixes in [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L377); Phase 1 should pull that logic into a transport-normalization boundary that outputs one canonical packet struct before simulation state is touched. |
| TRAN-02 | Server exposes enough packet/debug counters or logs to distinguish transport-framing problems from gameplay reconciliation problems during mixed-session testing. | Current debug output is event-style `printf` only and does not keep counters or phase-separated metrics in [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L81); Phase 1 should add per-slot/global counters for raw bytes, normalized packets, drops, resyncs, and accepted gameplay packets, plus lightweight client-side debug surfacing. |
</phase_requirements>

## Summary

The transport split in the current codebase is real. Linux clients emit canonical 4-byte UDP `DELTA` datagrams directly with `sendto()` and never need framing repair ([`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c#L323), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c#L1222)). The Atari client does not send datagrams. It queues bytes through FujiNet NetStream one byte at a time with `NS_SEND`, and its receive side is explicitly a byte-stream parser built around `NS_AVAIL` and `NS_RECV` ([`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L942), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1076), [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1120)). That mismatch is why the server currently reparses client input as a stream and applies Atari-specific compatibility fixes inline before mutating player input state ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L377), [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L459)).

The planning implication is straightforward: Phase 1 should not change gameplay semantics. It should isolate transport cleanup into a server-side normalization boundary that converts any inbound source into one canonical packet representation, then hand only validated packets to gameplay handlers. The lowest-risk observability is also server-first: count raw bytes, packet candidates, extra-leading-`0x41` normalizations, swapped `pid/seq` decodes, resync attempts, stale sequence drops, invalid joy drops, and accepted DELTAs per slot. That gives mixed-session debugging a hard line between "transport/parser fault" and "simulation/reconciliation fault" before Phase 2 starts changing authoritative movement behavior.

**Primary recommendation:** Introduce a dedicated server transport-normalization layer that emits canonical packet records and transport counters, then keep Atari and Linux clients largely wire-compatible for this phase.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ISO C / POSIX sockets | Repo-local toolchain | Server transport ingress and Linux clients | Matches the existing server and Linux clients, keeps transport work simple, inspectable, and low-risk. |
| FujiNet NetStream handler (`NSENGINE.OBX`) | Repo-local binary | Atari byte-stream transport boundary | Already defines the Atari network path; the phase should adapt around it rather than replace it. |
| MADS assembler | Repo-local toolchain | Atari client implementation | Existing Atari client is already built and structured around MADS assembly. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `poll(2)` | libc/POSIX | Server and Linux input/socket polling | Keep for low-risk I/O scheduling during transport cleanup. |
| `ncurses` | System package | Linux text client | Use for fast mixed-session packet testing without changing gameplay code. |
| SDL 1.2 | System package | Linux graphical client | Use to cross-check that normalized transport does not regress snapshot/render intake. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Server-side normalization only | Rewrite Atari transport framing in Phase 1 | Higher risk and harder to validate because Atari already has substantial snapshot/reconciliation logic in flight. |
| Counter-based observability | Full replay/trace tooling | Better long term, but too much scope for Phase 1 and already deferred in requirements. |

**Installation:**
```bash
make all
```

**Version verification:** No package-managed application libraries were introduced by this research. The current build is defined by the repo `Makefile` and local toolchains in [`Makefile`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile#L1).

## Architecture Patterns

### Recommended Project Structure
```text
server/
├── main.c                  # Authoritative simulation plus socket loop
├── transport_normalize.*   # New: byte-stream/datagram normalization boundary
└── transport_stats.*       # New: per-slot/global counters and log formatting

clients/linux/
├── main.c                  # Canonical datagram sender + simple debug client
└── sdl_main.c              # Canonical datagram sender + graphical debug client

clients/atari/
└── maze-war.asm            # NetStream byte-stream sender/parser; only minimal debug hooks in Phase 1
```

### File-Level Ownership
| Area | Files | Phase 1 Ownership |
|------|-------|-------------------|
| Server ingress and observability | [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c) | Primary Phase 1 work. Move compatibility parsing and counters here first. |
| Linux canonical client behavior | [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c), [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c) | Secondary. Keep as canonical senders; only add optional debug identifiers/logging. |
| Atari NetStream path | [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) | Tertiary. Avoid gameplay changes; only add minimal debug counters or status exposure if needed. |
| Protocol documentation | [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md), [`README.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md) | Update after normalization contract is frozen. |

### Pattern 1: Normalize Before Gameplay Dispatch
**What:** Split raw inbound bytes from gameplay packet handling. The normalizer owns reassembly, compatibility decoding, and rejection reasons. Gameplay sees only canonical packets.
**When to use:** For every client-originated packet path, especially Atari NetStream input.
**Example:**
```c
// Source: server/main.c transport ingress around process_client_bytes()
struct normalized_packet pkt;
enum normalize_result nr = normalize_client_bytes(slot, raw, raw_len, &pkt, &stats);
if (nr == NORMALIZE_PACKET_READY) {
  handle_canonical_packet(slot, &pkt, players, brick_bits, ...);
}
```

### Pattern 2: One Canonical DELTA Format Internally
**What:** Internally represent all client input as one 4-byte semantic record: `{type=0x41, seq, pid=slot, joy}`.
**When to use:** After any byte-stream repair, swapped-field decoding, or source-address binding.
**Example:**
```c
// Source: server/main.c current DELTA compatibility logic
pkt.type = PKT_DELTA;
pkt.seq = parsed_seq;
pkt.pid = (uint8_t)slot;   // authoritative identity
pkt.joy = sanitized_joy;
```

### Pattern 3: Transport Metrics Separate from Simulation Metrics
**What:** Keep counters for parser/normalizer decisions separate from gameplay acceptance and tick/snapshot activity.
**When to use:** Always. Mixed-session debugging needs to answer "did transport fail?" before "did reconciliation fail?"
**Example:**
```c
// Source: recommended server-side addition
struct transport_stats {
  uint32_t raw_datagrams;
  uint32_t raw_bytes;
  uint32_t delta_primary;
  uint32_t delta_swapped;
  uint32_t delta_extra_type41;
  uint32_t delta_resync;
  uint32_t drop_bad_joy;
  uint32_t drop_stale_seq;
  uint32_t accepted_delta;
};
```

### Pattern 4: Client Canonicalization at the Input Edge Only
**What:** Linux and Atari clients should canonicalize joystick intent, not transport framing policy.
**When to use:** On the client send side. Atari already canonicalizes stick nibble to cardinal-or-neutral before transmit; Linux should remain the reference sender.
**Example:**
```asm
; Source: clients/atari/maze-war.asm
; NET_TX_BUILD_DELTA builds 0x41, seq, local_pid, joy after NET_SAN_STICKA.
```

### Anti-Patterns to Avoid
- **Transport repair inside gameplay handlers:** The current server repairs framing just before mutating `players[pid].joy`; Phase 1 should move that logic out of `handle_client_packet()`.
- **Changing reconciliation thresholds in this phase:** Atari snapshot apply already contains threshold-based reconcile triggers ([`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm#L1416)); leave that for Phase 2.
- **Creating Linux-only protocol shortcuts:** Linux clients already speak the documented canonical 4-byte `DELTA`; do not add debugging shortcuts that bypass the shared packet contract.
- **Adding heavy debug tooling first:** Phase 1 needs cheap counters and targeted logs, not replay infrastructure.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Client identity | Payload-trusted `pid` ownership | Source-address slot binding already in server | Current server correctly binds slot identity to address/port; keep that authoritative. |
| Transport diagnosis | Ad hoc `printf` archaeology after failures | Structured counters plus reason-coded logs | Counters make framing faults visible across long mixed sessions. |
| Atari transport rewrite | New protocol or handler replacement | Server-side normalizer around existing NetStream behavior | Replacing FujiNet behavior now would couple transport cleanup to emulator/firmware risk. |
| Gameplay-aware parser heuristics | Input repair that depends on maze/snapshot state | Pure framing/parser normalization | Parser decisions should depend only on bytes, slot, and canonical wire rules. |

**Key insight:** The deceptive complexity here is not packet format size; it is mixed datagram/byte-stream behavior. The right move is a narrow server-side adapter, not more compatibility branches sprinkled through simulation code.

## Common Pitfalls

### Pitfall 1: Leaving NetStream Repair in Gameplay Code
**What goes wrong:** Transport quirks continue to look like movement bugs because the same function both repairs bytes and mutates simulation state.
**Why it happens:** `handle_client_packet()` currently combines compatibility decoding, sequence filtering, and gameplay mutation.
**How to avoid:** Introduce a transport-normalization function that returns either a canonical packet or a counted/drop reason.
**Warning signs:** Logs still say `DELTA normalize` or `DELTA resync` from the same path that updates `players[pid].joy`.

### Pitfall 2: Treating Linux as the Problem Path
**What goes wrong:** Work gets spent changing the already-canonical sender instead of the actual byte-stream boundary.
**Why it happens:** Linux and Atari both notionally send `DELTA`, but only Atari uses `NS_SEND` byte-by-byte.
**How to avoid:** Treat Linux clients as the control group; use them to prove the server canonical path stays stable while Atari normalization is improved.
**Warning signs:** Phase 1 plans modify Linux send format or packet layout without a server-side need.

### Pitfall 3: Mixing Phase 1 Transport Work with Phase 2 Reconciliation
**What goes wrong:** The transport phase turns into a movement-smoothing rewrite and loses a clear success condition.
**Why it happens:** Atari already has staged snapshot handling and reconcile thresholds, which makes transport bugs easy to conflate with snap/correction behavior.
**How to avoid:** Keep Phase 1 success limited to canonical packet ingress and transport observability; do not retune `NET_RECON_P0`, `NET_DESYNC_CNT`, or prediction cadence yet.
**Warning signs:** Planned tasks talk about smoothness, replay buffers, or bounded correction rather than normalized input acceptance.

### Pitfall 4: Logs Without Stable Counters
**What goes wrong:** Testers cannot tell whether a missing move came from a malformed byte stream, stale seq rejection, or later simulation divergence.
**Why it happens:** Current debug mode prints individual events but does not retain aggregate counts.
**How to avoid:** Add per-slot and global counters plus a periodic summary line or on-demand dump.
**Warning signs:** Debugging still requires scrolling terminal history to infer how many packets were dropped.

### Pitfall 5: Failing to Document the Canonical Contract
**What goes wrong:** Future phases keep reintroducing compatibility logic because the transport boundary remains implicit.
**Why it happens:** `doc/protocol.md` documents compatibility formats but not a strict internal canonical form.
**How to avoid:** Update protocol docs after implementation to state that normalization happens before gameplay dispatch and define the counter names/drop reasons.
**Warning signs:** New code paths add fresh `pid/seq` swap checks outside the normalizer.

## Code Examples

Verified patterns from repository sources:

### Canonical Linux DELTA Sender
```c
// Source: clients/linux/main.c
uint8_t pkt[4];
pkt[0] = PKT_DELTA;
pkt[1] = seq++;
pkt[2] = (uint8_t)((local_pid >= 0) ? local_pid : 0);
pkt[3] = joy;
sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&srv, sizeof(srv));
```

### Atari Byte-Stream DELTA Builder
```asm
; Source: clients/atari/maze-war.asm
LDA #$41
STA NET_TX_BUF
LDA NET_SEQ
STA NET_TX_BUF+1
INC NET_SEQ
LDY NET_LOCAL_PID
STY NET_TX_BUF+2
```

### Current Server Compatibility Decode That Should Move Behind a Normalizer
```c
// Source: server/main.c
if (pkt[2] == pid && sanitize_client_joy(pkt[3], &joy)) {
  seq_in = pkt[1];
  parsed = 1;
} else if (pkt[1] == pid && sanitize_client_joy(pkt[3], &joy)) {
  seq_in = pkt[2];
  parsed = 1;
}
```

### Recommended Counter Dump Shape
```text
transport slot=1 raw_datagrams=124 raw_bytes=496
  delta_primary=118 delta_swapped=2 delta_extra_41=3 delta_resync=1
  drop_bad_joy=0 drop_stale_seq=4 accepted_delta=119
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Assume UDP datagram boundaries imply canonical client packets | Server reparses inbound bytes and accepts compatibility DELTA variants | Current repo state on 2026-04-07 | Transport normalization is already needed before higher-level sync work. |
| Immediate live mutation from packet parse | Atari client stages snapshots and commits through VBI-owned latches | Current repo state on 2026-04-07 | Phase 1 should avoid disturbing Atari’s existing snapshot/reconcile pipeline. |
| Manual terminal debugging | Recommended: persistent counters plus reason-coded logs | Phase 1 target | Makes transport failures separable from future reconciliation failures. |

**Deprecated/outdated:**
- Treating "The Atari and Linux clients speak the same protocol" as meaning they arrive through the same transport semantics. The payload contract is shared; the transport framing behavior is not ([`README.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md#L154)).

## Suggested Sequencing

1. Refactor server ingress only. Create a transport-normalization boundary in the server and route all current `process_client_bytes()` behavior through it without changing gameplay semantics.
2. Add server counters and summary logs. Prove mixed Linux-only and Atari-only sessions produce understandable transport metrics.
3. Update Linux clients only for optional debug visibility. Keep send format canonical and unchanged unless you need a client identifier in debug output.
4. Add minimal Atari observability only if server-side data is insufficient. Good candidates are one-byte counters or HUD/debug color hooks for "connected", "brick full received", and "snapshot sequence advancing".
5. Update protocol/readme docs to freeze the canonical ingress story and counter names.
6. Stop. Do not begin ack-based reconciliation, replay buffers, or snap-threshold work in this phase.

## Open Questions

1. **Where exactly does the extra leading `0x41` originate?**
   - What we know: The server explicitly detects `[0x41][0x41][seq][pid][joy]` and comments that some Atari NetStream paths prepend an extra `0x41` ([`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c#L483)).
   - What's unclear: Whether this is handler behavior, emulator behavior, or a startup timing artifact.
   - Recommendation: Treat the source as transport-opaque in Phase 1. Count it, normalize it, and defer root-cause elimination unless counters show it is frequent enough to justify Atari-side work.

2. **Should observability be periodic summary logs, on-demand dump, or both?**
   - What we know: Current server debug mode is event-oriented only.
   - What's unclear: What cadence is most usable during live mixed-session testing.
   - Recommendation: Start with low-noise periodic summaries gated by `--debug`, plus connect/disconnect summaries. Avoid interactive control surfaces in this phase.

3. **Does Atari need explicit on-screen transport diagnostics in Phase 1?**
   - What we know: Atari already has latent debug-oriented state like `NET_DBG_COLOR` and several network staging variables.
   - What's unclear: Whether server-side counters alone are enough for FujiNet emulator validation.
   - Recommendation: Plan Atari debug exposure as optional. Only add it if mixed-session testing shows server logs cannot localize failures.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | None yet; current validation is build + manual mixed-session runs |
| Config file | none — see Wave 0 |
| Quick run command | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` |
| Full suite command | `make all` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TRAN-01 | Server accepts Atari and Linux inputs through one canonical packet path before gameplay logic | smoke/manual integration | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ Wave 0 |
| TRAN-02 | Debug output distinguishes framing faults from later sync faults | manual integration | `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `make build/maze-war-server build/maze-war-client build/maze-war-client-sdl`
- **Per wave merge:** `make all`
- **Phase gate:** Manual mixed-session run with one Atari path plus one Linux client while checking transport counters/logs for accepted, normalized, and dropped packets

### Wave 0 Gaps
- [ ] `tests/transport_normalize_smoke.sh` — scripted server + Linux canonical client smoke run for DELTA acceptance
- [ ] `tests/transport_counters_smoke.sh` — verifies debug summary contains normalization/drop counters after synthetic input
- [ ] Harness for Atari/FujiNet emulator validation notes — documents how to capture server logs alongside Atari session behavior
- [ ] Framework install: none required if shell-script smoke tests are used first

## Sources

### Primary (HIGH confidence)
- [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c) - current ingress path, compatibility decoding, slot binding, and debug output
- [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm) - NetStream TX/RX path, byte-stream parser, staged snapshot handling, and available debug state
- [`clients/linux/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/main.c) - canonical ncurses client send/receive behavior
- [`clients/linux/sdl_main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/linux/sdl_main.c) - canonical SDL client send/receive behavior
- [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md) - documented packet contract and currently documented compatibility formats
- [`README.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/README.md) - build, runtime, and debugging workflow expectations
- [`Makefile`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/Makefile) - current build/test baseline

### Secondary (MEDIUM confidence)
- None. Research was based on current repository sources.

### Tertiary (LOW confidence)
- None.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - The phase should stay within the repo’s existing C/MADS/FujiNet stack.
- Architecture: HIGH - The current code clearly exposes the transport/playback split and where normalization currently leaks.
- Pitfalls: HIGH - The main pitfalls are directly visible in current code and roadmap constraints.

**Research date:** 2026-04-07
**Valid until:** 2026-05-07
