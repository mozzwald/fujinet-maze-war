> Branch note (2026-09-09): the Phase 7 server on `realm-net` now uses TCP.
> The historical UDP bots/relay in this directory have not been migrated;
> use `tests/tcp_transport_smoke.sh` and the Phase 7 validation notes for TCP.
> The Atari AI-socket inspection helpers remain usable.

## Current TCP movement baseline (2026-09-10)

`tcp_movement_probe.py` is a standalone TCP diagnostic using the current real
server and COBS/CRC protocol. It creates an isolated loopback server and open
map, sends commands at the Atari's current frame cadence, records snapshot and
application timing as JSON, and cleans up its own server. It does not drive
Atari800 or measure rendering/SIO. Run twice per rate:

```sh
make build/maze-war-server
python3 tests/rig/tcp_movement_probe.py --fps 60 --seconds 10 > /tmp/cadence-60.json
python3 tests/rig/tcp_movement_probe.py --fps 50 --seconds 10 > /tmp/cadence-50.json
```

By default the probe uses 6 frames at 60 Hz and 5 frames at 50 Hz, i.e. a
10 Hz command stream. Override with `--frames N` to reproduce older pacing or
stress a proposed client cadence. The 50 Hz option simulates PAL input pacing;
it is not a PAL emulator test.
See `planning/phases/04-render-state-separation/04-LAG-REVIEW.md` for the results
and current repair plan. The historical UDP conclusions below do not establish
smoothness of the current TCP renderer. In particular, convergence after a stop
and a small cell gap do not measure animation phase timing or display latency.

# Rig: reproducing lag and rendering faults on the emulator

Not part of `make test`. These are the instruments the Phase 4 measurements in
`planning/phases/04-render-state-separation/04-RESEARCH.md` were taken with, kept
so the numbers can be reproduced rather than re-derived.

Loopback delivers every packet instantly and never drops one, which is why the
reported lag and snapping never appeared on the emulator. `link.py` is what
closes that gap.

**Read "Trusting a number out of this rig" before running anything.** Two
separate instrument bugs, both silent and both capable of producing confident,
wrong, and non-reproducible numbers, were found and fixed in this file's
history. The rig is trustworthy now, but only if used the way this file
describes.

## Setting it up

```sh
# 1. server on a private port, open map so geometry never confounds a measurement
./build/maze-war-server --bind 127.0.0.1 --port 9500 --zombies 0 \
    --brick open.txt --debug

# 2. the link the Atari actually talks to: 120 ms one way, 3 % loss.
#    Give it a control file up front -- see "Changing conditions" below.
echo "120 0.03" > tests/rig/link_control.txt
python3 tests/rig/link.py 127.0.0.2 9000 9500 120 0.03 tests/rig/link_control.txt

# 3. boot the Atari against 127.0.0.2 (see ai.py: AI.boot), once, and leave it
# 4. a remote player to watch -- see "patrol.py vs movestop.py" below
python3 tests/rig/movestop.py 9500
```

`link.py` must bind a loopback **alias**, never `0.0.0.0`: FujiNet-PC binds its
own netstream socket to the destination port with `SO_REUSEADDR`, so sharing
`9000` on the same address makes delivery order-dependent.

## Changing conditions without restarting the relay

**Never kill and restart `link.py` while the Atari is connected through it**,
even to switch delay/loss for the next run. Doing that was found to permanently
wedge the Atari-side netstream handshake: `netsio_status` showed
`netstream.active` flip to `false` and `sync.timeouts` increase, and the link
never recovered without a full cold reset (`atari_reset`) and reboot of the
game. The mechanism: the game-level UDP path (not the netsio/SIO channel --
that's separate and unaffected) goes quiet for the second or so between killing
the old process and the new one binding, and that's apparently enough to trip
whatever timeout the netstream driver runs. A real link never actually does
this -- a real listener is always there even when a given packet is lost -- so
this is a rig artifact, not a finding about the client.

Instead, `link.py` takes an optional control-file path (6th argument) and polls
its mtime once per loop iteration. Write `"DELAY_MS DROP_FRACTION"` to that file
and the running process picks it up on the next iteration, with the Atari's
connection never interrupted:

```sh
echo "120 0.20" > tests/rig/link_control.txt   # now 120ms / 20% loss, live
```

Confirm the change landed by tailing the relay's own stdout (it prints
`control: delay=...ms drop=...` on each change) rather than assuming the write
succeeded.

