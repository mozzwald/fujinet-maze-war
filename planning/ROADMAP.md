# Roadmap: FujiNet Maze War

## Overview

This roadmap follows the dependency chain identified in research: normalize transport first, lock the reconciliation contract next, freeze combat semantics before smoothing presentation, then harden slot lifecycle and validate the full mixed-session target of 1 Atari client, 1 Linux client, and 2 AI zombies. Each phase delivers one coherent capability that can be planned and verified independently.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Transport Normalization and Observability** - Canonicalize packet ingress so transport bugs stop masquerading as gameplay bugs.
- [x] **Phase 2: Reconciliation Contract** - Add acknowledged-input reconciliation so Atari movement can stay smooth and bounded. Final real-Atari verification approved the cadence/fire-direction replay fix and closed RECN-01 through RECN-04.
- [x] **Phase 3: Combat and World Authority** - Freeze action ordering and authoritative world outcomes so firing behaves identically across clients. Human mixed-session checkpoint approved 2026-09-03.
- [x] **Phase 3.1: Netstream Handler Refresh and POKEY Channel Isolation (INSERTED)** - Update to the latest netstream handler (built from source) and remap all sound to POKEY channels 1+2 so the game can never corrupt the handler's channel 3+4 baud timer. See `ref/net-fix-plan.md` for full analysis. Completed 2026-09-02; all four criteria verified (see STATE.md).
- [x] **Phase 4: Render-State Separation** - Closed as good enough for now after user testing on 2026-09-11. The timing/recovery repair pass is greatly improved: two-computer emulation is almost flawless, real Atari XL + hardware FujiNet is acceptable with only occasional one-to-two-cell jumps, and the HUD marker side quest is user-verified. If the game moves to a cloud-hosted server and the added WAN latency makes remote movement feel worse, revisit bounded timed remote-sample playback from `04-03`. See [lag review and repair sequence](phases/04-render-state-separation/04-LAG-REVIEW.md).
- [x] **Phase 5: Slot Lifecycle and Zombie Handoff** - Make four-slot zombie backfill and human takeover stable through joins and disconnects. Reordered ahead of Phase 4: correctness work (ghost shots, stale facing, inherited state) that blocks reliable play. Code complete 2026-09-02; human confirmation of a live handoff received 2026-09-04.
- [x] **Phase 5.1: Link Integrity and Frame Resynchronisation (INSERTED)** - Make the Atari receive path robust to a lossy SIO byte stream: per-packet checksum, COBS framing with a zero delimiter so the parser always realigns, actor-state validation, and the boot/render faults these exposed. Unplanned; driven by real-hardware symptoms that emulation could not reproduce. Completed and human-confirmed 2026-09-04. See STATE.md "Phase 5.1".
- [x] **Phase 6: Mixed-Session Validation and Hardening** - Accepted after repeatable TCP validation and the user’s real Atari/FujiNet plus Linux/SDL mixed-session test.
- [x] **Phase 7: Realtime Transport Reliability (FujiRealm-informed)** - TCP migration, client conversion, CRC-16 framing, and the acknowledged reliable-event stream are merged into `a8-net-fix`. The remaining 07-05 client-authoritative movement proposal is documented and intentionally deferred because it is not recommended.
- [ ] **Phase 8: Lobby and Round Polish** - Add isolated multi-room TCP hosting, authoritative rounds, nonblocking Atari game-over presentation, clean leave/grace behavior, build-time endpoints, Lobby AppKeys/publication/browser integration, and repeatable room switching. Begins only after Phase 6 closes. See `phases/08-lobby-rounds-polish/08-RESEARCH.md` and plans 08-01 through 08-11.

## Phase Details

