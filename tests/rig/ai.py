"""Minimal client for the atari800 AI socket, plus the helpers the rig needs.

CRITICAL: the emulator does NOT free-run on its own thread. Its main loop only
advances a frame between AI-socket commands, so a back-to-back burst of
commands with truly zero gap between them starves it completely -- verified
directly: 1844 peeks in 2 real seconds with zero sleep between them advanced
the CPU's PC/A/X/Y/SP by exactly zero. Even a 5ms sleep between commands is
enough to let it keep pace with real time (measured at ~58 Hz against RTCLOK,
matching NTSC 60Hz within measurement noise); the gap doesn't need to be much
bigger than that. `cmd()` enforces a floor below so every script using this
class is protected without having to remember it per call-site.

This explains the contradictory measurements from the previous session: a
tight peek-only sampling loop was intermittently pausing the emulated Atari
while the real server, real relay and real bot kept running in wall-clock
time, so the observed "drift" was an artifact of the instrument, not the
client under test.
"""
import json, socket, time

GAMESCR, W = 0x73C0, 40
PL0 = 0x3C00
MIN_CMD_INTERVAL = 0.008  # seconds; empirically safe margin above the 5ms floor


class AI:
    def __init__(self, sock):
        self.sock = sock
        self._last_cmd_at = 0.0

    def cmd(self, c):
        wait = MIN_CMD_INTERVAL - (time.time() - self._last_cmd_at)
        if wait > 0:
            time.sleep(wait)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.sock)
        d = json.dumps(c).encode()
        s.sendall(str(len(d)).encode() + b"\n" + d)
        h = b""
        while not h.endswith(b"\n"):
            h += s.recv(1)
        n = int(h.strip())
        b = b""
        while len(b) < n:
            b += s.recv(n - len(b))
        s.close()
        self._last_cmd_at = time.time()
        return json.loads(b)

    def peek(self, addr, n):
        out = []
        while n:
            k = min(n, 256)
            out += self.cmd({"cmd": "peek", "addr": addr, "len": k})["data"]
            addr += k
            n -= k
        return out

    def poke(self, addr, vals):
        self.cmd({"cmd": "poke", "addr": addr, "data": list(vals)})

    def run(self, frames):
        # The "run" command itself blocks synchronously until the frames have
        # actually executed (verified: run(60) took 1.001 real seconds), so no
        # extra sleep is needed here -- an earlier version added one anyway,
        # silently doubling every boot() wait.
        self.cmd({"cmd": "run", "frames": frames})

    def stick(self, d):
        self.cmd({"cmd": "joystick", "port": 0, "direction": d, "fire": False})

    def boot(self, host, name):
        """Through the host prompt: no '.' key and backspace is ignored."""
        self.run(240)
        self.poke(0x7E55, list(host.encode()) + [0])
        self.poke(0x7E75, list(name.encode()) + [0] * (9 - len(name)))
        for _ in range(2):
            self.cmd({"cmd": "key", "code": 0x0C})   # RETURN, as a keycode
            time.sleep(0.25)
            self.cmd({"cmd": "key_release"})
            time.sleep(0.25)
        self.run(300)

    def state(self):
        zp = self.peek(0x90, 0x30)
        m = self.peek(0x7C00, 2)
        g = lambda base, i: zp[base - 0x90 + i]
        return {"dead": m[0], "erase": m[1],
                "dir": [g(0x94, i) for i in range(4)],
                "locx": [g(0xA0, i) for i in range(4)],
                "locy": [g(0xA4, i) for i in range(4)],
                "rndx": [g(0xA8, i) for i in range(4)],
                "rndy": [g(0xAC, i) for i in range(4)],
                "movest": [g(0xB4, i) for i in range(4)],
                "actflag": [g(0x90, i) for i in range(4)],
                "pid": self.peek(0x7A85, 1)[0]}

    def cell(self, x, y):
        row = self.peek(GAMESCR + y * W + x * 2, 2)
        return tuple(row)

    def pm(self, slot):
        return self.peek(PL0 + slot * 0x100, 256)
