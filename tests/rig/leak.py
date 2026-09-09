"""Count stale painted cells: characters on the map no visible actor accounts for.

The server runs an open map, so every interior character must belong to an
actor. Reports how long each stale cell persists, which separates a transient
(a shot in flight, an animation frame) from residue that never goes away.
"""
import sys, time
sys.path.insert(0, sys.argv[1])
from ai import AI

a = AI(sys.argv[2])
SECS = float(sys.argv[3])
first_seen = {}
persistent = {}
samples = 0
t0 = time.time()
while time.time() - t0 < SECS:
    st = a.state()
    scr = a.peek(0x73C0, 760)
    samples += 1
    allowed = set()
    for i in range(4):
        if st["dead"] & (1 << i):
            continue
        x, y = st["rndx"][i], st["rndy"][i]
        allowed.add((x, y))
        d = st["dir"][i]
        # the walk image spans two cells whichever phase it is in
        allowed |= ({(x, y - 1), (x, y + 1)} if d & 1 else {(x - 1, y), (x + 1, y)})
    painted = set()
    for row in range(19):
        for col in range(40):
            if scr[row * 40 + col] and not (col // 2 in (0, 19) or row in (0, 18)):
                painted.add((col // 2, row))
    now = time.time()
    stale = painted - allowed
    for c in stale:
        first_seen.setdefault(c, now)
        age = now - first_seen[c]
        if age > 2.0:
            persistent[c] = max(persistent.get(c, 0), age)
    for c in list(first_seen):
        if c not in stale:
            del first_seen[c]
print(f"samples {samples}; cells stale for more than 2s: {len(persistent)}")
for c, age in sorted(persistent.items(), key=lambda kv: -kv[1])[:10]:
    print(f"   {c} persisted {age:.1f}s")
