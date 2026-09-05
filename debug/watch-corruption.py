#!/usr/bin/env python3
"""Watch a running maze-war client for the first sign of graphical corruption
and dump the machine state at that instant.

Three symptoms have been reported from interactive play that scripted input has
not reproduced: coloured dots down an actor's column, a border cell vanishing,
and the wizard's head clipping. All three are cheap to detect from screen and
PM memory, so rather than guess at the input pattern, play normally and let this
catch the moment it happens.

    python3 debug/watch-corruption.py [--sock /path/to/ai.sock]

Prints a line per event and keeps running. Ctrl-C to stop.
"""
import argparse, glob, json, socket, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--sock", default=None)
ap.add_argument("--interval", type=float, default=0.5)
args = ap.parse_args()

SOCK = args.sock or sorted(glob.glob('/tmp/atari800-mcp/*/ai.sock'))[-1]


def cmd(c):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(10)
    s.connect(SOCK)
    d = json.dumps(c).encode(); s.sendall(str(len(d)).encode() + b'\n' + d)
    h = b''
    while not h.endswith(b'\n'): h += s.recv(1)
    n = int(h.strip()); b = b''
    while len(b) < n: b += s.recv(n - len(b))
    s.close(); return json.loads(b.decode())


def pk(a, n=1): return cmd({"cmd": "peek", "addr": a, "len": n})["data"]


GAMESCR = 0x73C0
SYMS = {}
for line in open(__file__.rsplit('/', 2)[0] + '/build/maze-war.lab'):
    f = line.split()
    if len(f) >= 3: SYMS[f[2]] = int(f[1], 16)
S = SYMS.get


def screen():
    s = []
    for off in range(0, 760, 256): s += pk(GAMESCR + off, min(256, 760 - off))
    return s


def state():
    return dict(
        LOCX=pk(S('LOCX'), 4), LOCY=pk(S('LOCY'), 4),
        RNDX=pk(S('RNDX'), 4), RNDY=pk(S('RNDY'), 4),
        DIR=pk(S('DIR'), 4), MOVEST=pk(S('MOVEST'), 4),
        ACTFLAG=pk(S('ACTFLAG'), 4), SHOTDIR=pk(S('SHOTDIR'), 4),
        SHOTMST=pk(S('SHOTMST'), 4),
        RCHASE=pk(S('NET_RCHASE'), 1)[0],
        PEND=pk(S('NET_PEND_COUNT'), 1)[0],
        PX=pk(S('NET_PX_X'), 4), PY=pk(S('NET_PX_Y'), 4),
        DEAD=pk(S('NET_DEAD_MASK'), 1)[0],
        ERASE=pk(S('NET_ERASE_MASK'), 1)[0],
        CKBAD=pk(S('NET_CK_BAD'), 1)[0],
    )


def pm_pages():
    return [[j for j, v in enumerate(pk(0x3C00 + i * 256, 256)) if v] for i in range(4)]


def actor_cells():
    rx, ry = pk(S('RNDX'), 4), pk(S('RNDY'), 4)
    live = set()
    for i in range(4):
        for dy in (0, 1):
            for dx in range(4):
                live.add((ry[i] + dy) * 40 + rx[i] * 2 + dx)
    return live


MAZE_OK = {0x00, 0xA0, 0xFD, 0xFE, 0xFF}


def check(s):
    """return a list of (kind, detail) anomalies"""
    out = []
    # a border cell gone blank
    for col in range(20):
        for row in (0, 18):
            if s[row * 40 + col * 2] == 0 and s[row * 40 + col * 2 + 1] == 0:
                out.append(("border-blank", f"col{col} row{row}"))
    for row in range(19):
        for col in (0, 19):
            if s[row * 40 + col * 2] == 0 and s[row * 40 + col * 2 + 1] == 0:
                out.append(("border-blank", f"col{col} row{row}"))
    # actor glyphs stranded away from any actor
    live = actor_cells()
    for idx, c in enumerate(s):
        if 0xC0 <= c <= 0xDF and idx not in live:
            out.append(("stray-actor-glyph", f"col{idx%40//2} row{idx//40} $%02X" % c))
        elif c not in MAZE_OK and not (0xC0 <= c <= 0xDF) and idx not in live:
            out.append(("stray-glyph", f"col{idx%40//2} row{idx//40} $%02X" % c))
    # player-missile residue: more than one 8-row band lit in a page
    for i, nz in enumerate(pm_pages()):
        bands = sorted({j // 8 for j in nz})
        if len(bands) > 2:
            out.append(("pm-residue", f"PL{i} bands {bands}"))
    return out


print(f"watching {SOCK}")
print("play normally; anomalies print here with the state that produced them\n")
seen = set()
base = None
pending = {}
# Every reported symptom is PERSISTENT -- bricks that never came back, dots that
# accumulate, a head that stays clipped. Shots and explosions are legitimately
# on the playfield for a few frames and would otherwise swamp the output, so
# only report a cell that stays wrong across several samples.
NEED = max(3, int(2.0 / args.interval))
while True:
    try:
        s = screen()
    except Exception as e:
        print("read failed:", e); time.sleep(2); continue
    if pk(S('LOCX'), 4) == [1, 1, 1, 1]:
        time.sleep(args.interval); continue      # not joined yet
    now = {(k, d) for k, d in check(s)}
    if base is None:
        base = now                               # ignore anything already true
        print(f"baseline captured ({len(base)} pre-existing); "
              f"an anomaly must persist {NEED} samples to report\n")
    for key in list(pending):
        if key not in now: del pending[key]
    for key in now:
        if key in base or key in seen: continue
        pending[key] = pending.get(key, 0) + 1
    for key, n in list(pending.items()):
        if n < NEED: continue
        kind, detail = key
        seen.add(key); del pending[key]
        st = state()
        print(f"[{time.strftime('%H:%M:%S')}] {kind}: {detail}")
        for k, v in st.items(): print(f"      {k}={v}")
        try:
            r = cmd({"cmd": "screenshot"})
            print(f"      screenshot: {r.get('path')}")
        except Exception:
            pass
        print()
    time.sleep(args.interval)