## Reading the machine

`ai.py`'s `AI` class speaks the emulator's AI socket (length-prefixed JSON).
**It enforces a minimum ~8ms gap between commands internally -- do not bypass
`AI.cmd()`/`AI.peek()` with a raw socket loop, and do not remove or shrink
`MIN_CMD_INTERVAL`.** See the emulator pacing bug below for why this exists;
it is not a style preference.

Addresses move whenever the client is rebuilt. Look them up in
`build/maze-war.lab`, never from memory — `HOSTBUF`, `NET_DEAD_MASK`,
`NET_PX_X` and friends have all moved at least once.

Two traps in the boot path, both handled by `AI.boot`: the MCP key table has no
`.` key, so the host string is poked into `HOSTBUF` rather than typed, and the
prompt ignores backspace from the AI socket. RETURN is keycode `$0C`.

## The measurements

- `gap.py` — how far a remote actor's rendered cell trails the authoritative
  cell the client has received, over a fixed duration. Picks a slot that is
  live, not hidden, and demonstrably moving; aborts rather than report a number
  it cannot stand behind.
- `converge.py` — the more useful of the two. Splits the same gap measurement
  into "moving" (the watched slot's authoritative position changed within the
  last second) and "still" (it hasn't). A gap while moving that is not
  reproduced while still is ordinary transit latency; a gap that survives well
  into "still" is a real stuck-reconciliation fault. Needs a bot that actually
  stops sometimes -- `movestop.py`, not `patrol.py`.
- `leak.py` — painted interior cells no visible actor accounts for, with how
  long each persisted. On an open map every interior character must belong to an
  actor, which makes this a clean oracle for rendering residue.
- `park.py` — puts the local actor on a fixed cell before a run. Two runs are
  only comparable if the geometry is; a player left on a random spawn sits in
  the patrol lane on one run and not the next.

### `patrol.py` vs `movestop.py`

Two different remote bots for two different questions:

- `patrol.py` walks a rectangle and never stops. Use it for `gap.py`, or for
  anything that just needs "something is continuously moving."
- `movestop.py` alternates ~4s of walking with ~4s of standing dead still, and
  auto-restarts its own socket loop on any exception (an earlier version
  without this silently died mid-run — server log showed `client disconnected`
  with no trace in the bot's own log, and every measurement after that point
  was against a frozen, disconnected slot). Use it with `converge.py`, since
  that script needs genuine still periods to classify samples into.

Either way, check the bot process is actually still alive
(`pgrep -af movestop.py`) before trusting a long run's results, and check
`dead`/`seat` masks (peek `0x7C00` / `0x7B52`) show the watched slot as
occupied, not vacant.

## Trusting a number out of this rig

Two separate, serious instrument bugs were found here. Both produced
confident, plausible-looking, wrong numbers, and both are why "run it twice
and expect agreement" is load-bearing advice, not paranoia.

### Bug 1: an unoccupied slot looks like a broken client

An earlier version of `gap.py` chose which slot to watch as "not mine" and
nothing more. **An unoccupied slot is hidden and parked on its placeholder cell
`(1, slot+1)` while the server still holds a spawn position for it**, so
measuring one yields a large, perfectly constant gap that looks exactly like a
catastrophic rendering fault. Several confident conclusions were drawn from
that before it was spotted, in both directions, on the same build.

Fixed: `gap.py` and `converge.py` both pick a slot that is live, not hidden,
and demonstrably moving during a three-second probe, and abort the run
otherwise.

### Bug 2: peeking the emulator too fast freezes it

This one is worse and took most of a session to find, because it doesn't look
like an instrument problem -- it looks like the client's behavior is genuinely
erratic and non-reproducible.

**The atari800 emulator, run through the AI socket, does not free-run on its
own thread.** Its main loop only advances a frame in the gaps between AI-socket
commands. A tight loop of `peek()` calls issued back-to-back with no gap starves
it completely: verified directly, 1844 peek calls in 2 real seconds left the
CPU's PC/A/X/Y/SP registers byte-for-byte unchanged. RTCLOK (`$14`, the OS's own
VBI counter) confirmed it: a zero-gap peek loop advances RTCLOK at 0 Hz over 5
real seconds; the same loop with even a 5ms sleep between calls tracks real
time at ~58 Hz (NTSC is 60 Hz; the gap is ordinary call overhead). The `run`
command doesn't need this treatment -- it blocks synchronously for the real
duration of the frames it's asked to run (`run(60)` took 1.001 real seconds),
so it already advances things correctly on its own.