### Phase 1: Transport Normalization and Observability
**Goal**: Clients reach gameplay simulation through one canonical transport path, with enough observability to separate framing faults from sync faults.
**Depends on**: Nothing (first phase)
**Requirements**: TRAN-01, TRAN-02
**Success Criteria** (what must be TRUE):
  1. An Atari client and Linux client can both send gameplay input through the same canonical packet path without startup or join failures caused by NetStream framing differences.
  2. A tester can inspect counters or logs and tell whether a mixed-session failure came from malformed or normalized transport input versus gameplay reconciliation.
  3. Mixed-session debugging no longer requires guessing whether packet framing drift or gameplay state drift caused the visible issue.
**Plans**: 2 plans
Plans:
- [x] `01-01-PLAN.md` — Create Wave 0 transport smoke coverage and move DELTA framing normalization behind a canonical server boundary.
- [x] `01-02-PLAN.md` — Add transport counters, debug summaries, and transport contract documentation on top of the canonical ingress path.

### Phase 2: Reconciliation Contract
**Goal**: The Atari client predicts locally, replays only unacknowledged inputs, and stays closely aligned with authoritative movement and maze collisions.
**Depends on**: Phase 1
**Requirements**: RECN-01, RECN-02, RECN-03, RECN-04
**Success Criteria** (what must be TRUE):
  1. The local Atari wizard moves immediately on input and remains visually smooth during normal play while still converging back to server truth.
  2. When a correction is needed, the Atari wizard settles back to authoritative state without large teleports and with no more than about one maze cell of visible correction in normal play.
  3. Original Maze War movement and turning cadence remains recognizable on Atari while client prediction is active.
  4. Movement disagreements caused by stale local input are resolved by replaying only unacknowledged inputs instead of repeated snap-threshold retuning.
**Plans**: 6 plans
Plans:
- [x] `02-01-PLAN.md` — Extend SNAPSHOT with recipient-specific `ack_seq`, update Linux ack decoding, and add a real-server ack smoke harness.
- [x] `02-02-PLAN.md` — Add the Atari pending-input ring, ack discard helpers, and replay wiring at the staged authoritative commit seam.
- [x] `02-03-PLAN.md` — Replace threshold-only local correction with bounded ack-driven replay, then verify correction size and cadence on real Atari/FujiNet play. Real Atari verification failed due to player-movement screen artifacts.
- [x] `02-04-PLAN.md` — Fix Atari replay/correction stale-draw cleanup and repeat the blocked real-hardware movement approval for RECN-03 and RECN-04. Ghosting is fixed, but the retry still failed on cadence feel and a fire-plus-direction jump/snap-back while firing.
- [x] `02-05-PLAN.md` — Tighten Atari local input/replay cadence handling so trigger-bearing directional inputs do not replay as spurious movement, then repeat the real-hardware cadence/fire checkpoint. Approved on real Atari/FujiNet.

### Phase 3: Combat and World Authority
**Goal**: Turn, move, fire, hit, death, respawn, and shared world checks follow one explicit authoritative contract across server, Atari, and Linux clients.
**Depends on**: Phase 2
**Requirements**: COMB-01, COMB-02, COMB-03, COMB-04, WRLD-01, WRLD-02
**Success Criteria** (what must be TRUE):
  1. Bullets shown on the Atari client always originate from the wizard position and facing direction currently visible to the player.
  2. Repeating the same move-then-fire or turn-then-fire input sequence on Atari and Linux produces the same visible shot behavior and gameplay outcome.
  3. Hits, deaths, respawns, and score changes visible on clients match the server-authoritative outcome rather than diverging by client.
  4. Wall and brick interactions used for movement and line-of-fire checks stay consistent across Atari, Linux, and server simulation.
**Plans**: 3 plans
Plans:
- [x] `03-01-PLAN.md` — Freeze the authoritative same-tick combat/world contract on the server, document it in the protocol, and add server smoke coverage for ordering plus world outcomes.
- [x] `03-02-PLAN.md` — Align Atari shot/respawn/score consumption to the visible authoritative actor state so bullet origin and death/respawn presentation stay server-true.
- [x] `03-03-PLAN.md` — Align Linux combat/world packet interpretation, extend parity coverage, and finish with the mixed Atari/Linux approval checkpoint. Code complete, smokes green; the human mixed-session approval checkpoint is still pending and is deferred until Phase 3.1 lands, since the channel 3/4 baud-timer corruption fixed there may explain earlier hardware symptoms.

