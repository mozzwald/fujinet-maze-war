# Phase 4: Render-State Separation — Research

**Researched:** 2026-09-09
**Domain:** Why a remote actor lags and snaps on the Atari client, measured rather than reasoned about
**Confidence:** HIGH for the mechanism, MEDIUM for how much of the felt lag each part contributes

## Why this exists

The Phase 4 plans (04-01 .. 04-03) were written before anyone could reproduce
the symptom. The standing note said so explicitly: *"Could NOT reproduce the
snap on the emulator rig, which is the important finding."* Loopback delivers
every packet instantly and never drops one, so the client always looked perfect.

That gap is now closed. `scratchpad/link.py`-style relay (recorded below) sits
between FujiNet-PC and the game server and adds one-way delay and loss in each
direction, which makes the reported behaviour appear on the emulator on demand
and, more importantly, **measurable**.

## The rig

- Game server on `127.0.0.1:9500`, open map (outer wall only) so the geometry
  never confounds the measurement, `--zombies 0`.
- Relay bound to `127.0.0.2:9000`, forwarding to `9500`, with configurable
  one-way delay and drop fraction. It must bind a loopback *alias*, not
  `0.0.0.0`: FujiNet-PC binds its own netstream socket to the destination port
  with `SO_REUSEADDR`, so sharing `9000` on the same address makes delivery
  order-dependent. The Atari is pointed at `127.0.0.2`.
- A scripted UDP bot joins directly on `9500` as the remote player and hunts.
- Sampling through the emulator's AI socket at ~230 Hz: `LOCX/LOCY` (`$A0`/`$A4`,
  the simulation cell that is drawn from) against `NET_PX_X/Y` (`$7C1A`/`$7C1E`,
  the last authoritative cell received), plus `NET_DEAD_MASK` (`$7C00`) and the
  diagnostic counters at `$7A89`.

## What was measured

Remote actor: how far the client's own simulation cell for that actor trails the
last authoritative cell it has actually received. This is deliberately *not* a
measure of transport delay — it is a measure of whether the client is faithful
to the stream it has, which is the only part the client can fix.

| Link | 0 cells behind | 1 cell | 8 cells | Longest unbroken run behind |
|------|---------------|--------|---------|-----------------------------|
| 0 ms, 0 % loss | 100 % | — | — | none |
| 120 ms, 0 % loss | 52.6 % | 41.8 % | 5.6 % | ~15 000 samples (~60 s) |
| 120 ms, 3 % loss | 0 % | **100 %** | — | every sample |

25 000 and 19 000 samples per row respectively.

## What that says

**1. `REMOTE_FOLLOW` has no catch-up, so a single missed step is permanent.**
It advances a remote actor at most one cell per move tick, and only when the
next cell in the actor's authoritative joy direction equals the authoritative
cell — it re-derives the path one cell at a time from position plus joy. Fall
one cell behind and there is no mechanism that ever closes the gap: the actor
walks at exactly the rate the server does, one cell in arrears, until a
divergence threshold fires. Under delay-plus-loss the offset is 100 % of
samples and unbroken.

That is the "remote players are laggy" complaint, and most of it is **not**
irreducible transport delay. One cell is a whole animation period, ~100 ms of
visual lag stacked on top of the real link delay.

**2. Divergence is bimodal, and the middle is empty.** The 5.6 % of samples at
8 cells are the other face: the follow loses the thread entirely, then
`NET_RECOVER_P1` (3) or `NET_RECON_P1` (10) resolves it with a jump. There is
nothing between "walk one cell per tick" and "teleport", which is why it reads
as snapping rather than as lag.

**3. The local player is not the problem.** `NET_DIAG_MAXDRIFT` did reach 17
under these conditions, but the local actor is predicted and reconciled, and the
user reports local movement feels fine. Treat the 17 as a respawn artefact until
someone shows otherwise — the counter's own note says it is an upper bound.

## Consequences for the plan

- **04-06 (new, do first).** Give the follow a bounded catch-up. Small,
  self-contained, no new state, and it removes the permanent one-cell offset
  that is the bulk of the felt lag. Measurable against the table above with the
  same rig, which makes it the cheapest real win available.
- **04-02 / 04-03 unchanged in intent.** Render-state separation is still the
  right end state: it is what removes the *quantisation* — one authoritative
  sample per 5-6 frames with nothing to interpolate between — and it is what
  turns the 8-cell case into a smooth converge instead of a jump. But it is a
  much larger change to a renderer with no spare state, and it should not be
  attempted until the cheap win is banked and the rig is trusted.
- **Do not smooth before measuring.** The standing warning still holds:
  interpolation over a stream that is missing updates turns a wrong jump into a
  wrong slide. The relay is what makes it possible to tell those apart, so every
  step in this phase should be quoted as a row in the table above.

## Not investigated

- Whether the same one-cell arrears affects shots. Shots are published
  authoritatively and drawn from packet coordinates, so they are probably
  exempt, but nobody has measured it.
- Real-hardware numbers. Everything here is the emulator plus a synthetic link.
  The shape should hold, the constants may not.
