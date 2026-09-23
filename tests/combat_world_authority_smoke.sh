#!/bin/sh

set -eu

PORT=9112
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/combat-world-authority.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/combat-world-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ $status -ne 0 ]; then
    printf 'combat world authority smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

cat >"$BRICK_FILE" <<'EOF'
####################
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
#..................#
#..................#
#..................#
####################
EOF

"$SERVER_BIN" --port "$PORT" --tick-hz 4 --zombies 0 --brick "$BRICK_FILE" --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PY'
import collections


def cobs_decode(pkt):
    """Server frames are COBS encoded with a trailing $00 delimiter."""
    if not pkt or pkt[-1] != 0:
        return b""
    frame = pkt[:-1]
    out = bytearray()
    rd = 0
    while rd < len(frame):
        code = frame[rd]
        rd += 1
        if code == 0:
            return b""
        for _ in range(code - 1):
            if rd >= len(frame):
                return b""
            out.append(frame[rd])
            rd += 1
        if code != 0xFF and rd < len(frame):
            out.append(0)
    return bytes(out)

import socket
from tcp_frames import recv_frame, send_frame
import sys
import time

port = int(sys.argv[1])

PKT_SNAPSHOT = 0x40
PKT_SHOT = 0x42
PKT_BRICK_FULL = 0x50
PKT_BRICK_DELTA = 0x51
PKT_RESPAWN = 0x52

DIRS = {
    0: (1, 0, 0x07),
    1: (0, 1, 0x0D),
    2: (-1, 0, 0x0B),
    3: (0, -1, 0x0E),
}


