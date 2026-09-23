# Phase 7: Realtime Transport Reliability (FujiRealm-informed) — Research

**Researched:** 2026-09-09, on branch `realm-net`
**Domain:** What FujiRealm (`~/fujicode/fujirealm-game-demo`) actually does differently, and which of it genuinely serves maze-war's own stated goal of smooth, server-authoritative movement
**Confidence:** HIGH on the mechanics (read from FujiRealm's source directly, cross-checked against the shared netstream handler's own source and docs). MEDIUM on which parts explain the user's felt "less lag" — that's inference, flagged as such below, and should be re-judged once the recommended changes are on hardware.

## Why this exists

Asked to plan a migration to "fujirealm style client/server networking" because FujiRealm feels more reliable and less laggy in play. This branch (`realm-net`, off `a8-net-fix`) exists to carry that work without disturbing the `a8-net-fix` line, which is close to landing on `master`.

**Read this whole document before executing any plan in this phase.** The headline finding changes the shape of the recommended work: FujiRealm's networking is not one thing, it's three separable things with very different risk/reward, and one of them would not actually address the specific complaint (remote-player lag) this project spent most of 2026-09-08/09 measuring.

## What FujiRealm actually does (verified in its own source)

### 1. Same handler family, transport is a flag bit

FujiRealm's Atari client vendors the **same** netstream handler source maze-war already builds from (`~/fujicode/fujinet-atari-netstream`, referenced by both projects' Makefiles). `NS_InitNetstream`'s flags byte is documented in that shared repo (`docs/netstream_api.md`):

- `0x01` transport: 0=UDP, 1=TCP
- `0x02` REGISTER on/off
- `0x04` TX clock: 0=internal, 1=external
- `0x08` RX clock: 0=internal, 1=external
- `0x20` UDP sequencing (UDP only)

maze-war's `NET_FLAGS = $04` (UDP, external TX/internal RX clock). FujiRealm's `NETSTREAM_FLAGS = $07` (TCP + REGISTER + same clock config). **This is a runtime argument to `NS_InitNetstream`, not something baked into the handler binary** (`clients/atari/maze-war.asm:1004` calls `NS_INIT` with `NET_FLAGS` as a plain equate) — switching transport needs no handler rebuild, just a different byte.

`NS_SendByte`/`NS_RecvByte` (jump-table offsets `+12`/`+15`) are **single-byte calls in both projects** — maze-war already uses them this way (`clients/atari/maze-war.asm:1629,1642`). The Atari↔FujiNet-device hop is a raw SIO byte stream regardless of what the FujiNet device does with those bytes on its network side. This is exactly why maze-war's Phase 5.1 built COBS+checksum framing in the first place ("Its receive path is a byte stream over SIO rather than discrete datagrams" — `doc/protocol.md`), and **that framing is already transport-agnostic**: it doesn't know or care whether the FujiNet device's own outbound socket is UDP or TCP. Switching the flag bit changes nothing about how the COBS decoder runs.

**Practical consequence: the transport switch itself is low-risk.** No handler rebuild, no framing rewrite, a one-line client equate change, and a server-side socket-model change that's a natural extension of the `poll()` loop the server already runs (`server/main.c:1808-1820`) — accept() a few more file descriptors instead of one UDP socket, keep everything above the I/O layer untouched.

### 2. Stronger framing: CRC-16 instead of a sum

FujiRealm's realtime v3 frame (`docs/PROTOCOL.md`, confirmed in `server/protocol.py`) is COBS-encoded, `$00`-delimited, same shape as maze-war's, but the trailing check is a real CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) instead of maze-war's one-byte additive sum. Same "one frame's cost, parser realigns on the next delimiter" property either way; CRC-16 just catches more corruption patterns than an 8-bit sum can. Trailing zero bytes of the payload are stripped on encode and re-padded on decode, so a mostly-empty packet costs less on the wire — a nice-to-have maze-war's small fixed packets don't need urgently, but cheap to adopt alongside the CRC change since both touch the same encode/decode code.

