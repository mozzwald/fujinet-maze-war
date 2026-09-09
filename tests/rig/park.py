"""Walk the local Atari actor to a fixed corner and leave it there.

Runs are only comparable if the geometry is. A stationary player parked in a
random spawn can sit in the patrol lane on one run and not the next, which
changes what the remote actor does and therefore what is being measured.
"""
import sys, time
sys.path.insert(0, sys.argv[1])
from ai import AI

a = AI(sys.argv[2])
TX, TY = int(sys.argv[3]), int(sys.argv[4])
end = time.time() + 30
while time.time() < end:
    st = a.state()
    p = st["pid"]
    x, y = st["locx"][p], st["locy"][p]
    if (x, y) == (TX, TY):
        break
    a.stick("right" if TX > x else "left") if x != TX else \
        a.stick("down" if TY > y else "up")
    time.sleep(0.12)
a.stick("center")
time.sleep(0.5)
st = a.state()
print("parked at", (st["locx"][st["pid"]], st["locy"][st["pid"]]), "target", (TX, TY))