def decode_bricks(packet):
    bits = packet[3:51]
    grid = [[1] * 20 for _ in range(19)]
    for y in range(19):
        for x in range(20):
            idx = y * 20 + x
            grid[y][x] = (bits[idx // 8] >> (idx % 8)) & 1
    return grid


class Client:
    peers = []
    def __init__(self, slot_hint):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.slot_hint = slot_hint
        self.last_joy = 0x0F
        self.last_tx = time.monotonic()
        self.peers.append(self)
        self.pid = None
        self.seq = 1
        self.players = {}
        self.bricks = None
        self.respawns = []
        self.shots = []
        self.brick_deltas = []

    def handle(self, packet):
        if len(packet) >= 51 and packet[0] == PKT_BRICK_FULL:
            self.bricks = decode_bricks(packet)
            return
        if len(packet) >= 20 and packet[0] == PKT_SNAPSHOT:
            self.pid = (packet[2] >> 1) & 0x03
            for idx in range(4):
                self.players[idx] = {
                    "x": packet[3 + idx * 2],
                    "y": packet[4 + idx * 2],
                    "score": packet[15 + idx],
                }
            return
        if len(packet) >= 6 and packet[0] == PKT_RESPAWN:
            self.respawns.append(bytes(packet[:6]))
            return
        if len(packet) >= 6 and packet[0] == PKT_SHOT:
            self.shots.append(bytes(packet[:6]))
            return
        if len(packet) >= 4 and packet[0] == PKT_BRICK_DELTA:
            self.brick_deltas.append(bytes(packet[:4]))

    def pump(self, duration=0.2):
        deadline = time.time() + duration
        while time.time() < deadline:
            # Preserve idle seats while another peer runs a long scenario.
            # Never refresh held movement/fire: stale-input checks still apply.
            for peer in self.peers:
                if (peer.pid is not None and peer.last_joy == 0x0F and
                        time.monotonic() - peer.last_tx >= 1):
                    peer.send_neutral()
            try:
                packet = cobs_decode(recv_frame(self.sock, 256))
            except socket.timeout:
                continue
            self.handle(packet)

    def wait_ready(self):
        deadline = time.time() + 5.0
        while time.time() < deadline:
            send_frame(self.sock, bytes([0x41, self.seq & 0xFF, self.slot_hint & 0xFF, 0x0F]))
            self.seq = (self.seq + 1) & 0xFF
            self.pump(0.02)
            if self.pid is not None and self.bricks is not None and len(self.players) == 4:
                return
        raise SystemExit("timed out waiting for initial state")

    def send_delta(self, joy):
        self.last_joy = joy
        self.last_tx = time.monotonic()
        packet = bytes([0x41, self.seq & 0xFF, self.pid & 0xFF, joy & 0xFF])
        self.seq = (self.seq + 1) & 0xFF
        send_frame(self.sock, packet)

    def send_neutral(self):
        self.send_delta(0x0F)

    def wait_snapshot_score(self, pid, score, timeout=12.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            if self.players.get(pid, {}).get("score") == score:
                return
        raise SystemExit(f"timed out waiting for pid {pid} score {score}")

    # Waits are generous on purpose. These assert ORDERING, not latency, and
    # the suite is often run alongside an emulator and a FujiNet sidecar on the
    # same box; a tight deadline turns machine load into a false failure.
    def wait_snapshot_pos(self, pid, pos, timeout=12.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            if self.players.get(pid) and (self.players[pid]["x"], self.players[pid]["y"]) == pos:
                return
        raise SystemExit(f"timed out waiting for pid {pid} at {pos}")

    def wait_snapshot_change(self, pid, start, timeout=12.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            if self.players.get(pid):
                pos = (self.players[pid]["x"], self.players[pid]["y"])
                if pos != start:
                    return pos
        raise SystemExit(f"timed out waiting for pid {pid} to change from {start}")

    def wait_respawn(self, pid, final, timeout=6.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            for packet in self.respawns:
                is_final = (packet[5] & 0x02) != 0
                if packet[2] == pid and is_final == final:
                    return packet
        kind = "final" if final else "pending"
        raise SystemExit(f"timed out waiting for {kind} respawn pid={pid}")

    def wait_brick_delta(self, pos, timeout=12.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            for packet in self.brick_deltas:
                if (packet[2], packet[3]) == pos:
                    return packet
        raise SystemExit(f"timed out waiting for brick delta {pos}")


def passable(bricks, pos, blocked):
    x, y = pos
    return 0 <= x < 20 and 0 <= y < 19 and bricks[y][x] == 0 and pos not in blocked


def bfs(bricks, start, goal, blocked):
    q = collections.deque([start])
    prev = {start: None}
    while q:
        cur = q.popleft()
        if cur == goal:
            path = []
            while prev[cur] is not None:
                path.append(cur)
                cur = prev[cur]
            path.reverse()
            return path
        for dir_id, (dx, dy, _joy) in DIRS.items():
            nxt = (cur[0] + dx, cur[1] + dy)
            if nxt in prev or not passable(bricks, nxt, blocked):
                continue
            prev[nxt] = cur
            q.append(nxt)
    return None


def joy_for_step(a, b):
    dx = b[0] - a[0]
    dy = b[1] - a[1]
    for dir_id, (ddx, ddy, joy) in DIRS.items():
        if (dx, dy) == (ddx, ddy):
            return joy
    raise SystemExit(f"no direction from {a} to {b}")



def occupied_now(client, *exclude):
    """Cells currently held by anyone except the listed pids.

    Read fresh each time a path is planned: positions captured once at startup
    go stale as soon as an actor moves, and a plan plotted through an occupied
    cell blocks forever.
    """
    client.pump(0.1)
    skip = set(exclude)
    out = set()
    for idx in range(4):
        if idx in skip or not client.players.get(idx):
            continue
        out.add((client.players[idx]["x"], client.players[idx]["y"]))
    return out

def walk(client, pid, path):
    current = (client.players[pid]["x"], client.players[pid]["y"])
    for step in path:
        joy = joy_for_step(current, step)
        client.send_delta(joy)
        actual = client.wait_snapshot_change(pid, current)
        if actual != step:
            raise SystemExit(f"expected pid {pid} step {step}, got {actual}")
        client.send_neutral()
        client.wait_snapshot_pos(pid, step)
        current = step


def choose_adjacent_target(bricks, anchor, start, blocked):
    best = None
    for dir_id, (dx, dy, _joy) in DIRS.items():
        target = (anchor[0] + dx, anchor[1] + dy)
        if not passable(bricks, target, blocked):
            continue
        path = bfs(bricks, start, target, blocked)
        if path is not None:
            candidate = (len(path), target, DIRS[dir_id][2], path)
            if best is None or candidate[0] < best[0]:
                best = candidate
    if best is None:
        raise SystemExit("no adjacent target scenario")
    _, target, joy, path = best
    return target, joy, path


def choose_line_shot_toward_target(bricks, shooter_start, target, blocked):
    tx, ty = target
    best = None

    for x in range(1, tx - 1):
        shooter = (x, ty)
        if not passable(bricks, shooter, blocked):
            continue
        if any(bricks[ty][mx] for mx in range(x + 1, tx)):
            continue
        shooter_path = bfs(bricks, shooter_start, shooter, blocked)
        if shooter_path is not None:
            candidate = (len(shooter_path) + (tx - x), shooter, DIRS[0][2], shooter_path)
            if best is None or candidate[0] < best[0]:
                best = candidate

    for x in range(tx + 2, 19):
        shooter = (x, ty)
        if not passable(bricks, shooter, blocked):
            continue
        if any(bricks[ty][mx] for mx in range(tx + 1, x)):
            continue
        shooter_path = bfs(bricks, shooter_start, shooter, blocked)
        if shooter_path is not None:
            candidate = (len(shooter_path) + (x - tx), shooter, DIRS[2][2], shooter_path)
            if best is None or candidate[0] < best[0]:
                best = candidate

    for y in range(1, ty - 1):
        shooter = (tx, y)
        if not passable(bricks, shooter, blocked):
            continue
        if any(bricks[my][tx] for my in range(y + 1, ty)):
            continue
        shooter_path = bfs(bricks, shooter_start, shooter, blocked)
        if shooter_path is not None:
            candidate = (len(shooter_path) + (ty - y), shooter, DIRS[1][2], shooter_path)
            if best is None or candidate[0] < best[0]:
                best = candidate

    for y in range(ty + 2, 18):
        shooter = (tx, y)
        if not passable(bricks, shooter, blocked):
            continue
        if any(bricks[my][tx] for my in range(ty + 1, y)):
            continue
        shooter_path = bfs(bricks, shooter_start, shooter, blocked)
        if shooter_path is not None:
            candidate = (len(shooter_path) + (y - ty), shooter, DIRS[3][2], shooter_path)
            if best is None or candidate[0] < best[0]:
                best = candidate

    if best is None:
        raise SystemExit("no moving-shot scenario")
    _, shooter, joy, shooter_path = best
    return shooter, joy, shooter_path


def choose_fire_into_brick(bricks, start, blocked):
    best = None
    for y in range(1, 18):
        for x in range(1, 19):
            origin = (x, y)
            if not passable(bricks, origin, blocked):
                continue
            for dir_id, (dx, dy, joy) in DIRS.items():
                wall = (x + dx, y + dy)
                if wall[0] in (0, 19) or wall[1] in (0, 18):
                    continue
                if not (0 <= wall[0] < 20 and 0 <= wall[1] < 19):
                    continue
                if bricks[wall[1]][wall[0]] != 1:
                    continue
                path = bfs(bricks, start, origin, blocked)
                if path is not None:
                    candidate = (len(path), origin, joy, wall, path)
                    if best is None or candidate[0] < best[0]:
                        best = candidate
    if best is None:
        raise SystemExit("no brick scenario")
    _, origin, joy, wall, path = best
    return origin, joy, wall, path


clients = [Client(0), Client(1)]
for expected_pid, client in enumerate(clients):
    # Complete the accepted connection handshake with neutral input.
    send_frame(client.sock, bytes([0x41, 1, expected_pid, 0x0F]))
    client.seq = 2
    client.wait_ready()

bricks = clients[0].bricks
slot0_pid = clients[0].pid
slot1_pid = clients[1].pid
join_pos = (clients[1].players[slot1_pid]["x"],
            clients[1].players[slot1_pid]["y"])
clients[0].wait_snapshot_pos(slot1_pid, join_pos)
slot0_pos = (clients[0].players[slot0_pid]["x"], clients[0].players[slot0_pid]["y"])
slot1_pos = (clients[0].players[slot1_pid]["x"], clients[0].players[slot1_pid]["y"])
other_slots = {
    (clients[0].players[idx]["x"], clients[0].players[idx]["y"])
    for idx in range(4)
    if idx not in (slot0_pid, slot1_pid)
}
# Drain the bounded direct/reliable/echo join publications first. Join-time
# final RESPAWN is session baseline, not the post-death final spawn this combat
# scenario waits for below.
for client in clients:
    client.pump(1.0)
for client in clients:
    client.respawns = []

# Immediate adjacent hit -> score + pending + final respawn
target, fire_joy, path = choose_adjacent_target(
    bricks, slot0_pos, slot1_pos, occupied_now(clients[0], slot1_pid)
)
walk(clients[1], slot1_pid, path)
slot1_pos = target
clients[0].send_delta(fire_joy | 0x10)
clients[0].wait_respawn(slot1_pid, False, timeout=8.0)
clients[0].pump(0.4)
clients[0].send_neutral()
clients[0].wait_snapshot_score(slot0_pid, 1)
final_packet = clients[0].wait_respawn(slot1_pid, True, timeout=7.0)
final_pos = (final_packet[3], final_packet[4])
clients[0].wait_snapshot_pos(slot1_pid, final_pos, timeout=12.0)
clients[1].wait_snapshot_pos(slot1_pid, final_pos, timeout=12.0)
for client in clients:
    client.respawns = []
    client.brick_deltas = []
    client.shots = []

# Moving-shot hit -> second score and pending respawn
slot0_pos = (clients[0].players[slot0_pid]["x"], clients[0].players[slot0_pid]["y"])
slot1_pos = final_pos
shooter, fire_joy, shooter_path = choose_line_shot_toward_target(
    bricks, slot0_pos, slot1_pos, occupied_now(clients[0], slot0_pid)
)
walk(clients[0], slot0_pid, shooter_path)
slot0_pos = shooter
clients[0].send_delta(fire_joy | 0x10)
clients[0].wait_respawn(slot1_pid, False, timeout=8.0)
clients[0].pump(0.4)
clients[0].send_neutral()
clients[0].pump(2.0)
for client in clients:
    client.brick_deltas = []

# Brick mutation -> authoritative BRICK_DELTA
slot0_pos = (clients[0].players[slot0_pid]["x"], clients[0].players[slot0_pid]["y"])
origin, brick_joy, wall, path = choose_fire_into_brick(
    bricks, slot0_pos, occupied_now(clients[0], slot0_pid)
)
walk(clients[0], slot0_pid, path)
clients[0].send_delta(brick_joy | 0x10)
clients[0].pump(1.0)
clients[0].send_neutral()

for client in clients:
    client.sock.close()
PY

kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=

grep -E "combat event kind=immediate-hit slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "combat event kind=moving-hit slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "combat event kind=brick-break slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "TX respawn pid=[0-3] x=" "$LOG_FILE" >/dev/null
