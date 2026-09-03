#!/bin/sh

set -eu

PORT=9111
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/combat-ordering-smoke.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/combat-ordering-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ $status -ne 0 ]; then
    printf 'combat ordering smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
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
import socket
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
    def __init__(self, slot_hint):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.slot_hint = slot_hint
        self.pid = None
        self.seq = 1
        self.players = {}
        self.bricks = None
        self.shots = []
        self.respawns = []
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
                    "joy": packet[11 + idx],
                    "score": packet[15 + idx],
                }
            return
        if len(packet) >= 6 and packet[0] == PKT_SHOT:
            self.shots.append(bytes(packet[:6]))
            return
        if len(packet) >= 6 and packet[0] == PKT_RESPAWN:
            self.respawns.append(bytes(packet[:6]))
            return
        if len(packet) >= 4 and packet[0] == PKT_BRICK_DELTA:
            self.brick_deltas.append(bytes(packet[:4]))

    def pump(self, duration=0.2):
        deadline = time.time() + duration
        while time.time() < deadline:
            try:
                packet = self.sock.recv(256)
            except socket.timeout:
                continue
            self.handle(packet)

    def wait_ready(self):
        deadline = time.time() + 5.0
        while time.time() < deadline:
            self.sock.send(bytes([0x41, self.seq & 0xFF, self.slot_hint & 0xFF, 0x0F]))
            self.seq = (self.seq + 1) & 0xFF
            self.pump(0.02)
            if self.pid is not None and self.bricks is not None and len(self.players) == 4:
                return
        raise SystemExit("timed out waiting for initial state")

    def send_delta(self, joy):
        if self.pid is None:
            raise SystemExit("pid unknown")
        packet = bytes([0x41, self.seq & 0xFF, self.pid & 0xFF, joy & 0xFF])
        self.seq = (self.seq + 1) & 0xFF
        self.sock.send(packet)

    def send_neutral(self):
        self.send_delta(0x0F)

    def wait_snapshot_pos(self, pid, pos, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            player = self.players.get(pid)
            if player and (player["x"], player["y"]) == pos:
                return
        raise SystemExit(f"timed out waiting for pid {pid} at {pos}")

    def hold_until_change(self, pid, start, joy, timeout=5.0):
        """Hold a direction until the move lands.

        The server samples one joy value per tick, so a direction sent once can
        be overwritten by the neutral from the previous step before the tick
        reads it. Re-sending while waiting is what holding the stick does, and
        it makes the walk independent of tick alignment.
        """
        deadline = time.time() + timeout
        next_send = 0.0
        while time.time() < deadline:
            if time.time() >= next_send:
                self.send_delta(joy)
                next_send = time.time() + 0.15
            self.pump(0.02)
            if self.players.get(pid):
                pos = (self.players[pid]["x"], self.players[pid]["y"])
                if pos != start:
                    return pos
        raise SystemExit(f"timed out holding {joy:#04x} for pid {pid} from {start}")

    def wait_snapshot_change(self, pid, start, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            player = self.players.get(pid)
            if player:
                pos = (player["x"], player["y"])
                if pos != start:
                    return pos
        raise SystemExit(f"timed out waiting for pid {pid} to change from {start}")

    def wait_shot(self, pid, pos=None, active=True, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            for packet in self.shots:
                if packet[2] != pid:
                    continue
                if bool(packet[5] & 0x01) != active:
                    continue
                if active and pos is not None and (packet[3], packet[4]) != pos:
                    continue
                return packet
        wanted = "active" if active else "clear"
        raise SystemExit(f"timed out waiting for {wanted} shot pid={pid} pos={pos}")

    def wait_respawn_pending(self, pid, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            for packet in self.respawns:
                if packet[2] == pid and (packet[5] & 0x01):
                    return packet
        raise SystemExit(f"timed out waiting for respawn pending pid={pid}")

    def wait_brick_delta(self, pos, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.02)
            for packet in self.brick_deltas:
                if (packet[2], packet[3]) == pos:
                    return packet
        raise SystemExit(f"timed out waiting for brick delta at {pos}")


def passable(bricks, pos, blocked):
    x, y = pos
    return 0 <= x < 20 and 0 <= y < 19 and bricks[y][x] == 0 and pos not in blocked


def bfs(bricks, start, goal, blocked):
    q = collections.deque([start])
    prev = {start: None}
    while q:
        cur = q.popleft()
        if cur == goal:
            out = []
            while prev[cur] is not None:
                out.append(cur)
                cur = prev[cur]
            out.reverse()
            return out
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
            return dir_id, joy
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
        _dir_id, joy = joy_for_step(current, step)
        actual = client.hold_until_change(pid, current, joy)
        client.send_neutral()
        client.wait_snapshot_pos(pid, actual)
        current = actual


def choose_move_then_fire(bricks, start, blocked):
    best = None
    for y in range(1, 18):
        for x in range(1, 19):
            origin = (x, y)
            if not passable(bricks, origin, blocked):
                continue
            for dir_id, (dx, dy, joy) in DIRS.items():
                moved = (x + dx, y + dy)
                shot = (moved[0] + dx, moved[1] + dy)
                if not passable(bricks, moved, blocked):
                    continue
                if not passable(bricks, shot, blocked):
                    continue
                path = bfs(bricks, start, origin, blocked)
                if path is not None:
                    candidate = (len(path), origin, dir_id, joy, moved, shot, path)
                    if best is None or candidate[0] < best[0]:
                        best = candidate
    if best is None:
        raise SystemExit("no move-then-fire scenario")
    _, origin, dir_id, joy, moved, shot, path = best
    return origin, dir_id, joy, moved, shot, path


def choose_turn_fire(bricks, start, blocked):
    best = None
    for y in range(1, 18):
        for x in range(1, 19):
            origin = (x, y)
            if not passable(bricks, origin, blocked):
                continue
            for dir_id, (dx, dy, joy) in DIRS.items():
                shot = (x + dx, y + dy)
                if not passable(bricks, shot, blocked):
                    continue
                path = bfs(bricks, start, origin, blocked)
                if path is not None:
                    candidate = (len(path), origin, dir_id, joy, shot, path)
                    if best is None or candidate[0] < best[0]:
                        best = candidate
    if best is None:
        raise SystemExit("no turn-fire scenario")
    _, origin, dir_id, joy, shot, path = best
    return origin, dir_id, joy, shot, path


def choose_adjacent_target(bricks, anchor, start, blocked):
    best = None
    for dir_id, (dx, dy, _joy) in DIRS.items():
        target = (anchor[0] + dx, anchor[1] + dy)
        if not passable(bricks, target, blocked):
            continue
        path = bfs(bricks, start, target, blocked)
        if path is not None:
            candidate = (len(path), target, dir_id, DIRS[dir_id][2], path)
            if best is None or candidate[0] < best[0]:
                best = candidate
    if best is None:
        raise SystemExit("no adjacent target scenario")
    _, target, dir_id, joy, path = best
    return target, dir_id, joy, path


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
                    candidate = (len(path), origin, dir_id, joy, wall, path)
                    if best is None or candidate[0] < best[0]:
                        best = candidate
    if best is None:
        raise SystemExit("no fire-into-brick scenario")
    _, origin, dir_id, joy, wall, path = best
    return origin, dir_id, joy, wall, path


clients = [Client(0), Client(1)]
for expected_pid, client in enumerate(clients):
    # The server only allocates a slot after the first inbound packet.
    client.sock.send(bytes([0x41, 1, expected_pid, 0x0F]))
    client.seq = 2
    client.wait_ready()

bricks = clients[0].bricks
slot0_pid = clients[0].pid
slot1_pid = clients[1].pid
slot0_pos = (clients[0].players[slot0_pid]["x"], clients[0].players[slot0_pid]["y"])
slot1_pos = (clients[0].players[slot1_pid]["x"], clients[0].players[slot1_pid]["y"])
other_slots = {
    (clients[0].players[idx]["x"], clients[0].players[idx]["y"])
    for idx in range(4)
    if idx not in (slot0_pid, slot1_pid)
}

origin, move_dir, move_joy, moved, shot_pos, path = choose_move_then_fire(
    bricks, slot0_pos, occupied_now(clients[0], slot0_pid)
)
walk(clients[0], slot0_pid, path)
slot0_pos = origin
clients[0].send_delta(move_joy)
actual = clients[0].wait_snapshot_change(slot0_pid, slot0_pos)
clients[0].send_neutral()
clients[0].wait_snapshot_pos(slot0_pid, actual)
slot0_pos = actual
clients[0].send_delta(move_joy | 0x10)
clients[0].pump(0.4)
clients[0].send_neutral()
clients[0].pump(4.5)

brick_origin, brick_dir, brick_joy, wall, path = choose_fire_into_brick(
    bricks, slot0_pos, occupied_now(clients[0], slot0_pid)
)
walk(clients[0], slot0_pid, path)
slot0_pos = brick_origin
clients[0].send_delta(brick_joy | 0x10)
clients[0].wait_brick_delta(wall)
clients[0].pump(0.4)
clients[0].send_neutral()

turn_origin, turn_dir, turn_joy, turn_shot_pos, path = choose_turn_fire(
    bricks, slot0_pos, occupied_now(clients[0], slot0_pid)
)
walk(clients[0], slot0_pid, path)
slot0_pos = turn_origin
clients[0].send_delta(turn_joy | 0x10)
clients[0].pump(0.4)
clients[0].send_neutral()
clients[0].pump(4.5)

target, hit_dir, hit_joy, path = choose_adjacent_target(
    bricks, slot0_pos, slot1_pos, occupied_now(clients[0], slot1_pid)
)
walk(clients[1], slot1_pid, path)
slot1_pos = target
clients[0].send_delta(hit_joy | 0x10)
clients[0].wait_respawn_pending(slot1_pid)
clients[0].pump(0.4)
clients[0].send_neutral()

for client in clients:
    client.sock.close()
PY

kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=

grep -E "combat order phase=move-apply slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "combat order phase=fire-eval slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "combat order phase=move-gate slot=[0-3] reason=directional-fire" "$LOG_FILE" >/dev/null
grep -E "combat event kind=shot-spawn slot=[0-3]" "$LOG_FILE" >/dev/null
grep -E "combat event kind=brick-break slot=[0-3] phase=fire-eval" "$LOG_FILE" >/dev/null
grep -E "combat event kind=immediate-hit slot=[0-3] victim=[0-3]" "$LOG_FILE" >/dev/null

shot_count=$(grep -Ec "combat event kind=shot-spawn slot=[0-3]" "$LOG_FILE")
if [ "$shot_count" -lt 2 ]; then
  echo "expected at least two authoritative shot-spawn events" >&2
  exit 1
fi
