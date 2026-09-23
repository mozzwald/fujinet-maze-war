#!/bin/sh

# Zombies make committed, server-authoritative moves at a slower cadence than
# humans. This test observes only normal snapshots: no client-specific zombie
# presentation or protocol field is required.

set -eu

PORT=9139
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/zombie-cadence.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/zombie-cadence-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ "$status" -ne 0 ]; then
    printf 'zombie cadence smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit "$status"
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

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

"$SERVER_BIN" --port "$PORT" --tick-hz 10 --zombies 1 --brick "$BRICK_FILE" \
  --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PY'
import socket
import sys
import time

from tcp_frames import decode_frame, recv_frame, send_frame

port = int(sys.argv[1])
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", port))
sock.settimeout(0.08)

seq = 1
last = None
moves = []
positions = []
deadline = time.monotonic() + 6.0
next_send = 0.0

while time.monotonic() < deadline:
    now = time.monotonic()
    if now >= next_send:
        send_frame(sock, bytes((0x41, seq & 0xFF, 0, 0x0F)))
        seq += 1
        next_send = now + 0.1
    try:
        frame = recv_frame(sock, 256)
    except socket.timeout:
        continue
    packet = decode_frame(frame)
    if len(packet) != 21 or packet[0] != 0x40:
        continue
    # Our human owns slot 0; --zombies 1 therefore owns slot 1.
    if ((packet[2] >> 3) & 0x0F) != 0x02:
        raise SystemExit(f"unexpected zombie mask {(packet[2] >> 3) & 0x0F:#04x}")
    current = (packet[5], packet[6])
    if last is not None and current != last:
        dx = abs(current[0] - last[0])
        dy = abs(current[1] - last[1])
        if dx + dy != 1:
            raise SystemExit(f"zombie skipped cells {last} -> {current}")
        moves.append(time.monotonic())
        positions.append(current)
    last = current

sock.close()
if len(moves) < 5:
    raise SystemExit(f"saw only {len(moves)} zombie moves")
intervals = [b - a for a, b in zip(moves, moves[1:])]
# A 200 ms schedule resolves to 200–300 ms on the 10 Hz simulation. It must
# never regress to one-cell-per-tick pursuit, and normal open-area moves must
# stay in the intended four-to-five-cells-per-second range.
if sum(0.15 <= interval <= 0.35 for interval in intervals) < 3:
    raise SystemExit(f"no committed 4-5 cells/sec zombie run: {intervals!r}")
for a, b, c in zip(positions, positions[1:], positions[2:]):
    if a == c:
        raise SystemExit(f"zombie immediately reversed {a} -> {b} -> {c}")
print("zombie cadence assertions passed")
PY

# Preserve the server-only contract: movement commitments and firing windup
# remain in the server; the client receives only ordinary snapshots and shots.
SERVER_SRC="$ROOT_DIR/server/main.c"
grep -F '#define ZOMBIE_MOVE_INTERVAL_MS 200' "$SERVER_SRC" >/dev/null
grep -F '#define ZOMBIE_DECISION_MIN_MOVES 3' "$SERVER_SRC" >/dev/null
grep -F '#define ZOMBIE_DECISION_MAX_MOVES 7' "$SERVER_SRC" >/dev/null
grep -F '#define ZOMBIE_FIRE_WINDUP_MIN_MS 200' "$SERVER_SRC" >/dev/null
grep -F 'static int zombie_shot_target_valid' "$SERVER_SRC" >/dev/null
grep -F 'zombie_fire_windup_until_ms' "$SERVER_SRC" >/dev/null
grep -F 'zombie fire cancel' "$SERVER_SRC" >/dev/null
grep -F 'zombie fire release' "$SERVER_SRC" >/dev/null
grep -F 'static int zombie_attempts_brick' "$SERVER_SRC" >/dev/null
grep -F 'static int zombie_attempts_breakable_brick' "$SERVER_SRC" >/dev/null
grep -F 'ZOMBIE_WALL_HESITATE_MS' "$SERVER_SRC" >/dev/null
grep -F '#define ZOMBIE_BRICK_FIRE_COOLDOWN_MS 3000' "$SERVER_SRC" >/dev/null
grep -F 'zombie brick fire pending' "$SERVER_SRC" >/dev/null
grep -F 'zombie brick fire slot=' "$SERVER_SRC" >/dev/null
if grep -q 'ZOMBIE_SKIP_DENOMINATOR\|ZOMBIE_REST_DENOMINATOR' "$SERVER_SRC"; then
  echo 'FAIL: random per-tick zombie pacing remains' >&2
  exit 1
fi
if ! grep -Eq 'zombie pursue slot=1 dir=[0-3] moves=[3-7]' "$LOG_FILE"; then
  echo 'FAIL: zombie did not make a bounded pursuit commitment' >&2
  exit 1
fi
