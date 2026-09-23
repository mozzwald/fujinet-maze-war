"""UDP relay that gives loopback a real link's delay and loss.

The emulator rig cannot reproduce the reported lag because loopback delivers
every packet instantly and never drops one. This sits between FujiNet-PC and the
game server and adds one-way delay and loss in each direction independently, so
the rendered remote actor can be measured against the authoritative one under
conditions the hardware actually sees.

  link.py <listen-addr> <listen-port> <upstream-port> <delay-ms> <drop-fraction> [control-file]

Bind to a loopback alias, not 0.0.0.0: FujiNet-PC binds its own netstream socket
to the destination port with SO_REUSEADDR, so sharing port 9000 on the same
address makes delivery order-dependent.

IMPORTANT: never kill and restart this process while the Atari is connected
through it, even briefly, to change delay/loss. Doing so was found to trip the
Atari-side netstream handshake into a permanently inactive state (confirmed via
netsio_status: netstream.active flips false, sync.timeouts increments) that
does not recover without a full cold reset and reboot of the emulated machine
-- the game-level UDP path going quiet for even ~1-2 round trips while nothing
is listening on the bound address appears to be enough to trip it. The 0.03
"unavailable" case is not something a real link ever truly does (a real
listener is always there even if a given packet doesn't make it), so the
disruption is a rig artifact, not a discovery about the client.

To change delay/loss without restarting, write "DELAY_MS DROP_FRACTION" (space
separated) to the control file (6th argument, default "link_control.txt" next
to this script) and this process will pick it up on its next loop iteration.
"""
import heapq, os, random, select, socket, sys, time

ADDR, PORT, UP = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
DELAY = float(sys.argv[4]) / 1000.0
DROP = float(sys.argv[5])
CONTROL = sys.argv[6] if len(sys.argv) > 6 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "link_control.txt")
_control_mtime = 0.0


def check_control():
    global DELAY, DROP, _control_mtime
    try:
        mtime = os.path.getmtime(CONTROL)
    except OSError:
        return
    if mtime == _control_mtime:
        return
    _control_mtime = mtime
    try:
        with open(CONTROL) as f:
            parts = f.read().split()
        new_delay = float(parts[0]) / 1000.0
        new_drop = float(parts[1])
    except (OSError, IndexError, ValueError):
        return
    if (new_delay, new_drop) != (DELAY, DROP):
        DELAY, DROP = new_delay, new_drop
        print(f"control: delay={DELAY*1000:.0f}ms drop={DROP}", flush=True)


front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
front.bind((ADDR, PORT))
peers, back = {}, {}
queue = []                      # (due, direction, payload, key)
stats = {"c2s": 0, "s2c": 0, "dropped": 0}
print(f"link up: {ADDR}:{PORT} -> 127.0.0.1:{UP}  delay={DELAY*1000:.0f}ms "
      f"drop={DROP}  control={CONTROL}", flush=True)

while True:
    check_control()
    timeout = 0.002 if queue else 0.2
    r, _, _ = select.select([front] + list(back), [], [], timeout)
    now = time.time()
    for s in r:
        if s is front:
            data, addr = front.recvfrom(2048)
            if addr not in peers:
                u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                u.connect(("127.0.0.1", UP))
                peers[addr], back[u] = u, addr
                print("client", addr, flush=True)
            stats["c2s"] += 1
            if random.random() >= DROP:
                heapq.heappush(queue, (now + DELAY, "up", data, addr))
            else:
                stats["dropped"] += 1
        else:
            data = s.recv(2048)
            stats["s2c"] += 1
            if random.random() >= DROP:
                heapq.heappush(queue, (now + DELAY, "down", data, back[s]))
            else:
                stats["dropped"] += 1
    now = time.time()
    while queue and queue[0][0] <= now:
        _, direction, data, key = heapq.heappop(queue)
        if direction == "up":
            peers[key].send(data)
        else:
            front.sendto(data, key)
