#!/bin/sh

# Authoritative input contract.
#   - every distinct input a client sends is applied, in order, one per tick
#   - the ack names only inputs actually applied, never merely received
#   - repeats of the same joy coalesce, so keepalives cannot delay a real turn
#
# The bug this pins: the server used to keep only the newest joy per tick and
# overwrite the rest, while acking every sequence it received. A quick corner
# turn never reached the simulation, but the client was told it had, dropped it
# from its pending ring, and then snapped back once drift crossed the reconcile
# threshold.

set -eu

PORT=9161
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/input-queue-smoke.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'input queue smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

# A slow tick makes the between-ticks window wide and the assertions crisp.
"$SERVER_BIN" --port "$PORT" --tick-hz 4 --zombies 0 --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket, sys, time

port = int(sys.argv[1])
TICK = 0.25


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.seq = 1

    def send(self, joy):
        self.sock.send(bytes([0x41, self.seq & 0xFF, 0, joy]))
        self.seq += 1
        return (self.seq - 1) & 0xFF

    def ack(self, secs):
        end = time.time() + secs
        out = None
        while time.time() < end:
            try:
                p = self.sock.recv(256)
            except socket.timeout:
                continue
            if len(p) >= 20 and p[0] == 0x40 and (p[2] & 0x80):
                out = p[19]
        return out


def fail(m):
    raise SystemExit("FAIL: " + m)


c = C()
c.send(0x0F)
c.ack(1.0)

# Five distinct inputs inside a single tick: a fast corner turn. Every one must
# be applied, and the ack must walk them one per tick rather than jumping.
burst = [c.send(j) for j in (0x07, 0x0D, 0x0B, 0x0E, 0x07)]
seen = []
for _ in range(8):
    a = c.ack(TICK + 0.02)
    if a is not None and (not seen or seen[-1] != a):
        seen.append(a)
    if seen and seen[-1] == burst[-1]:
        break

if not seen:
    fail("no acks at all")
if seen[0] == burst[-1]:
    fail(f"ack jumped straight to the newest sequence {burst[-1]}; inputs the "
         "server never applied are being acknowledged")
for a in seen:
    if a not in burst:
        fail(f"ack {a} is not one of the sent sequences {burst}")
if seen[-1] != burst[-1]:
    fail(f"ack stalled at {seen[-1]}, never reached {burst[-1]}: an input was lost")
if len(seen) < 3:
    fail(f"ack only took {len(seen)} steps for a 5-input burst: inputs are "
         "still being coalesced away or dropped")
print(f"burst applied in order, ack walked {seen}")

# Repeats of the same joy must not accumulate. Spam far faster than the tick,
# then send one genuine turn: it has to land on the very next tick.
for _ in range(60):
    c.send(0x0F)
    time.sleep(0.02)
c.ack(2 * TICK)
turn = c.send(0x0D)
landed = None
for k in range(4):
    a = c.ack(TICK + 0.02)
    if a == turn:
        landed = k
        break
if landed is None:
    fail("a turn sent after idle keepalives never got applied; repeats are "
         "building a backlog ahead of real input")
if landed > 1:
    fail(f"turn took {landed + 1} ticks to apply; keepalives are adding latency")
print("turn after keepalive spam applied promptly")
PYEOF

# Keepalive spam must not have overflowed the queue.
if grep -F "queue-full" "$LOG_FILE" >/dev/null; then
    echo "FAIL: queue overflowed on plain keepalives" >&2
    exit 1
fi

SERVER_SRC="$ROOT_DIR/server/main.c"
# The ack must be set where the input is applied, never from what was received.
if grep -F "applied_input_seq = clients[i].last_delta_seq" "$SERVER_SRC"; then
    echo "FAIL: ack is back to reporting merely-received sequences" >&2
    exit 1
fi
grep -A6 -F "clients[i].input_q[head].joy" "$SERVER_SRC" \
    | grep -F "applied_input_seq" >/dev/null || {
    echo "FAIL: ack is not set from the applied queue entry" >&2
    exit 1
}
grep -E "INPUT_REPEAT_MAX" "$SERVER_SRC" >/dev/null

echo "input queue smoke passed"