### Phase 3.1: Netstream Handler Refresh and POKEY Channel Isolation (INSERTED)
**Goal**: The game runs the latest netstream handler built from source, and no game code can disturb the handler's POKEY serial configuration (channels 3+4 joined baud timer, AUDCTL, SKCTL) after netstream init.
**Depends on**: Phase 3 code (inserted before the Phase 3 human checkpoint)
**Requirements**: Supports COMB-01..04 and VALD-03 indirectly (transport integrity under sound activity)
**Reference**: `ref/net-fix-plan.md` (full exploration findings, site-by-site remap list, memory-map analysis)
**Success Criteria** (what must be TRUE):
  1. `build/maze-war-net.xex` embeds a handler built from `../fujinet-atari-netstream` source (path overridable via `NETSTREAM_DIR`), including the `NS_InitNetstream` c_sp-leak fix and the RX IRQ overrun/error-latch fix; the checked-in `NSENGINE.OBX` fallback is refreshed to match.
  2. Netstream clock config remains TX external / RX internal (`NET_FLAGS = $04` — verified already correct).
  3. After `NS_INIT`, the game never writes AUDF3/AUDF4/AUDC3/AUDC4/AUDCTL/SKCTL. In particular `MOVSND` (the live bug: walk sound for slots 2/3 writes the handler's AUDF3/AUDF4 baud divisor every frame) routes through a channel 1+2 allocator: local player owns channel 1, remotes share channel 2 last-writer-wins with owner-checked silencing.
  4. During a soak with zombies walking in slots 2/3, POKEY shows AUDCTL=$28 and handler-programmed AUDF3/AUDF4 throughout, and the new sticky `NS_GetStatus` error byte stays clear.
**Plans**: 2 plans
Plans:
- [x] `03.1-01` — Build the handler from the netstream source repo in the game Makefile (with checked-in OBX fallback), refresh `NSENGINE.OBX`, and add minimal `NS_GetStatus` error-latch observability to `NET_POLL`. Implemented 2026-07-19. Verified 2026-09-02: the from-source rule reproduces the checked-in OBX byte for byte.
- [x] `03.1-02` — Remap all sound sites to a channel 1+2 priority allocator (`SND_SEL`/`SND_OFF`), guard the cold-start AUDCTL/SKCTL writes and restart-path AUDC3/AUDC4 clears, and add `sound_channel_guard_smoke.sh`. Implemented 2026-07-19. Verified 2026-09-02 in an emulator session covering movement, firing and a full role handoff: AUDCTL=$28, AUDF3/AUDF4 handler-programmed throughout, NET_NS_ERRS=$00.

### Phase 4: Render-State Separation
**Goal**: Authoritative state, predicted local state, and render-facing state are cleanly separated so smoothing improves presentation without mutating simulation truth.
**Depends on**: Phase 5 (reordered — presentation polish for release, after correctness work)
**Requirements**: RNDR-01, RNDR-02
**Success Criteria** (what must be TRUE):
  1. Remote wizards and AI zombies move smoothly on the Atari display without being treated as locally predicted actors.
  2. Local smoothing and remote interpolation never create fake gameplay positions that alter bullets, collision checks, or slot state.
  3. The Atari client can present smoother actor motion while keeping authoritative simulation state and predicted-local simulation state inspectably distinct.
  4. No display artefact originates from memory the client never initialised, and any character a player can enter renders as itself.
**Plans**: 5 plans
Plans:
- [x] `04-01` — Repair implemented and user-tested: NTSC sends now match the server's 10 Hz tick, animation has enough phase capacity, and server tick deadlines no longer drift from late loop iterations.
- [x] `04-02` — Introduce render-only actor position distinct from `LOCX/LOCY`; collision, shot origin and slot state stay on simulation truth while draw/erase uses `RNDX/RNDY`.
- [x] `04-03` — Accepted as good enough for the current local-server target: fallback direction and recovery-distance bugs are fixed and instrumented. Bounded timed remote-sample playback remains unimplemented by design and is reserved as the first revisit if future cloud-hosted server latency makes remote motion feel worse.
- [x] `04-04` — Clear all player-missile memory before PM DMA is enabled, and stop enabling missile DMA the game never uses. Hardware artefact check remains useful during the phase acceptance pass.
- [x] `04-05` — Complete the embedded font for characters text can use, including player-name and prompt/status text coverage.
- [x] `04-06` — RETRACTED 2026-09-09, not executed. Proposed a bounded catch-up for `REMOTE_FOLLOW` on the premise that a diverged remote actor gets permanently stuck; that premise was traced to two bugs in the measurement rig itself (documented in `04-RESEARCH.md` and `tests/rig/README.md`), and a corrected instrument shows reconciliation converges to zero gap in 100% of samples once a remote actor is still, at every loss rate tested including 50%. No catch-up mechanism is needed. Kept in the plan list for the record.
- [x] `04-07` — Mark each bottom HUD player row with a shirt-color PMG missile swatch while keeping gameplay DLI disabled.

**Status note (2026-09-09)**: The remote-actor lag investigation (requested
separately from the phase's original plans) is closed. See `04-RESEARCH.md`
for the full writeup. Headline: with a corrected measurement rig (two serious
instrument bugs found and fixed -- the emulator freezing under tight polling
loops, and the relay wedging the netstream handshake on restart), remote
reconciliation was shown to work correctly under real-hardware-like conditions
(120ms delay, 0-50% loss) -- no stuck-state bug exists. The residual lag while
a remote actor is actively moving is the designed cost of `REMOTE_FOLLOW`
walking one cell per tick without interpolation, which is exactly what `04-02`
and `04-03` below already exist to address. That work was not attempted
unsupervised: it is a substantially larger change to core rendering, and this
project has consistently gated changes of that size behind human hardware
verification.

**Status note (2026-09-04)**: Two prerequisites are already done and should not be
re-derived. Prediction now runs on the input actually transmitted
(`NET_TX_LAST_STICK`), which cut corrections from ~11 to ~3 per 40s of scripted
cornering; and local corrections walk the gap off one cell at a time
(`LOCAL_FOLLOW`) instead of teleporting. The second is a stopgap living inside
gameplay state — `04-02` should replace it. The residual ~3 corrections per 40s
are the clock phase slip that `04-01` targets. See STATE.md "Phase 4 starting
notes" and "Phase 4 investigation log".

### Phase 5: Slot Lifecycle and Zombie Handoff
**Goal**: The match always maintains four valid wizard slots, with clean zombie backfill and clean human takeover or disconnect recovery.
**Depends on**: Phase 3 (including the 3.1 insertion and the Phase 3 human checkpoint; reordered ahead of Phase 4 as correctness work)
**Requirements**: LIFE-01, LIFE-02, LIFE-03, LIFE-04
**Success Criteria** (what must be TRUE):
  1. A running match always shows exactly four active slots, with unused slots controlled by server AI zombies.
  2. When a human joins a zombie-filled slot, control transfers cleanly without ghost shots, stale movement, stale facing, or inherited transient state.
  3. When a human disconnects or times out, the slot returns to AI zombie control without breaking the match for remaining players.
  4. Clients can visibly track who owns each slot and remain in sync through zombie-to-human and human-to-zombie role changes.
**Plans**: 1 plan
Plans:
- [x] `05-01` — Reset per-slot gameplay state on both handoff directions server-side, shorten the client timeout so a reconnecting player is not stranded beside their own ghost, clear the matching per-slot latches on the Atari client when a role or local pid changes, document the slot contract, and cover it with `slot_lifecycle_smoke.sh`. Implemented 2026-09-02.

**Status note**: Human confirmation of a live join/leave handoff received 2026-09-04; human testing continues alongside each change. LIFE-02/03/04 are addressed. LIFE-01 is satisfied at `--zombies 3`; lower values intentionally leave seats open for more humans, and an empty seat still renders as a motionless wizard. That is documented in `doc/protocol.md` rather than changed, since it is a decision about what `--zombies` means.

### Phase 5.1: Link Integrity and Frame Resynchronisation (INSERTED)
**Goal**: The Atari client cannot be desynchronised by a damaged or misframed byte on the SIO link.
**Depends on**: Phase 5 (inserted after it; driven by real-hardware symptoms)
**Requirements**: none tracked yet — candidate IDs noted in STATE.md pending todos
**Success Criteria** (what must be TRUE):
  1. A byte lost, gained or flipped on the link costs at most one frame and the parser realigns on the next delimiter. — verified by `tests/cobs_resync_smoke.sh`
  2. A corrupt frame is discarded whole rather than partially applied; no packet can place an actor outside the playfield interior or index past a four-slot array. — verified by `tests/packet_checksum_smoke.sh` and source guards
  3. The client boots to a readable prompt and a correct playfield from cold power-up RAM. — verified on emulator
  4. Real hardware plays without the brick flicker, actor hopping, score flicker, name corruption or multi-second movement stalls that motivated the phase. — human-confirmed 2026-09-04
**Plans**: 0 (unplanned; executed directly from hardware symptoms, recorded retrospectively in STATE.md)

### Phase 6: Mixed-Session Validation and Hardening
**Goal**: The target live session is repeatably validated in the supported Atari/FujiNet workflow with deterministic checks for the known failure cases.
**Depends on**: Phase 5 (minimal validation pass); Phase 4 (full hardening for release)
**Requirements**: VALD-01, VALD-02, VALD-03
**Success Criteria** (what must be TRUE):
  1. A live session with 1 Atari client, 1 Linux client, and 2 AI zombies runs without movement-desync bugs that block normal play.
  2. Deterministic validation covers move-then-fire, turn-then-fire, zombie replacement, and human disconnect replacement cases and can be rerun after changes.
  3. The project can be validated through the current FujiNet-PC or FujiNet emulator workflow without needing Linux-only protocol shortcuts.
**Plans**: 1 plan
Plans:
- [x] `06-01` — Freeze and validate the merged TCP baseline with repeatable mixed-session, emulator/FujiNet-PC, delay/loss, and user hardware evidence.

### Phase 7: Realtime Transport Reliability (FujiRealm-informed)

The completed TCP/CRC/reliable-event work was developed on `realm-net` and is
now merged into `a8-net-fix`. Requirements remain tracked under v2 (`RTP-*`).

**Goal**: Adopt the parts of FujiRealm's networking that genuinely serve
maze-war's own stated goal (smooth, server-authoritative, reliable movement)
without adopting the parts that don't — see `07-RESEARCH.md` for the full
comparison and the reasoning behind what's in and out of scope.
**Depends on**: None structurally, but sequenced to land *before* resuming
Phase 4 (`04-02`/`04-03`), since render-state separation should be tuned on
top of a more reliable transport rather than through its noise.
**Requirements**: RTP-01, RTP-02, RTP-03, RTP-04, RTP-05
**Success Criteria** (what must be TRUE):
  1. The realtime channel runs over TCP through the same vendored netstream
     handler, with no handler rebuild required and no change to game tick
     rate or payload semantics.
  2. Every frame is CRC-16 protected, demonstrably catching corruption
     patterns the previous 1-byte sum missed.
  3. Brick, respawn, and name events are delivered through one acknowledged
     reliable stream and recover a single lost event within one retransmit
     interval, not the previous multi-second full-resync window.
  4. The client-authoritative movement question is documented with a clear
     recommendation and is not implemented without an explicit separate
     decision.
**Plans**: 5 plans (04 executable, 01 explicitly deferred)
Plans:
- [x] `07-01` — Server TCP listen/accept complete; full smoke suite passes. See `07-01-SUMMARY.md`.
- [x] `07-02` — TCP clients and emulator reconnect validation complete; real-hardware acceptance confirmed. See `07-02-SUMMARY.md`.
- [x] `07-03` — CRC-16/CCITT-FALSE framing complete in both directions; emulator and hardware confirmation recorded. See `07-03-SUMMARY.md`.
- [x] `07-04` — Replace `BRICK_DELTA`/`RESPAWN`/`NAME` echo hacks with one ordered, cumulatively-acknowledged reliable-event stream. See `07-04-SUMMARY.md`.
- [ ] `07-05` — NOT RECOMMENDED, documented only: full client-authoritative local movement. Do not execute without an explicit separate go-ahead.

### Phase 8: Lobby and Round Polish

**Goal**: Turn the stable four-player network game into a round-based, multi-room FujiNet Lobby title without weakening server authority or the verified Atari TCP path.
**Depends on**: Phase 6; Phase 7 plans 07-01 through 07-04
**Reference**: `ref/mazewar_lobby_rounds_implementation_plan.md`
**Research**: `phases/08-lobby-rounds-polish/08-RESEARCH.md`
**Model/effort and handoffs**: [08-MODELS.md](phases/08-lobby-rounds-polish/08-MODELS.md). Every step ends with the next recommendation and pauses for the user to switch; 08-05 is hardware-accepted, including safe-spawn and immediate-sprite redraw follow-ups.
**Round boundary contract**: [08-PROTOCOL.md](phases/08-lobby-rounds-polish/08-PROTOCOL.md)
**Success Criteria**:
  1. One server process runs isolated four-seat rooms on distinct TCP ports.
  2. The server authoritatively ends and resets rounds at a configurable kill limit, and every client agrees on the frozen result.
  3. Atari game-over presentation remains nonblocking with VBI, networking, and DLI-disabled rendering intact.
  4. Voluntary leave, unexpected loss, no-human grace, and Zombie replacement follow distinct tested contracts.
  5. Atari can obtain Lobby username/room AppKeys, browse validated QA rooms, tear down NetStream, and repeatedly join the same or another room.
  6. Lobby publication cannot stall simulation and remains opt-in until explicit production promotion.
  7. Obsolete unreachable title/game-over code is removed and its measured space is reused under enforced memory-layout limits.
**Plans**: 11 plans
Plans:
- [x] `08-01` — Remove unreachable title/game-over implementation and establish measured Atari memory headroom. Accepted on real Atari/FujiNet against emulation 2026-09-11.
- [x] `08-02` — Encapsulate behavior in one Room, then add isolated multi-room TCP listeners. Accepted with two Ataris in one room and split across separate rooms.
- [x] `08-03` — Add authoritative MATCH_END/ROUND_START protocol and complete round reset. Accepted in mixed real Atari/FujiNet and emulator testing, including reconnect, redraw, bullet-lifecycle, and idle-sound follow-ups, 2026-09-12.
- [x] `08-04` — Add the nonblocking Atari/SDL round-end presentation and results. Accepted with human and Zombie wins, five-second dance, and eight-second result display on 2026-09-12.
- [x] `08-05` — Add voluntary leave acknowledgement, orthogonal no-human grace, and shared session teardown/reset. Accepted on real Atari/FujiNet and emulator testing in two rooms.
- [x] `08-06` — Generate build configuration and replace the old title with title/direct-connect UI. Accepted on real Atari/FujiNet against emulation 2026-09-13; rare new-round shirt-only spawn remains explicitly deferred to Phase 4 trace work.
- [x] `08-07` — Add Lobby AppKeys, strict TCP URL validation, and startup routing. Accepted on physical Atari/FujiNet and emulation with temporary key `$2A`, both configured room ports, persisted username, OPTION bypass, and failed-connection fallback on 2026-09-13.
- [x] `08-08` — Publish rooms asynchronously to QA Lobby with opt-in lifecycle management. Accepted against the live QA Lobby, physical Atari/FujiNet, and emulation on 2026-09-13.
- [x] `08-09` — Add the bounded-memory Atari QA Lobby room browser. Accepted on physical Atari/FujiNet and emulation against QA Lobby on 2026-09-13.
- [ ] `08-10` — Integrate shared reset and validate repeated switching/external QA launch.
- [ ] `08-11` — Promote tested artifacts and registrations to production with explicit approval.

## Traceability

| Requirement | Phase |
|-------------|-------|
| TRAN-01 | Phase 1 |
| TRAN-02 | Phase 1 |
| RECN-01 | Phase 2 |
| RECN-02 | Phase 2 |
| RECN-03 | Phase 2 |
| RECN-04 | Phase 2 |
| COMB-01 | Phase 3 |
| COMB-02 | Phase 3 |
| COMB-03 | Phase 3 |
| COMB-04 | Phase 3 |
| WRLD-01 | Phase 3 |
| WRLD-02 | Phase 3 |
| RNDR-01 | Phase 4 |
| RNDR-02 | Phase 4 |
| LIFE-01 | Phase 5 |
| LIFE-02 | Phase 5 |
| LIFE-03 | Phase 5 |
| LIFE-04 | Phase 5 |
| VALD-01 | Phase 6 |
| VALD-02 | Phase 6 |
| VALD-03 | Phase 6 |
| MEM-01 | Phase 8 |
| ROOM-01 | Phase 8 |
| ROOM-02 | Phase 8 |
| ROND-01 | Phase 8 |
| ROND-02 | Phase 8 |
| ROND-03 | Phase 8 |
| GRCE-01 | Phase 8 |
| CONF-01 | Phase 8 |
| APKY-01 | Phase 8 |
| APKY-02 | Phase 8 |
| LOBY-01 | Phase 8 |
| LOBY-02 | Phase 8 |
| SWCH-01 | Phase 8 |
| PROD-01 | Phase 8 |

**Coverage:**
- v1 requirements: 21 total
- Mapped to phases: 21
- Unmapped: 0

## Progress

**Execution Order:**
1 -> 2 -> 3 -> 3.1 -> 5 -> 5.1 -> 7.1-7.4 -> 4 -> 6 -> 8

Phases 1, 2, 3, 3.1, 4, 5, 5.1, 6, and the executable Phase 7 scope are done. Phase 8 Lobby and round polish is active.

Phase 5 was brought forward ahead of the Phase 3 human checkpoint: both need the same
mixed session to verify, and running the checkpoint before the slot work would have
meant running it twice.

"Reliably playable" is reached after the minimal Phase 6 validation pass; full
Phase 6 hardening freezes the baseline; Phase 8 makes it Lobby-releasable.

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Transport Normalization and Observability | 2/2 | Complete | 2026-04-08 |
| 2. Reconciliation Contract | 5/5 | Complete | 2026-04-08 |
| 3. Combat and World Authority | 3/3 | Complete | 2026-09-03 |
| 3.1 Netstream Handler Refresh and POKEY Channel Isolation | 2/2 | Complete | 2026-09-02 |
| 5. Slot Lifecycle and Zombie Handoff | 1/1 | Code complete; human handoff confirmation wanted | 2026-09-02 |
| 4. Render-State Separation | 5/5 effective; 04-06 retracted | Closed as good enough for now after 2026-09-11 user testing; revisit bounded timed remote-sample playback if a future cloud-hosted server makes WAN latency visible | 2026-09-11 |
| 6. Mixed-Session Validation and Hardening | 1/1 | Complete; user mixed-session acceptance | 2026-09-11 |
| 7. Realtime Transport Reliability | 4/4 executable | Complete as scoped and merged; 07-05 remains a deferred, not-recommended design note | 2026-09-10 |
| 8. Lobby and Round Polish | 5/11 | 08-05 accepted on mixed real Atari/FujiNet and emulator testing in two rooms | 2026-09-12 |