The earlier session's `lagmeasure.py` (now deleted) sampled in a bare
`while time.time() - t0 < N: peek()` loop with no gap. **That script was
intermittently pausing the emulated Atari** while the real server, real relay
and real remote bot kept running on their own OS processes in true wall-clock
time. Depending on exactly how long a given pause landed and what the server
did to the (frozen) client's slot during it, the same build could measure as
"perfectly tracking" or "catastrophically diverged" from one run to the next --
which is exactly the contradictory result that triggered the retraction in
`04-RESEARCH.md`. The client was not misbehaving; the rig was pausing it.

Fixed: `AI.cmd()` in `ai.py` now enforces a minimum ~8ms gap between commands
(comfortably above the empirically-verified 5ms floor) before every socket
round trip, so nothing built on `AI.peek()`/`AI.cmd()` can reintroduce this by
sampling too eagerly. Verified after the fix: an unmodified `peek()`-in-a-loop
matches real time at ~58 Hz with no manual sleep in the calling script at all,
including through `AI.state()`'s three chained calls per iteration.

**If a future rig script bypasses the `AI` class and talks to the AI socket
directly (as `patrol.py`/`movestop.py`/`bot.py` do, deliberately, for the game's
own UDP protocol -- that part is fine, it's a different socket), it must
either use `AI.cmd()` or reimplement the same minimum-gap discipline. There is
no way to detect this failure mode by inspecting a single run; it only shows up
as two runs of the same build disagreeing.**

### Discipline this leaves behind

- Run any comparison at least twice per build and condition. Treat two runs of
  the same build that disagree as a broken instrument, not as a result -- that
  is genuinely how both bugs above were found.
- Before trusting a long run, confirm the bot is still alive and its slot shows
  occupied (see `patrol.py` vs `movestop.py` above).
- Never restart `link.py` mid-session; use the control file.
- Never remove or shrink `MIN_CMD_INTERVAL` in `ai.py`, and never add a
  polling loop that talks to the AI socket without going through it.

## What the rig found, once it could be trusted (2026-09-09)

With both bugs fixed, `converge.py` was run against unmodified HEAD (no
reconciliation code changes) across 0%, 3%, 20% and 50% loss at 120ms one-way
delay, 2-3 runs each, 40-100s per run:

- **While the watched remote is actively moving:** a small, real, gap of
  mostly 0-1 cells (roughly 60-87% at gap 0 depending on loss), a shrinking
  tail out to 4-5 cells, and -- across every condition tested, including
  50% independent random loss -- **never once reaching the 3-cell or 10-cell
  thresholds** (`NET_RECOVER_P1`/`NET_RECON_P1`) that would engage the
  bounded-recovery or hard-snap paths in `REMOTE_FOLLOW`.
- **Once the watched remote has been still for more than a second:** gap 0 in
  **100% of samples, in every run, at every loss rate tested**, including
  three separate runs at 3% loss (799+ samples) and one at 50% loss (1365
  samples). No run ever showed a nonzero gap surviving into a genuine still
  period.

Conclusion: the leading hypothesis from the retracted `04-06-PLAN.md` -- that
`RF_SNAP`'s `MOVEST != 0` guard leaves a diverged remote actor permanently
stuck because a following actor is mid-move nearly all the time -- **does not
reproduce**. The reconciliation converges correctly and reliably under every
condition tested, including conditions considerably worse than the original
120ms/3% target. There is no stuck-state bug here to fix.

The residual "moving" gap is not obviously a defect either: `REMOTE_FOLLOW`
deliberately walks toward the target one cell per move tick rather than
snapping instantly to each new authoritative position (that smooth walk is the
entire point -- an instant snap to every snapshot would be far more visually
jarring), so some transient lag while the target is actively moving is the
expected cost of that design, not a bug in it. Independent random loss, even at
50%, rarely produces the *sustained* multi-tick blackout needed to build up a
large gap, because each snapshot has an independent chance of arriving.

What this does **not** rule out: the client has no render-only position for
remote actors at all (see `planning/phases/04-render-state-separation/`), so
the *smoothness* of that one-cell-per-tick walk -- as opposed to whether it gets
stuck -- is exactly what the already-planned `04-02`/`04-03` render-state
separation work addresses. This rig, now trustworthy, is the right way to
measure whether that work actually helps once it exists.
