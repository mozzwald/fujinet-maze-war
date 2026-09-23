"""Split remote-actor render-vs-authoritative gap by whether the remote is
currently moving or has been still for a while.

If the gap only ever appears while the remote is actively moving and always
closes to 0 once it stops (and stays stopped for long enough that the follow
has had many ticks to catch up), that is ordinary transit latency -- expected,
self-resolving, not a bug. If the gap persists nonzero for a long stretch after
the remote has genuinely stopped, that is a real client-side reconciliation
fault worth fixing.

"Still" is defined as: NET_PX_X/Y for the watched slot has not changed for more
than STILL_AFTER seconds. Every read in this script goes through AI.peek(),
which is internally rate-limited (see ai.py) to avoid freezing the emulator.
"""
import sys, time
sys.path.insert(0, sys.argv[1])
from ai import AI

a = AI(sys.argv[2])
SECS = float(sys.argv[3])
STILL_AFTER = 1.0  # seconds of no authoritative-position change

pid = a.peek(0x7A85, 1)[0]
dead = a.peek(0x7C00, 1)[0]
cands = [i for i in range(4) if i != pid and not (dead & (1 << i))]
if not cands:
    raise SystemExit("FAIL: no live remote slot to watch")

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

hist = {"moving": {}, "still": {}}
still_since_gap = {}  # after going still, track the largest gap seen and when
last_px = None
last_change = time.time()
t0 = time.time()
became_still_nonzero_reports = 0
while time.time() - t0 < SECS:
    px = a.peek(0x7C1A, 8)
    loc = a.peek(0xA0, 8)
    if a.peek(0x7C00, 1)[0] & (1 << r):
        raise SystemExit(f"FAIL: slot {r} went hidden mid-run; discard this run")
    now = time.time()
    p = (px[r], px[4 + r])
    if p != last_px:
        last_px = p
        last_change = now
    d = abs(loc[r] - px[r]) + abs(loc[4 + r] - px[4 + r])
    bucket = "still" if (now - last_change) > STILL_AFTER else "moving"
    hist[bucket][d] = hist[bucket].get(d, 0) + 1
    if bucket == "still" and d != 0 and (now - last_change) > STILL_AFTER + 2.0:
        became_still_nonzero_reports += 1

for b in ("moving", "still"):
    tot = sum(hist[b].values())
    print(f"{b}: {tot} samples" + (" (none)" if tot == 0 else ""))
    for d in sorted(hist[b]):
        print(f"    gap {d}: {hist[b][d]:6d}  {100.0*hist[b][d]/tot:5.1f}%")

if became_still_nonzero_reports:
    print(f"\nWARNING: {became_still_nonzero_reports} samples had a nonzero gap "
          f"more than {STILL_AFTER + 2.0:.1f}s after the target stopped moving "
          "-- that is not ordinary transit latency, it is a stuck reconcile.")
else:
    print("\nNo nonzero gap survived more than "
          f"{STILL_AFTER + 2.0:.1f}s of the target being still.")
