"""A remote player that never stops moving: walks a rectangle, using its own
authoritative position to turn, so it never stalls against a wall.

Continuous motion is the case the follow lag claim rests on. A bot that walks
into a wall and holds the stick looks stationary to the server, which is not
the same thing at all.
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

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9500
STICK = {"r": 0x07, "d": 0x0D, "l": 0x0B, "u": 0x0E}
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("127.0.0.1", PORT)); s.settimeout(0.02)
seq = 0; pid = None; pos = None

def send(joy):
    global seq
    s.send(bytes([0x41, seq, pid or 0, joy])); seq = (seq + 1) & 0xFF

def pump(secs):
    global pid, pos
    end = time.time() + secs
    while time.time() < end:
        try: p = cobs_decode(s.recv(256))
        except socket.timeout: continue
        if len(p) >= 20 and p[0] == 0x40:
            pid = (p[2] >> 1) & 0x03
            pos = (p[3 + pid * 2], p[4 + pid * 2])

send(0x0F); pump(1.5)
print("patrol pid", pid, "at", pos, flush=True)
box = [(4, 4), (14, 4), (14, 14), (4, 14)]
corner = 0
moved = 0
stuck = 0
while True:
    tx, ty = box[corner]
    if pos is None:
        pump(0.1); continue
    x, y = pos
    if (x, y) == (tx, ty):
        corner = (corner + 1) % 4
        continue
    if x != tx:
        send(STICK["r"] if tx > x else STICK["l"])
    else:
        send(STICK["d"] if ty > y else STICK["u"])
    before = pos
    pump(0.09)
    if pos != before:
        moved += 1
        stuck = 0
        if moved % 60 == 0:
            print("moved", moved, "now", pos, flush=True)
    else:
        # blocked, most likely by the other actor standing in the lane: give up
        # on this corner rather than hold the stick against it, which would look
        # stationary to the server and make the measurement meaningless
        stuck += 1
        if stuck > 12:
            corner = (corner + 1) % 4
            stuck = 0
