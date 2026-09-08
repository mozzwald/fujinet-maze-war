#!/bin/sh

# A slot nobody is in must not be solid.
#
# With `--zombies` below 3 the server still keeps a spawn position for slots no
# client holds. Those positions were counted in movement collision, fire
# evaluation and shot hits, so an empty slot was an invisible wall. Clients draw
# nothing there and their own prediction walks straight through, so the client
# moved, the server refused, and roughly three cells later the accumulated drift
# crossed the reconcile threshold and yanked the player back -- the "snap back
# after walking through nothing" report.
#
# Run against the real server on an open map, so a straight walk needs no
# pathfinding: only the outer wall is solid.

set -eu

PORT=9172
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/vacant-slot-collision.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/vacant-slot-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ $status -ne 0 ]; then
    printf 'vacant slot collision smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

# Open map: outer wall only.
cat >"$BRICK_FILE" <<'EOF'
####################
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
#..................#
####################
EOF

"$SERVER_BIN" --port "$PORT" --tick-hz 20 --zombies 0 --brick "$BRICK_FILE" \
    --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket, sys, time


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


port = int(sys.argv[1])
PKT_SNAPSHOT = 0x40
PKT_DELTA = 0x41
PKT_SEATS = 0x44

STICK = {"right": 0x07, "down": 0x0D, "left": 0x0B, "up": 0x0E}
NEUTRAL = 0x0F


def fail(m):
    raise SystemExit("FAIL: " + m)


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.seq = 0
        self.pid = None
        self.pos = {}
        self.seat_mask = None

    def send(self, joy):
        pid = self.pid if self.pid is not None else 0
        self.sock.send(bytes([PKT_DELTA, self.seq, pid, joy]))
        self.seq = (self.seq + 1) & 0xFF

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = cobs_decode(self.sock.recv(256))
            except socket.timeout:
                continue
            if len(p) >= 20 and p[0] == PKT_SNAPSHOT:
                self.pid = (p[2] >> 1) & 0x03
                for i in range(4):
                    self.pos[i] = (p[3 + i * 2], p[4 + i * 2])
            elif len(p) >= 3 and p[0] == PKT_SEATS:
                self.seat_mask = p[2] & 0x0F

    def me(self):
        return self.pos[self.pid]

    def walk_axis(self, axis, target, timeout=25.0):
        """Hold a direction until our own coordinate reaches target."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            cur = self.me()
            if cur[axis] == target:
                self.send(NEUTRAL)
                self.pump(0.1)
                return
            if axis == 0:
                self.send(STICK["right"] if target > cur[0] else STICK["left"])
            else:
                self.send(STICK["down"] if target > cur[1] else STICK["up"])
            self.pump(0.06)
        fail(f"stuck at {self.me()} walking axis {axis} to {target}; "
             f"an empty slot is still solid")


a = C()
a.send(NEUTRAL)
deadline = time.time() + 5.0
while time.time() < deadline:
    a.pump(0.2)
    if a.pid is not None and a.seat_mask is not None and len(a.pos) == 4:
        break
if a.pid is None or len(a.pos) != 4:
    fail("no snapshot received")
if a.seat_mask != 1 << a.pid:
    fail(f"expected only our own seat held, got {a.seat_mask:#06b}")

vacant = [i for i in range(4) if i != a.pid]
target = a.pos[vacant[0]]
start = a.me()
if target == start:
    fail("server stacked two actors on one cell at startup")

# Straight lines on an open map: x first, then y, ending exactly on the empty
# slot's cell. Before the fix the final step onto it was refused.
a.walk_axis(0, target[0])
a.walk_axis(1, target[1])

if a.me() != target:
    fail(f"expected to stand on the empty slot at {target}, at {a.me()}")

# And standing there must not have been recorded as a kill or a hit.
if a.pos[vacant[0]] != target:
    fail("the empty slot moved; it should be inert")

print("vacant slot collision assertions passed")
PYEOF

# The walk above is the assertion; the server's own move-blocked lines are not
# usable as one, because holding a direction until the target coordinate is
# reached always ends with a few queued steps pushing into the outer wall.

SERVER_SRC="$ROOT_DIR/server/main.c"
# One predicate for "is this slot a thing you can walk into or shoot", so the
# three call sites cannot drift apart again.
grep -E "^static int slot_on_board" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: no single on-board predicate" >&2; exit 1; }
if [ "$(grep -c "slot_on_board(players," "$SERVER_SRC")" -lt 3 ]; then
    echo "FAIL: collision, fire evaluation and shot hits do not all use it" >&2
    exit 1
fi
# It has to know who is actually in a slot, refreshed every tick.
grep -F "g_occupied_mask" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: the on-board predicate cannot see slot occupancy" >&2; exit 1; }
grep -A6 -F "g_occupied_mask = 0;" "$SERVER_SRC" | grep -F "zombie_mask[i] || human_mask[i]" >/dev/null || {
    echo "FAIL: occupancy is not rebuilt from this tick's masks" >&2; exit 1; }

# Atari: the hide pass runs every frame, not only when the HUD is refreshed.
# Gating it on NET_SCORE_PEND let the first-snapshot reveal draw a wizard in
# every empty slot until the next SEATS or role change -- a visible flash.
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
TAB=$(printf '\t')
grep -A8 -E "^VBI_SCR_OK" "$ATARI_SRC" | grep -E "JSR[$TAB ]+NET_VACANT_UPDATE" >/dev/null || {
    echo "FAIL: vacant slots are only hidden on a HUD refresh" >&2; exit 1; }

echo "vacant slot collision smoke passed"
