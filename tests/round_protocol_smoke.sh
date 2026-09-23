#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
PORT=9141
LOG_FILE=$(mktemp /tmp/round-protocol.XXXXXX.log)
BRICK_FILE=$(mktemp /tmp/round-map.XXXXXX.txt)
SERVER_PID=

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$BRICK_FILE"
}
trap cleanup EXIT INT TERM

cat >"$BRICK_FILE" <<'EOF'
####################
#..................#
#..................#
#..................#
#....#.............#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
#..................#
####################
EOF

make -C "$ROOT_DIR" build/maze-war-server >/dev/null
"$SERVER_BIN" --port "$PORT" --tick-hz 100 --zombies 0 \
    --kill-limit 1 --intermission-ms 300 --brick "$BRICK_FILE" --debug \
    >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 0.2

PYTHONPATH="$ROOT_DIR/tests" python3 - "$PORT" <<'PYEOF'
from collections import deque
import socket
import sys
import time
from tcp_frames import decode_frame, encode_frame

port = int(sys.argv[1])

# Largest v1 frame: RELIABLE_EVENT header plus a tagged BRICK_FULL, CRC and
# delimiter. This must fit the Atari client's fixed 60-byte wire buffer.
largest_reliable = bytes((0x53, 0, 1, 0, 0x50)) + bytes(51)
assert len(largest_reliable) == 56
assert len(encode_frame(largest_reliable)) <= 60


