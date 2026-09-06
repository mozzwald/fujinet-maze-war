#!/usr/bin/env python3
"""Report which correction path fires, live, while you play.

Scripted movement on loopback produces zero corrections over hundreds of cells,
so the ones that are annoying in real play come from something the scripts do
not do. The client already counts corrections in three buckets, and which one
dominates points straight at the cause:

  staged  drift reached NET_RECON_P0 (3 cells) while inputs were outstanding --
          the client and server disagree about where the wizard is, by a lot.
          Consistent with lost or dropped inputs.
  idle    the player stopped, stayed still for NET_IDLE_SETTLE frames, and the
          positions still disagreed. A quiet, accumulated divergence.
  vbi     the per-frame threshold path, which only fires with NO inputs
          outstanding -- so client and server had both settled and still
          disagreed. That is a genuine simulation divergence, not lag.

Run it alongside a normal session and play until the walk-back annoys you:

    python3 debug/correction-report.py --sock /tmp/atari800_ai.sock

It prints a line whenever a counter moves, with the state at that moment, and
a running total. Ctrl-C for a summary.
"""
import argparse, glob, json, os, re, socket, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LAB = os.path.join(REPO, "build", "maze-war.lab")

ap = argparse.ArgumentParser()
ap.add_argument("--sock", default=None)
ap.add_argument("--interval", type=float, default=0.25)
args = ap.parse_args()


def find_socket():
    found = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            argv = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")
        except OSError:
            continue
        argv = [a.decode("utf-8", "replace") for a in argv if a]
        if argv and "atari800" in argv[0]:
            for i, a in enumerate(argv):
                if a == "-ai-socket" and i + 1 < len(argv):
                    found.append(argv[i + 1])
    found += [p for p in glob.glob("/tmp/*.sock") + glob.glob("/tmp/*/ai.sock")
              if p not in found]
    return found


if args.sock:
    SOCK = args.sock
else:
    c = find_socket()
    if not c:
        sys.exit("no atari800 -ai socket found; pass --sock")
    if len(c) > 1:
        sys.exit("several emulator sockets present:\n  " + "\n  ".join(c) +
                 "\npass --sock to choose")
    SOCK = c[0]

if not os.path.exists(LAB):
    sys.exit(f"{LAB} not found -- build the client first (make)")
LABTXT = open(LAB).read()


def sym(name):
    # Always resolve against the current build. Hard-coded addresses have
    # already produced one report of 30 corrections on a client that had not
    # moved, because a storage change had shifted every symbol after it.
    m = re.search(r"^\S+\s+([0-9A-F]{4})\s+%s$" % re.escape(name), LABTXT, re.M)
    if not m:
        sys.exit(f"symbol {name} not in {LAB} -- rebuild?")
    return int(m.group(1), 16)


def cmd(c):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(10)
    s.connect(SOCK)
    d = json.dumps(c).encode(); s.sendall(str(len(d)).encode() + b"\n" + d)
    h = b""
    while not h.endswith(b"\n"):
        h += s.recv(1)
    n = int(h.strip()); b = b""
    while len(b) < n:
        b += s.recv(n - len(b))
    s.close(); return json.loads(b.decode())


def pk(a, n=1):
    return cmd({"cmd": "peek", "addr": a, "len": n})["data"]


CNT, PID = sym("NET_DIAG_CNT"), sym("NET_LOCAL_PID")
LOCX, LOCY = sym("LOCX"), sym("LOCY")
PXX, PXY = sym("NET_PX_X"), sym("NET_PX_Y")
PEND, CKBAD = sym("NET_PEND_COUNT"), sym("NET_CK_BAD")
DIR, MOVEST = sym("DIR"), sym("MOVEST")
NAMES = ["staged", "idle", "vbi", "remote"]

print(f"watching {SOCK}")
print("play normally; a line prints whenever a correction fires\n")
prev = None
totals = [0, 0, 0, 0]
ck0 = None
try:
    while True:
        try:
            pid = pk(PID, 1)[0]
            if pid > 3:
                time.sleep(args.interval); continue
            cur = pk(CNT, 4)
            ck = pk(CKBAD, 1)[0]
        except Exception as e:
            print("read failed:", e); time.sleep(2); continue
        if ck0 is None:
            ck0 = ck
        if prev is None:
            prev = cur
            print("counters attached; baseline taken\n")
            continue
        d = [(cur[i] - prev[i]) & 0xFF for i in range(4)]
        if any(d):
            lx, ly = pk(LOCX, 4)[pid], pk(LOCY, 4)[pid]
            px, py = pk(PXX, 4)[pid], pk(PXY, 4)[pid]
            for i, n in enumerate(d):
                totals[i] += n
            fired = ", ".join(f"{NAMES[i]}+{d[i]}" for i in range(4) if d[i])
            print(f"[{time.strftime('%H:%M:%S')}] {fired}")
            print(f"    client ({lx},{ly})  server ({px},{py})  "
                  f"off by {abs(lx-px)+abs(ly-py)} cells")
            print(f"    pending inputs={pk(PEND,1)[0]}  dir={pk(DIR,4)[pid]}  "
                  f"movest={pk(MOVEST,4)[pid]}  bad frames since start="
                  f"{(ck-ck0)&0xFF}")
            sys.stdout.flush()
        prev = cur
        time.sleep(args.interval)
except KeyboardInterrupt:
    print("\n--- totals ---")
    for i, n in enumerate(NAMES):
        print(f"  {n:7s} {totals[i]}")
    print(f"  checksum-rejected frames: {(ck-ck0)&0xFF}")
    print("\nstaged dominating suggests lost/dropped inputs; vbi dominating "
          "suggests the two simulations genuinely diverge with nothing in "
          "flight; idle suggests slow accumulated drift.")
