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

- `lagmeasure.py` — how far a remote actor's simulation cell trails the last
  authoritative cell received. A constant offset is follow lag; spikes are
  stalls. These want different fixes, so tell them apart before designing one.
- `leak.py` — painted interior cells no visible actor accounts for, with how
  long each persisted. On an open map every interior character must belong to an
  actor, which makes this a clean oracle for rendering residue.