class Client:
    def __init__(self, name):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.sock.settimeout(0.2)
        self.raw = bytearray()
        self.seq = 1
        self.rev = 0
        self.round = None
        self.phase = None
        self.pid = None
        self.players = None
        self.snapshots = {}
        self.starts = set()
        self.maps = {}
        self.matches = {}
        self.send(bytes((0x46, 1)))
        self.wait(lambda: self.round is not None)
        if self.phase == 0:
            self.wait_ready(self.round)
        else:
            self.wait(lambda: self.round in self.matches)
        self.send_name(name)

    def send(self, payload):
        self.sock.sendall(encode_frame(payload))

    def wire_packet(self, timeout=0.2):
        end = time.monotonic() + timeout
        while True:
            cut = self.raw.find(0)
            if cut >= 0:
                frame = bytes(self.raw[:cut + 1])
                del self.raw[:cut + 1]
                return decode_frame(frame)
            left = end - time.monotonic()
            if left <= 0:
                raise socket.timeout
            self.sock.settimeout(left)
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("server closed")
            self.raw.extend(data)

    def handle(self, packet):
        if packet[0] == 0x47:
            assert len(packet) == 5 and packet[1] == 1
            self.round, self.phase = packet[2], packet[3]
            return
        if packet[0] == 0x53:
            rev = packet[2] | packet[3] << 8
            if rev == self.rev + 1:
                self.rev = rev
                self.handle_inner(packet[4:])
            assert rev <= self.rev
            self.send(bytes((0x45, self.seq & 0xff,
                             self.rev & 0xff, self.rev >> 8)))
            self.seq += 1
            return
        if packet[0] == 0x40 and len(packet) == 21:
            rid = packet[20]
            self.pid = (packet[2] >> 1) & 3
            self.players = [(packet[3 + i * 2], packet[4 + i * 2],
                             packet[15 + i]) for i in range(4)]
            self.snapshots[rid] = packet

    def handle_inner(self, inner):
        if inner[0] == 0x55:
            assert len(inner) == 3 and 1 <= inner[2] <= 10
            self.round, self.phase = inner[1], 0
            self.starts.add(inner[1])
        elif inner[0] == 0x50:
            assert len(inner) == 52
            self.maps[inner[51]] = inner
        elif inner[0] == 0x54:
            assert len(inner) == 43
            self.matches[inner[1]] = inner
            if inner[1] == self.round:
                self.phase = 1

    def pump(self, timeout=0.2):
        try:
            self.handle(self.wire_packet(timeout))
            return True
        except socket.timeout:
            return False

    def wait(self, predicate, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return
            self.pump(min(0.2, end - time.monotonic()))
        raise AssertionError("timed out waiting for protocol state")

    def wait_ready(self, rid):
        self.wait(lambda: rid in self.starts and rid in self.maps and
                          rid in self.snapshots)

    def send_name(self, name):
        data = name.encode("ascii")[:8].ljust(8, b" ")
        self.send(bytes((0x43, self.seq & 0xff, self.pid or 0)) + data)
        self.seq += 1

    def delta(self, joy, rid=None):
        seq = self.seq & 0xff
        self.send(bytes((0x41, seq, self.pid or 0, joy,
                         self.round if rid is None else rid)))
        self.seq += 1
        return seq

    def wait_ack(self, seq):
        def acked():
            p = self.snapshots.get(self.round)
            return p is not None and (p[2] & 0x80) and p[19] == seq
        self.wait(acked)

    def close(self):
        self.sock.close()


def cell_open(x, y):
    return 1 <= x <= 18 and 1 <= y <= 17 and (x, y) != (5, 4)


def path(start, goal, blocked):
    q = deque([start])
    prev = {start: None}
    for_pop = ((1, 0, 0x07), (0, 1, 0x0d),
               (-1, 0, 0x0b), (0, -1, 0x0e))
    while q:
        here = q.popleft()
        if here == goal:
            out = []
            while prev[here] is not None:
                before, joy = prev[here]
                out.append(joy)
                here = before
            return list(reversed(out))
        for dx, dy, joy in for_pop:
            nxt = (here[0] + dx, here[1] + dy)
            if nxt not in prev and nxt != blocked and cell_open(*nxt):
                prev[nxt] = (here, joy)
                q.append(nxt)
    raise AssertionError("no route to adjacent firing cell")


def make_adjacent(shooter, observer):
    shooter.wait(lambda: shooter.players is not None)
    victim = shooter.players[1][:2]
    start = shooter.players[0][:2]
    choices = [((victim[0] - 1, victim[1]), 0x17),
               ((victim[0] + 1, victim[1]), 0x1b),
               ((victim[0], victim[1] - 1), 0x1d),
               ((victim[0], victim[1] + 1), 0x1e)]
    for goal, fire in choices:
        if not cell_open(*goal):
            continue
        try:
            moves = path(start, goal, victim)
        except AssertionError:
            continue
        for joy in moves:
            seq = shooter.delta(joy)
            shooter.wait_ack(seq)
            observer.pump(0.001)
        assert shooter.players[0][:2] == goal
        return fire
    raise AssertionError("no reachable firing cell")


# A replaced FujiNet-PC host socket may see a queued NAME before the Atari's
# retried HELLO. That frame must neither mutate state nor provoke the old
# close/reconnect loop; the same socket must still be able to handshake.
recover = socket.create_connection(("127.0.0.1", port), timeout=2)
recover.sendall(encode_frame(bytes((0x43, 1, 0)) + b"EARLY   "))
recover.settimeout(0.1)
try:
    assert recover.recv(64) == b"", "server sent gameplay before HELLO"
    raise AssertionError("server closed on recoverable pre-HELLO traffic")
except socket.timeout:
    pass
recover.sendall(encode_frame(bytes((0x46, 1))))
raw = bytearray()
while 0 not in raw:
    raw.extend(recover.recv(64))
welcome = decode_frame(bytes(raw[:raw.index(0) + 1]))
assert welcome[0] == 0x47 and welcome[1] == 1
recover.close()

# Incompatible clients fail before occupying a gameplay seat.
bad = socket.create_connection(("127.0.0.1", port), timeout=2)
bad.sendall(encode_frame(bytes((0x46, 2))))
raw = bytearray()
while 0 not in raw:
    raw.extend(bad.recv(64))
assert decode_frame(bytes(raw[:raw.index(0) + 1]))[0] == 0x48
bad.close()

a = Client("ALPHA")
b = Client("BRAVO")
assert a.pid == 0 and b.pid == 1

# An ACK beyond the sent watermark must not discard the next real event.
a.send(bytes((0x45, a.seq & 0xff, 0xff, 0xff)))
a.seq += 1

for number in range(10):
    rid = a.round
    assert b.round == rid
    if number == 0:
        # Mutate one canonical brick; the reset baseline must restore it.
        a.send(bytes((0x51, a.seq & 0xff, 5, 4, rid)))
        a.seq += 1
    fire = make_adjacent(a, b)
    a.delta(fire)
    a.wait(lambda: rid in a.matches)
    b.wait(lambda: rid in b.matches)
    ma, mb = a.matches[rid], b.matches[rid]
    assert ma == mb
    assert ma[2] == 0 and ma[3] == 0x03 and ma[4] == 0
    assert ma[5] == 1 and ma[6:10] == bytes((1, 0, 0, 0))
    assert ma[11:19] == b"ALPHA   " and ma[19:27] == b"BRAVO   "

    if number == 0:
        late = Client.__new__(Client)
        Client.__init__(late, "LATE")
        # The constructor sees WELCOME phase=1 and the frozen MATCH_END;
        # it cannot become gameplay-ready until the reset barrier.
        assert late.phase == 1 and rid in late.matches
        assert late.matches[rid] == ma
        late.close()

    next_rid = (rid + 1) & 0xff
    a.wait_ready(next_rid)
    b.wait_ready(next_rid)
    assert a.round == next_rid and b.round == next_rid
    assert a.snapshots[next_rid][15:19] == b"\0\0\0\0"
    full = a.maps[next_rid]
    idx = 4 * 20 + 5
    assert full[3 + idx // 8] & (1 << (idx % 8))
    if number == 0:
        # Delayed old-round input is consumed as transport but never applied.
        a.delta(0x07, rid=rid)

a.close()
b.close()
print("ten authoritative rounds, late join, reset barrier and stale input passed")
PYEOF

grep -F "HELLO rejected" "$LOG_FILE" >/dev/null
grep -F "round=11 started kill_limit=1" "$LOG_FILE" >/dev/null
grep -F "DROP DELTA slot=0 bad-len=5" "$LOG_FILE" >/dev/null

echo "round protocol smoke passed"
