"""How far a remote actor's picture trails the authoritative cell it is chasing.

LOCX/LOCY is what is drawn; NET_PX_X/Y is the last cell the server reported.
A constant offset is latency; spikes are stalls -- a snapshot that never
arrived, or a follow step that could not be taken. The two want different fixes,
so measure which one this is before designing either.
"""
import sys, time
sys.path.insert(0, sys.argv[1])
from ai import AI

a = AI(sys.argv[2])
SECS = float(sys.argv[3])
hist = {}
runs = {}          # consecutive samples at the same non-zero drift
worst = {}
samples = 0
t0 = time.time()
while time.time() - t0 < SECS:
    zp = a.peek(0x90, 0x30)
    px = a.peek(0x7C1A, 8)
    dead = a.peek(0x7C00, 1)[0]
    pid = a.peek(0x7A85, 1)[0]
    samples += 1
    for i in range(4):
        if dead & (1 << i) or i == pid:
            continue
        lx, ly = zp[0xA0 - 0x90 + i], zp[0xA4 - 0x90 + i]
        ax, ay = px[i], px[4 + i]
        d = abs(lx - ax) + abs(ly - ay)
        hist[d] = hist.get(d, 0) + 1
        if d:
            runs[i] = runs.get(i, 0) + 1
            worst[i] = max(worst.get(i, 0), runs[i])
        else:
            runs[i] = 0
tot = sum(hist.values())
print(f"samples {samples}; remote render-vs-authoritative drift, in cells:")
for d in sorted(hist):
    print(f"   {d} cell(s): {hist[d]:6d}  {100.0*hist[d]/tot:5.1f}%")
print("longest unbroken run behind, in samples:", worst)
print("diag snaps/maxdrift/src:",
      a.peek(0x7A89, 1)[0], a.peek(0x7A8A, 1)[0], a.peek(0x7A8C, 1)[0])
