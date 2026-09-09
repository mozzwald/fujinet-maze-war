"""UDP relay that gives loopback a real link's delay and loss.

The emulator rig cannot reproduce the reported lag because loopback delivers
every packet instantly and never drops one. This sits between FujiNet-PC and the
game server and adds one-way delay and loss in each direction independently, so
the rendered remote actor can be measured against the authoritative one under
conditions the hardware actually sees.

  link.py <listen-addr> <listen-port> <upstream-port> <delay-ms> <drop-fraction>

Bind to a loopback alias, not 0.0.0.0: FujiNet-PC binds its own netstream socket
to the destination port with SO_REUSEADDR, so sharing port 9000 on the same
address makes delivery order-dependent.
"""
import heapq, random, select, socket, sys, time

ADDR, PORT, UP = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
DELAY = float(sys.argv[4]) / 1000.0
DROP = float(sys.argv[5])

front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
front.bind((ADDR, PORT))
peers, back = {}, {}
queue = []                      # (due, direction, payload, key)
stats = {"c2s": 0, "s2c": 0, "dropped": 0}

while True:
    timeout = 0.002 if queue else 0.5
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
