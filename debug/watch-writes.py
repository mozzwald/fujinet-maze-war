#!/usr/bin/env python3
"""Catch whatever is writing to the game's constant data tables.

The static-memory check in watch-corruption.py can say that SHOTSHP, SHAPES or
SUITS changed, but not who changed them -- and inferring the writer from the
damage has been wrong repeatedly. This uses the emulator's monitor breakpoints
to stop the CPU *on the instruction doing the write* and print it.

Requires an atari800 built with MONITOR_BREAKPOINTS (configure
--enable-monitorbreakpoints). It refuses to run otherwise rather than
pretending to watch.

    python3 debug/watch-writes.py --display :0 -- -netsio

Everything after `--` is passed to the emulator, so add -netsio (and start
FujiNet and the game server first) exactly as you normally would. Play until
the glyphs go wrong; each write to a watched address prints the instruction,
the registers and the nearest symbol, then the emulator carries on.

Why it drives the emulator itself: when a breakpoint hits, atari800 drops into
its interactive monitor, which takes over stdin/stdout and stops serving the AI
socket. So the monitor has to be driven over a pipe, which means owning the
process. (Started with no stdin it spins printing prompts -- that is how this
was found, via a 111MB log.)
"""
import argparse, json, os, re, select, socket, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ap = argparse.ArgumentParser()
ap.add_argument("--emulator", default="/home/mozzwald/build/atari800/src/atari800",
                help="atari800 built with MONITOR_BREAKPOINTS")
ap.add_argument("--program", default=os.path.join(REPO, "build", "maze-war-net.xex"))
ap.add_argument("--sock", default="/tmp/a8-watchwrites.sock")
ap.add_argument("--display", default=os.environ.get("DISPLAY", ":0"))
ap.add_argument("--range", nargs=2, metavar=("START", "END"), default=None,
                help="address range to watch, symbols or hex "
                     "(default: the shape tables SHAPES..EXPLSHP end)")
ap.add_argument("--machine", default="-xl")
ap.add_argument("emu_args", nargs="*", help="args after -- go to the emulator")
args = ap.parse_args()

# --- symbols, for naming addresses and the PC that writes them -------------
SYMS, BYADDR = {}, []
lab = os.path.join(REPO, "build", "maze-war.lab")
if os.path.exists(lab):
    for line in open(lab):
        f = line.split()
        if len(f) >= 3:
            try:
                a = int(f[1], 16)
            except ValueError:
                continue
            SYMS[f[2]] = a
            BYADDR.append((a, f[2]))
    BYADDR.sort()
else:
    sys.exit(f"{lab} not found -- build the client first (make)")


# The label file also carries hardware and OS equates (GRACTL $D01D,
# SETVBV $E45C), so its address span is NOT the game's image -- using it made
# the OS frame counter at $C0E2 look like it was inside the program. Take the
# real bounds from the segments the loader actually loads.
def _image_ranges():
    x = os.path.join(REPO, "build", "maze-war.xex")
    if not os.path.exists(x):
        return []
    d = open(x, "rb").read()
    i = 2 if d[:2] == b"\xff\xff" else 0
    out = []
    while i + 4 <= len(d):
        a = d[i] | (d[i+1] << 8); e = d[i+2] | (d[i+3] << 8)
        if a == 0xFFFF:
            i += 2; continue
        n = e - a + 1
        out.append((a, e)); i += 4 + n
    return out


IMAGE = _image_ranges()


def in_image(addr):
    return any(a <= addr <= e for a, e in IMAGE)


def name_of(addr):
    """nearest preceding symbol -- but only inside the game's own image.

    Naming an OS ROM address after the last game symbol produced
    "NAMEBUF+17009" for $C0E2, which is the OS frame counter. A confidently
    wrong label is worse than none.
    """
    if IMAGE and not in_image(addr):
        return "(outside the game image -- OS ROM or hardware)"
    prev = [s for s in BYADDR if s[0] <= addr and in_image(s[0])]
    if not prev:
        return ""
    a, n = prev[-1]
    return f"{n}+{addr-a}" if addr != a else n


TARGET = re.compile(r"\b(?:STA|STX|STY|INC|DEC|ASL|LSR|ROL|ROR)\s+"
                    r"\$([0-9A-Fa-f]{2,4})(,[XY])?\s*$")


def target_of(instr, x, y):
    """where the write landed, when the operand says so directly.

    Indirect forms like (ZP),Y cannot be resolved here: while the monitor has
    the CPU stopped the AI socket is not being served, so the pointer cannot
    be read. Those print as unresolved rather than guessed.
    """
    m = TARGET.search(instr.split(";")[0])
    if not m:
        return None
    base = int(m.group(1), 16)
    idx = m.group(2)
    if idx == ",X":
        return base + x
    if idx == ",Y":
        return base + y
    return base


def resolve(tok):
    tok = tok.strip()
    if tok in SYMS:
        return SYMS[tok]
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok, 0)


# --- what to watch ---------------------------------------------------------
# One RANGE, not a list of addresses. The monitor ANDs every condition in the
# table, so two "WRITE=addr" entries can never both be true and the breakpoint
# simply never fires -- which looks exactly like a clean run. A >= paired with
# a <= is satisfiable by a single write, so that is the way to cover a table.
# It also means only ONE range can be watched at a time.
if args.range:
    lo, hi = resolve(args.range[0]), resolve(args.range[1])
else:
    lo = SYMS.get("SHAPES", 0x6A7D)
    hi = SYMS.get("EXPLSHP", 0x6ADD) + 15
if hi < lo:
    sys.exit("range end is below its start")


