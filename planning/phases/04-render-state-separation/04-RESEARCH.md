# Phase 4: Render-State Separation — Research

**Researched:** 2026-09-09 (two sessions)
**Domain:** Why a remote actor lags and snaps on the Atari client, measured rather than reasoned about
**Confidence:** HIGH for the final finding below. The rig had two separate serious bugs, both found and fixed; the numbers below are from the corrected instrument and reproduce across many runs and loss rates.

## Final finding (second session, after fixing the rig itself): no reconciliation bug found

The `MOVEST != 0` guard hypothesis from the first session (below) **does not
reproduce**. A second, deeper instrument bug was found first -- the rig itself
was pausing the emulated Atari mid-measurement, which is what produced the
contradictory "fixed it / made it worse" result that closed the first session.
Full detail, including how that bug was found and the fix, is in
`tests/rig/README.md` ("Trusting a number out of this rig"); the short version:

**The atari800 emulator does not free-run in the background.** It only
advances a frame in the gaps between AI-socket commands, so a tight
`peek()`-only sampling loop with no gap between calls freezes it completely
(verified: 1844 peek calls in 2 real seconds advanced the CPU's registers by
exactly zero). The previous session's measurement scripts sampled in exactly
that pattern, so they were intermittently pausing the client while the real
server and bot kept running in wall-clock time -- which produces essentially
random, non-reproducible readings that look exactly like a client bug. Fixed:
`ai.py`'s `AI.cmd()` now enforces a minimum ~8ms gap between commands. A
second, smaller bug (restarting the relay process wedges the Atari-side
netstream handshake) was also found and fixed via a live control-file
mechanism so the relay never has to restart mid-session.

With both fixed, `converge.py` (new: splits the gap measurement by whether the
watched remote is currently moving or has been still for >1s) was run against
**unmodified HEAD, no code changes**, across one-way 120ms delay at 0%, 3%,
20% and 50% independent random loss, 2-3 runs per condition, 40-100s each:

- While moving: a real but modest gap, 60-87% at 0 cells depending on loss,
  shrinking tail to 4-5 cells, **never once reaching the 3-cell or 10-cell
  thresholds** that would engage `REMOTE_FOLLOW`'s bounded-recovery or
  hard-snap paths, at any loss rate tested including 50%.
- Once still for more than a second: **gap 0 in 100% of samples, every run,
  every condition**, including 799+ samples at 3% loss (three separate runs)
  and 1365 samples at 50% loss.

**Conclusion: the reconciliation logic converges correctly and reliably.**
There is no stuck-state bug in `RF_SNAP`/`REMOTE_FOLLOW` to fix at the code
level the first session was investigating. The `MOVEST` guard fix that the
first session implemented, measured as contradictory, and reverted was
correctly reverted -- it wasn't needed, and the contradiction that flagged it
for further scrutiny was the rig freezing the emulator, not the guard.

**What this does not settle:** the residual "moving" gap is architecturally
expected, not a bug. `REMOTE_FOLLOW` walks one cell per move tick toward the
last known authoritative position rather than snapping instantly to each new
snapshot (deliberately -- an instant snap every ~100ms would look far worse),
so *some* lag while the target actively moves is the designed cost of smooth
walking without prediction. Whether that residual lag is small enough to feel
good, or whether it's what the user is calling "hard to play against," is a
presentation-quality judgment call, not something this rig can answer by
itself -- it needs a human playing against it. **Reducing it further requires
the render-state separation already planned in `04-02`/`04-03`** (giving
remote actors real interpolation instead of one-cell-per-tick walking), which
is a substantially larger, higher-risk change to core rendering that this
project's own established practice gates behind human hardware verification at
each step -- not something to attempt unsupervised. See `04-06-PLAN.md` for the
status of the smaller catch-up idea (retracted; not warranted given the finding
above).

## First session: how the leading hypothesis was formed (superseded above, kept for the reasoning)

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

## Correction, 2026-09-09: the table above is not trustworthy

Do not build on the numbers above. The script that produced them chose which
slot to watch as "not mine" and nothing more, and **an unoccupied slot is hidden
and parked on its placeholder cell `(1, slot+1)` while the server still holds a
spawn position for it**. Measuring one yields a large, perfectly constant gap
that reads exactly like a catastrophic rendering fault. The 100 %-at-one-cell
and the eight-cell tail may both be that artefact rather than the client.

What survived re-measurement with a corrected instrument, a continuously
patrolling remote and the local player parked out of its lane:

- A remote actor tracks its authoritative cell within about one cell in normal
  running. The client is not obviously broken.
- Two runs of the *same* build, in the two link conditions, disagreed about
  whether a candidate fix helped -- dramatically better at 3 % loss, worse with
  no loss at all. That is an instrument problem, not a result.

`tests/rig/gap.py` now picks a slot that is live, not hidden, and demonstrably
moving, and aborts rather than report a number it cannot stand behind. The rig
README records the other two variance sources found the hard way: slot churn
from reconnects, and the local player standing in the remote's path.

**A candidate fix was implemented, measured and reverted.** `RF_SNAP` refuses to
reposition a diverged remote while `MOVEST != 0`, and a remote that is following
is mid-move nearly all the time, so the snap that path exists to perform is
almost never allowed to run -- while `NET_AUTH_REPOS`, the thing it guards, is
built for exactly that case and zeroes `MOVEST` itself. That remains the best
suspect on the table. It was not shipped because the A/B could not be made to
agree with itself.

## What the original numbers said, kept for the reasoning only

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
