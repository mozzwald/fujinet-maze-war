"""A scripted remote player: joins, hunts a target slot, fires point blank.

Something has to be moving on the other end for a lag measurement to mean
anything. On an open map the hunt is straight lines only, so this needs no
pathfinding: close the row, then the column, then fire.
"""
import socket, sys, time

def cobs_decode(pkt):
    if not pkt or pkt[-1] != 0: return b""
    frame = pkt[:-1]; out = bytearray(); rd = 0
    while rd < len(frame):
        code = frame[rd]; rd += 1
        if code == 0: return b""
        for _ in range(code - 1):
            if rd >= len(frame): return b""
            out.append(frame[rd]); rd += 1
        if code != 0xFF and rd < len(frame): out.append(0)
    return bytes(out)

PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9500
TARGET = int(sys.argv[1]) if len(sys.argv) > 1 else 0
R, D, L, U, N = 0x07, 0x0D, 0x0B, 0x0E, 0x0F

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("127.0.0.1", PORT)); s.settimeout(0.02)
seq = 0; pid = None; pos = {}

def send(joy):
    global seq
    s.send(bytes([0x41, seq, pid if pid is not None else 0, joy]))
    seq = (seq + 1) & 0xFF

def pump(secs):
    global pid
    end = time.time() + secs
    while time.time() < end:
        try: p = cobs_decode(s.recv(256))
        except socket.timeout: continue
        if len(p) >= 20 and p[0] == 0x40:
            pid = (p[2] >> 1) & 0x03
            for i in range(4): pos[i] = (p[3+i*2], p[4+i*2])

send(N); pump(1.0)
print("assassin pid", pid, "target", TARGET, flush=True)
kills = 0
while True:
    pump(0.12)
    if pid is None or TARGET not in pos or pid not in pos: continue
    me = pos[pid]; t = pos[TARGET]
    if t == (255, 255) or me == (255, 255):
        send(N); continue
    dx = t[0] - me[0]; dy = t[1] - me[1]
    if dy != 0 and abs(dy) > 0 and dx == 0:
        send((D if dy > 0 else U) | 0x10)   # lined up in a column: walk+fire
    elif dy != 0:
        send(D if dy > 0 else U)
    elif abs(dx) > 1:
        send(R if dx > 0 else L)
    else:
        send((R if dx > 0 else L) | 0x10)   # adjacent: fire
