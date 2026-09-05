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
import argparse, glob, json, os, socket, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--sock", default=None, help="path to the atari800-ai socket")
ap.add_argument("--interval", type=float, default=0.5)
args = ap.parse_args()


def find_sockets():
    """Ask the running emulators where their sockets are.

    Do not guess from a fixed path: an MCP-managed emulator puts its socket
    under /tmp/atari800-mcp/<session>/, while a hand-started atari800-ai puts
    it wherever -ai-socket said. Reading the command line covers both.
    """
    found = []
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            argv = open(f'/proc/{pid}/cmdline', 'rb').read().split(b'\0')
        except OSError:
            continue
        argv = [a.decode('utf-8', 'replace') for a in argv if a]
        if not argv or 'atari800' not in argv[0]:
            continue
        for i, a in enumerate(argv):
            if a == '-ai-socket' and i + 1 < len(argv):
                found.append((int(pid), argv[i + 1]))
    # anything left lying around, in case the process list missed it
    for g in ('/tmp/*.sock', '/tmp/atari800*/ai.sock',
              '/tmp/atari800-mcp/*/ai.sock', '/tmp/*/ai.sock'):
        for path in glob.glob(g):
            if os.path.exists(path) and path not in [p for _, p in found]:
                found.append((None, path))
    return found


if args.sock:
    SOCK = args.sock
else:
    cands = find_sockets()
    live = [(pid, p) for pid, p in cands if pid and os.path.exists(p)]
    if not live and not cands:
        sys.exit("no atari800 -ai socket found. Start the emulator first, or "
                 "pass --sock /path/to/ai.sock")
    pick = live or cands
    if len(pick) > 1:
        print("more than one emulator socket is present:")
        for pid, p in pick:
            print(f"   {p}" + (f"   (pid {pid})" if pid else "   (stale?)"))
        sys.exit("pass --sock to say which one to watch")
    SOCK = pick[0][1]


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


# Fail at once on a socket that is not there or not answering. Retrying
# silently forever looks identical to "playing but nothing is wrong", which is
# exactly the failure this tool exists to avoid.
if not os.path.exists(SOCK):
    sys.exit(f"{SOCK} does not exist -- is the emulator running?")
try:
    if cmd({"cmd": "ping"}).get("msg") != "pong":
        sys.exit(f"{SOCK} answered but not as an atari800-ai socket")
except Exception as e:
    sys.exit(f"cannot talk to {SOCK}: {e}")


GAMESCR = 0x73C0
SYMS = {}
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.path.join(REPO, 'build', 'maze-war.lab')
if not os.path.exists(LAB):
    sys.exit(f"{LAB} not found -- build the client first (make)")
for line in open(LAB):
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


# Codes the playfield is ever supposed to contain.
MAZE_OK   = {0x00, 0xA0, 0xFD, 0xFE, 0xFF}      # blank, space, brick/border
ACTOR_OK  = set(range(0xC0, 0xE0))              # wizard shapes
SHOT_OK   = {0x81, 0x82, 0x83, 0x84, 0x85}      # bullets
EXPL_OK   = {0x9B, 0x9C, 0x9D}                  # shrapnel
# $08-$0F are PL0CHR, the per-player coalesce tiles SETFUZZ rewrites at
# runtime, drawn on the playfield as a wizard materialises. Calling these
# illegal produced a false "illegal-glyph code $0A" report on a perfectly
# healthy coalescing player.
COALESCE_OK = set(range(0x08, 0x10)) | set(range(0x88, 0x90))
LEGAL = MAZE_OK | ACTOR_OK | SHOT_OK | EXPL_OK | COALESCE_OK

CHARSET = 0x4000


def build_charset():
    """the font as assembled, to compare the live one against"""
    d = open(os.path.join(REPO, 'build', 'maze-war.xex'), 'rb').read()
    i = 2 if d[:2] == b'\xff\xff' else 0
    segs = []
    while i + 4 <= len(d):
        a = d[i] | (d[i+1] << 8); e = d[i+2] | (d[i+3] << 8)
        if a == 0xFFFF:
            i += 2; continue
        n = e - a + 1
        segs.append((a, e, d[i+4:i+4+n])); i += 4 + n
    for a, e, b in segs:
        if a <= CHARSET and CHARSET + 1023 <= e:
            return list(b[CHARSET-a:CHARSET-a+1024])
    return None


REF_FONT = build_charset()


