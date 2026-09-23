"""Gap between a remote actor's rendered cell and its authoritative cell.

Picks the slot to watch rather than assuming one. An unoccupied slot is hidden
and parked on its placeholder cell while the server still holds a spawn position
for it, so measuring one yields a large meaningless constant -- which is exactly
what earlier runs of this script were reporting.
"""
import sys, time
sys.path.insert(0, sys.argv[1])
from ai import AI

a = AI(sys.argv[2])
SECS = float(sys.argv[3])

pid = a.peek(0x7A85, 1)[0]
dead = a.peek(0x7C00, 1)[0]
cands = [i for i in range(4) if i != pid and not (dead & (1 << i))]
if not cands:
    raise SystemExit("FAIL: no live remote slot to watch")

# of the live remotes, take the one whose authoritative cell actually moves
moved = {i: 0 for i in cands}
seen = {}
t0 = time.time()
while time.time() - t0 < 3.0:
    px = a.peek(0x7C1A, 8)
    for i in cands:
        p = (px[i], px[4 + i])
        if seen.get(i) not in (None, p):
            moved[i] += 1
        seen[i] = p
r = max(cands, key=lambda i: moved[i])
if moved[r] == 0:
    raise SystemExit(f"FAIL: no remote slot is moving (live slots {cands}); "
                     "the measurement would be meaningless")
print(f"our pid {pid}; live remotes {cands}; watching slot {r} "
      f"({moved[r]} authoritative moves in the 3s probe)")

hist = {}
t0 = time.time()
while time.time() - t0 < SECS:
    px = a.peek(0x7C1A, 8)
    loc = a.peek(0xA0, 8)
    d = abs(loc[r] - px[r]) + abs(loc[4 + r] - px[4 + r])
    hist[d] = hist.get(d, 0) + 1
    if a.peek(0x7C00, 1)[0] & (1 << r):
        raise SystemExit(f"FAIL: slot {r} went hidden mid-run; discard this run")
tot = sum(hist.values())
print(f"{tot} samples over {SECS:.0f}s:")
for d in sorted(hist):
    print(f"   gap {d}: {hist[d]:6d}  {100.0*hist[d]/tot:5.1f}%")