**Practical consequence: also low-risk, self-contained, and directly serves "more reliable" on the SIO hop specifically** — the hop every hardware-only bug this project has hit (Phase 5.1's bricks-flickering, actors-hopping, corrupt-snapshot-fields saga) actually lives on.

### 3. A real ARQ for events, not ad hoc echo bursts

This is FujiRealm's most valuable idea and the one most worth taking wholesale. Its `TERRAIN_EDGE` cache-step delivery (`server/hybrid_server.py:1340-1441`) is a small, clean go-back-N:

- Up to 3 steps in flight at once, each tagged with a monotonic 16-bit revision, retransmitted **byte-identical** when it retransmits at all.
- ACKs are **cumulative**: an ack matching a queued revision and its expected origin confirms that entry and everything older.
- Normal retransmit: any unacked step older than 0.5s → resend the whole pipeline oldest-first, bump a retry counter.
- **Duplicate-ACK fast retransmit**: if the client re-acks its already-confirmed state (meaning the real next step got lost), the server notices immediately and retransmits **right away**, rate-limited to one burst per 100ms — and this does **not** consume a retry, because ordinary loss isn't evidence of a dead link.
- After 4 timeout-driven retries with zero progress, give up and fall back to a full resync (`WINDOW_ROW`) — escalation triggered by *evidence the link is stuck*, not a blind timer.
- A successful ACK refills the pipeline **immediately**, inline, not on the next tick — throughput is RTT-bound when the link is healthy, not tick-bound.

Compare to what maze-war currently has: **four different one-off redundancy hacks**, each invented separately this project's own history to patch a specific symptom — `SHOT` clears burst 3 times, `BRICK_DELTA` echoes `BRICK_ECHO_REPEATS` (2) times, `RESPAWN` now echoes similarly (this session, `7c5ae23`), `NAME` re-broadcasts on a 1s rotation, and a full `BRICK_FULL` resync every 3s as a blunt backstop for anything the others miss. None of these have real acknowledgment; they're all "send it a few extra times and hope," with the 3s full resync as the only actual guarantee, and that guarantee has multi-second latency.

**Practical consequence: this is the FujiRealm mechanism to actually port.** One well-tested reliable-delivery primitive, applied to every maze-war event type that currently has its own bespoke echo hack, would be strictly better than what exists today, is transport-independent (works identically whether the network hop is UDP or TCP), and is the single change most likely to reduce the class of bug Phase 5.1 spent a whole session chasing.

## The finding that changes the plan: FujiRealm's authority model, and what it does and doesn't buy

FujiRealm's server (`server/game.py:1367-1488`, `apply_player_state`) does **not** independently simulate movement from an input byte the way maze-war's server does. **The client computes and reports its own new x/y as an already-decided fact.** The server's entire job is `_player_destination_allowed`: is the reported destination exactly one legal, walkable, unoccupied step from where the server last believed the player stood? If yes, adopt the client's coordinates verbatim. If no, keep the server's own old position and set a one-bit `correction_flags`. There is no second, independent server-side simulation to diverge from — which is why FujiRealm's client code has no `LOCX`/`NET_PX_X`-style predicted/authoritative split, no replay-pending-inputs queue, and none of the `NET_FRAME_DIV`/`MOVCLOK` phase-lock problem that motivated maze-war's own `04-01` plan. **Local movement has zero round-trip latency because the client's own screen is the source of truth**, full stop, with the server as a retroactive referee.

This is genuinely a different point on the authority spectrum than maze-war's design, and maze-war's own stated constraint is explicit about which side it has chosen: *"Authority: Server remains authoritative for wizard position and state — client-side smoothing must reconcile to server truth"* (`PROJECT.md`). Adopting FujiRealm's model for local movement would mean walking that back.

**And it would not address the complaint that prompted this whole investigation.** `atari8-client/fujirealm.asm:6977-7052` (`netstream_apply_remote_players`) copies each incoming remote-player record straight into `remote_x`/`remote_y` with a plain store — **no interpolation, no glide, nothing but a walk-cycle animation frame toggle for cosmetic effect.** FujiRealm's remote actors are *snapped* every packet. maze-war's remote actors already do better than that today: `REMOTE_FOLLOW` walks one cell per move-tick toward the target rather than teleporting, which is why this project's own 2026-09-09 measurement session found remote reconciliation converging cleanly rather than snapping under most conditions. If maze-war copied FujiRealm's remote-player handling verbatim, it would very likely feel **choppier**, not smoother, for exactly the actors the user singled out.

So where does FujiRealm's felt "less lag" actually come from? Three candidates, in order of how confident this research is:

1. **Zero-round-trip local movement** (verified mechanism, real effect) — but the user's own report this session was that maze-war's *local* movement "seems fine," and the complaint was specifically about *remote* players. This candidate explains a real difference between the two games but probably isn't what the user is feeling.
2. **Fewer real-hardware SIO-hop corruption events**, from CRC-16's stronger detection plus TCP removing an entire loss surface (the server↔FujiNet-device network hop) so the *only* remaining loss surface is the SIO hop — meaning fewer of the "bricks flicker, actor hops, snapshot field corrupts" class of defect that Phase 5.1 fought and that no amount of steady-state reconciliation tuning can fix, because that class of bug is about corrupted bytes, not slow convergence. **This is the best-supported candidate** and is exactly what items 2 and 3 above (CRC-16, real ARQ) target directly.
3. **Fewer stuck/lost events specifically** (a brick that stays wrong, a death that doesn't animate, a name that flickers) from the ARQ replacing best-effort echo bursts — same reasoning as #2, and again squarely addressed by item 3 above.

None of these three requires adopting FujiRealm's client-authoritative movement model. All three are served by the transport/framing/ARQ work, which is also the lower-risk, more self-contained work, and which doesn't require walking back a design constraint this whole project has been built around.

## Recommendation

**Adopt:** TCP transport (07-01/07-02), CRC-16 framing (07-03), and a unified ARQ replacing the ad hoc echo mechanisms (07-04). Then let the already-planned Phase 4 (`04-02`/`04-03`, render-state separation with real remote interpolation) proceed on top of a more reliable transport — it should measure more cleanly once transport-loss noise is reduced, and it will give maze-war *better* remote-actor smoothness than FujiRealm's own snap-only approach, not just parity with it.

**Do not adopt by default:** full client-authoritative local movement. It's real, it's documented as `07-05-PLAN.md` for the record and in case the user wants it after reading this, but it departs from the project's own stated authority constraint, weakens movement-specific cheat resistance (combat/scoring stay server-side either way in both designs, so this is narrower than it sounds, but it's still a real trade), and does not address the specific complaint that motivated this investigation. `07-05` is written up but marked **do not execute without explicit go-ahead**.

## Sequencing and risk

- `07-01` and `07-02` (server and client transport swap) should land together and be tested before anything else changes, so a transport-only regression is never confused with a framing or ARQ change. This is the highest-uncertainty step — everything else in this phase is additive on top of it — and the one most in need of real-hardware testing, not just emulator testing, given this whole project's own lesson that the emulator's netsio path does not reproduce real SIO-hop behavior.
- `07-03` (CRC-16) is independent of `07-01`/`07-02` and could land first or in parallel on the existing UDP transport if that's useful for de-risking, but doing it after the transport swap avoids touching the same wire-format code twice in flight.
- `07-04` (ARQ) depends on `07-01`-`07-03` being settled, since it's easiest to design one primitive against the final frame format rather than adapt it twice.
- Existing Phase 4 (`04-02`/`04-03`) is unaffected in design by this phase and should resume after `07-04`, per the reasoning above.
- This branch (`realm-net`) diverges from `a8-net-fix`. `a8-net-fix` is close to landing on `master` on its own track; merging `realm-net` back will need to reconcile with whatever lands there in the meantime. Treat that as ordinary branch hygiene, not a blocker to planning or early work here.

## Open questions to resolve during implementation, not during planning

- **`REGISTER` flag semantics**: the shared handler's own docs describe it only via one line in the smoke-test tool's README ("waits for a REGISTER packet, then prints Client Connected"). Unclear whether it's required for TCP mode to complete its handshake with real FujiNet-PC/hardware, or an independent, optional feature FujiRealm happens to also use. Test with it clear first (simpler); enable only if TCP mode doesn't work without it.
- **Baud rate**: FujiRealm uses 31250 against maze-war's current 57600. Not a requirement to copy — treat as a tunable to characterize empirically once TCP is working, not a default to inherit blindly.
- **Linux client parity**: both `clients/linux/main.c` and `clients/linux/sdl_main.c` are plain UDP datagram sockets today (`SOCK_DGRAM`, `recvfrom`/`sendto`) and already decode COBS frames. They need the same stream-socket conversion for continued use as test clients; scoped into `07-02`.