def check(s):
    """anomalies as (kind, detail, urgent) -- urgent ones skip the persistence
    filter because the thing they describe moves and would never sit still"""
    out = []

    # 1. an illegal glyph code anywhere on the playfield. Bullets move, so this
    #    is reported at once; waiting for it to persist is why the previous
    #    version of this tool saw nothing while bullets rendered as letters.
    for idx, c in enumerate(s):
        if c not in LEGAL:
            out.append(("illegal-glyph",
                        "col%d row%d code $%02X" % (idx % 40 // 2, idx // 40, c),
                        True))

    # 2. the font itself being rewritten under the game
    if REF_FONT:
        live = []
        for off in range(0, 1024, 256):
            live += pk(CHARSET + off, 256)
        bad = sorted({j // 8 for j in range(1024) if live[j] != REF_FONT[j]})
        if bad:
            out.append(("charset-modified",
                        "glyph slots " + ",".join("$%02X" % g for g in bad[:12]),
                        True))

    # 3. shot state out of range: SHOTDIR indexes a 4-entry table, and the
    #    shape offsets into SHOTSHP are deliberately unaligned, so a bad
    #    direction reads whatever follows the table as screen codes
    sd, sm = pk(S('SHOTDIR'), 4), pk(S('SHOTMST'), 4)
    for i in range(4):
        if sd[i] > 3:
            out.append(("shotdir-range", f"slot{i} SHOTDIR={sd[i]}", True))
        if sm[i] > 3:
            out.append(("shotmst-range", f"slot{i} SHOTMST={sm[i]}", True))

    # 4. a slot flagged as firing with no bullet drawn anywhere -- the
    #    "bullets stopped appearing" symptom, which is an ABSENCE and so was
    #    invisible to a detector that only looked for stray graphics
    act = pk(S('ACTFLAG'), 4)
    drawn = any(c in SHOT_OK for c in s)
    for i in range(4):
        if act[i] & 0x80 and not drawn:
            out.append(("shot-invisible",
                        f"slot{i} ACTFLAG=$%02X but no bullet glyph on screen"
                        % act[i], False))

    # 5. actor-missing: a live, non-evaporating slot whose drawn cell is blank
    rx, ry = pk(S('RNDX'), 4), pk(S('RNDY'), 4)
    dead = pk(S('NET_DEAD_MASK'), 1)[0]
    for i in range(4):
        if dead & (1 << i) or (act[i] & 0x03):
            continue
        if not (1 <= rx[i] <= 18 and 1 <= ry[i] <= 17):
            continue
        base = ry[i] * 40 + rx[i] * 2
        if s[base] == 0 and s[base+1] == 0:
            out.append(("actor-not-drawn",
                        f"slot{i} at ({rx[i]},{ry[i]}) draws blank", False))

    # 6. player-missile residue
    for i, nz in enumerate(pm_pages()):
        bands = sorted({j // 8 for j in nz})
        if len(bands) > 2:
            out.append(("pm-residue", f"PL{i} bands {bands}", False))
    return out


print(f"watching {SOCK}")
print("play normally; anomalies print here with the state that produced them\n")
seen = set()
base = None
pending = {}
fails = 0
# Every reported symptom is PERSISTENT -- bricks that never came back, dots that
# accumulate, a head that stays clipped. Shots and explosions are legitimately
# on the playfield for a few frames and would otherwise swamp the output, so
# only report a cell that stays wrong across several samples.
NEED = max(3, int(2.0 / args.interval))
while True:
    try:
        s = screen()
    except Exception as e:
        fails += 1
        if fails > 10:
            sys.exit(f"lost the emulator after 10 failed reads: {e}")
        print("read failed:", e); time.sleep(2); continue
    fails = 0
    if pk(S('LOCX'), 4) == [1, 1, 1, 1]:
        time.sleep(args.interval); continue      # not joined yet
    found = check(s)
    now = {(k, d) for k, d, _ in found}
    urgent = {(k, d) for k, d, u in found if u}
    if base is None:
        base = now
        # Do not swallow these. If the watcher is started after the trouble
        # begins -- or the game is already damaged -- silently baselining the
        # damage makes the tool report a clean bill of health on a broken game,
        # which is the worst thing it could do.
        if base:
            print(f"ALREADY WRONG at startup ({len(base)}):")
            for k, d in sorted(base):
                print(f"   {k}: {d}")
            print("   (these are not repeated below)\n")
        else:
            print("baseline clean\n")
        print(f"an anomaly must persist {NEED} samples to report "
              f"(urgent kinds report at once)\n")
    for key in list(pending):
        if key not in now: del pending[key]
    for key in now:
        if key in base or key in seen: continue
        # urgent kinds fire on first sight; the rest must persist
        pending[key] = pending.get(key, 0) + (NEED if key in urgent else 1)
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
