# Rig: reproducing lag and rendering faults on the emulator

Not part of `make test`. These are the instruments the Phase 4 measurements in
`planning/phases/04-render-state-separation/04-RESEARCH.md` were taken with, kept
so the numbers can be reproduced rather than re-derived.

Loopback delivers every packet instantly and never drops one, which is why the
reported lag and snapping never appeared on the emulator. `link.py` is what
closes that gap.

## Setting it up

```sh
# 1. server on a private port, open map so geometry never confounds a measurement
./build/maze-war-server --bind 127.0.0.1 --port 9500 --zombies 0 \
    --brick open.txt --debug

# 2. the link the Atari actually talks to: 120 ms one way, 3 % loss
python3 tests/rig/link.py 127.0.0.2 9000 9500 120 0.03

# 3. boot the Atari against 127.0.0.2 (see ai.py: AI.boot)
# 4. a remote player to watch
python3 tests/rig/bot.py 0
```

`link.py` must bind a loopback **alias**, never `0.0.0.0`: FujiNet-PC binds its
own netstream socket to the destination port with `SO_REUSEADDR`, so sharing
`9000` on the same address makes delivery order-dependent.

## Reading the machine

`ai.py` speaks the emulator's AI socket (length-prefixed JSON) directly, which is
about 3.7 ms per call — fast enough to sample the screen and zero page at tens of
Hz. Going through the MCP `run_until` path instead single-steps frames and
distorts timing badly enough to manufacture artefacts; don't.

Addresses move whenever the client is rebuilt. Look them up in
`build/maze-war.lab`, never from memory — `HOSTBUF`, `NET_DEAD_MASK`,
`NET_PX_X` and friends have all moved at least once.

Two traps in the boot path, both handled by `AI.boot`: the MCP key table has no
`.` key, so the host string is poked into `HOSTBUF` rather than typed, and the
prompt ignores backspace from the AI socket. RETURN is keycode `$0C`.

## The measurements

- `gap.py` — how far a remote actor's rendered cell trails the authoritative
  cell the client has received. **Read the warning below before trusting a
  number out of this.**
- `leak.py` — painted interior cells no visible actor accounts for, with how
  long each persisted. On an open map every interior character must belong to an
  actor, which makes this a clean oracle for rendering residue.
- `patrol.py` — a remote that never stops moving. It turns on its own
  authoritative position rather than holding a stick into a wall, because a bot
  stalled against a wall looks stationary to the server, and measuring a
  stationary remote answers a different question than the one being asked.
- `park.py` — puts the local actor on a fixed cell before a run. Two runs are
  only comparable if the geometry is; a player left on a random spawn sits in
  the patrol lane on one run and not the next.

## Trusting a number out of this rig

This is the part that cost the most and is easiest to get wrong. An earlier
version of `gap.py` chose which slot to watch as "not mine" and nothing more.
**An unoccupied slot is hidden and parked on its placeholder cell `(1, slot+1)`
while the server still holds a spawn position for it**, so measuring one yields
a large, perfectly constant gap that looks exactly like a catastrophic
rendering fault. Several confident conclusions were drawn from that before it
was spotted, in both directions, on the same build.

`gap.py` now picks its slot deliberately — live, not hidden, and demonstrably
moving during a three-second probe — and aborts the run rather than report a
number it cannot stand behind. Two further sources of variance are worth
knowing:

- **Slot churn.** Every reconnect takes a new slot and the old one lingers for
  the 15 s client timeout. Boot the Atari once and leave it; a boot in the
  middle of a run invalidates it.
- **Which actor is where.** Park the local player out of the patrol rectangle,
  or the remote spends the run blocked against it and barely moves.

The discipline that follows: run any comparison at least twice per build, and
treat two runs of the same build that disagree as a broken instrument rather
than as a result.