def sock_cmd(c, timeout=10):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(timeout)
    s.connect(args.sock)
    d = json.dumps(c).encode(); s.sendall(str(len(d)).encode() + b"\n" + d)
    h = b""
    while not h.endswith(b"\n"):
        h += s.recv(1)
    n = int(h.strip()); b = b""
    while len(b) < n:
        b += s.recv(n - len(b))
    s.close(); return json.loads(b.decode())


if not os.path.exists(args.emulator):
    sys.exit(f"{args.emulator} not found -- pass --emulator")
try:
    os.unlink(args.sock)
except OSError:
    pass

env = dict(os.environ, DISPLAY=args.display)
cmdline = [args.emulator, "-ai", "-ai-socket", args.sock, args.machine,
           "-nosound", "-no-video-accel"] + args.emu_args + ["-run", args.program]
print("starting:", " ".join(cmdline))
emu = subprocess.Popen(cmdline, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                       stderr=subprocess.DEVNULL, bufsize=0, env=env)

for _ in range(60):
    if os.path.exists(args.sock):
        break
    if emu.poll() is not None:
        sys.exit("emulator exited during startup")
    time.sleep(0.25)
else:
    emu.kill(); sys.exit("emulator never created its AI socket")
time.sleep(1.0)

caps = sock_cmd({"cmd": "hello"})["build"]
if not caps.get("monitor_breakpoints"):
    emu.kill()
    sys.exit("this atari800 was built WITHOUT MONITOR_BREAKPOINTS -- rebuild "
             "with --enable-monitorbreakpoints; refusing to run a watcher that "
             "cannot watch")

# Confirm the machine is executing BEFORE arming anything. The debugger's
# "paused" flag is not the thing to test -- it reads True on a perfectly
# free-running emulator, it describes the monitor. And this must come first:
# once a breakpoint is armed and hits, the emulator sits in its monitor and
# stops answering the socket, so a liveness probe after arming just times out.
sock_cmd({"cmd": "debugger.continue"})
t0 = sock_cmd({"cmd": "peek", "addr": 0x14, "len": 1})["data"][0]
time.sleep(1.0)
t1 = sock_cmd({"cmd": "peek", "addr": 0x14, "len": 1})["data"][0]
if t0 == t1:
    emu.kill()
    sys.exit("the frame counter is not advancing -- the emulator is not "
             "executing, so this watcher could only ever report nothing")

sock_cmd({"cmd": "breakpoint.clear"})
a1 = sock_cmd({"cmd": "breakpoint.add", "condition_type": "WRITE",
               "operator": ">=", "value": lo})
a2 = sock_cmd({"cmd": "breakpoint.add", "condition_type": "WRITE",
               "operator": "<=", "value": hi})
if a1.get("status") != "ok" or a2.get("status") != "ok":
    emu.kill(); sys.exit(f"could not arm the range: {a1} {a2}")
if a2.get("size") != 2:
    emu.kill()
    sys.exit(f"expected exactly 2 conditions (a >= and a <=), got "
             f"{a2.get('size')}; more than that ANDs into something no single "
             f"write can satisfy and would never fire")
print(f"watching CPU writes to ${lo:04X}-${hi:04X}  "
      f"({name_of(lo)} .. {name_of(hi)})")

print("\nemulator running; play normally -- every write into that range "
      "prints below\n")

# --- the monitor speaks over stdout, and listens on stdin ------------------
BREAK = re.compile(r"PC=([0-9A-Fa-f]{4}):\s*(.*?)\s*$")
REGS = re.compile(r"A=([0-9A-Fa-f]{2}) X=([0-9A-Fa-f]{2}) Y=([0-9A-Fa-f]{2})")
hits = 0
buf = b""
try:
    while True:
        if emu.poll() is not None:
            print("emulator exited"); break
        r, _, _ = select.select([emu.stdout], [], [], 0.5)
        if not r:
            continue
        chunk = emu.stdout.read(4096)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", "replace").strip()
            m = BREAK.search(text)
            if not m:
                continue
            hits += 1
            pc = int(m.group(1), 16)
            instr = m.group(2)
            rg = REGS.search(text)
            print(f"[{time.strftime('%H:%M:%S')}] WRITE #{hits}")
            print(f"   instruction : ${pc:04X}  {instr}")
            print(f"   in routine  : {name_of(pc)}")
            in_game = in_image(pc) if IMAGE else True
            if rg:
                x, y = int(rg.group(2), 16), int(rg.group(3), 16)
                note = ""
                # only meaningful for the game's own code; the OS uses X and Y
                # for its own purposes and flagging those is just noise
                if in_game and x > 3:
                    note = "   <-- X > 3, out of range for a 4-actor array"
                print(f"   registers   : A=${rg.group(1)} X=${rg.group(2)} "
                      f"Y=${rg.group(3)}{note}")
                tgt = target_of(instr, x, y)
                if tgt is not None:
                    print(f"   wrote to    : ${tgt:04X}  {name_of(tgt)}")
                elif in_game:
                    print("   wrote to    : (indirect operand -- not resolvable "
                          "while the CPU is stopped)")
            sys.stdout.flush()
            # resume; the monitor is sitting at its prompt
            try:
                emu.stdin.write(b"cont\n"); emu.stdin.flush()
            except (BrokenPipeError, OSError):
                pass
except KeyboardInterrupt:
    print(f"\nstopping ({hits} writes caught)")
finally:
    try:
        emu.stdin.write(b"cont\n"); emu.stdin.flush()
    except Exception:
        pass
    emu.terminate()
    try:
        emu.wait(timeout=5)
    except Exception:
        emu.kill()
