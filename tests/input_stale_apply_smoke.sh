#!/bin/sh

# An input applied late must still be applied.
#
# `last_input_ms` is stamped when a DELTA arrives, but inputs are applied one
# per tick in order, so an entry that waits its turn is already older than
# INPUT_STALE_MS by the time it runs. The server used to run a wall-clock
# staleness test over the top of the queue and reset joy to neutral in the same
# tick that applied the direction -- after acking it. The client had been told
# the input landed, so it dropped it from its pending ring and never replayed
# it: an acked-but-discarded input, which is what a snap back is made of.
#
# At the 4 Hz the combat smokes use, two ticks is exactly INPUT_STALE_MS, which
# is why combat_world_authority_smoke failed about four runs in ten. `--lag-ms`
# reproduces the same backlog deterministically.

set -eu

PORT=9173
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/input-stale-apply.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/input-stale-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ $status -ne 0 ]; then
    printf 'input stale apply smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

# Open map, so the step is free whichever direction we pick.
cat >"$BRICK_FILE" <<'BRICKEOF'
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
BRICKEOF

# --lag-ms 900 puts every input well past the 500 ms staleness window before it
# is eligible to run, which is exactly the backlog a slow drain produces.
"$SERVER_BIN" --port "$PORT" --tick-hz 10 --zombies 0 --lag-ms 900 \
    --brick "$BRICK_FILE" --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket, sys, time
from tcp_frames import recv_frame, send_frame


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


def fail(m):
    raise SystemExit("FAIL: " + m)


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.seq = 1
        self.pid = None
        self.pos = None
        self.ack = None

    def send(self, joy):
        send_frame(self.sock, bytes([0x41, self.seq & 0xFF, 0, joy]))
        self.seq = (self.seq + 1) & 0xFF
        return (self.seq - 1) & 0xFF

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = cobs_decode(recv_frame(self.sock, 256))
            except socket.timeout:
                continue
            if len(p) >= 20 and p[0] == 0x40:
                self.pid = (p[2] >> 1) & 0x03
                self.pos = (p[3 + self.pid * 2], p[4 + self.pid * 2])
                if p[2] & 0x80:
                    self.ack = p[19]


c = C()
c.send(0x0F)
c.pump(2.0)
if c.pos is None:
    fail("no snapshot received")

start = c.pos
# Away from the wall on an open map, so the step cannot be refused.
if start[0] < 18:
    stick, delta = 0x07, 1     # right
else:
    stick, delta = 0x0B, -1    # left
want = (start[0] + delta, start[1])

sent = c.send(stick)

deadline = time.time() + 8.0
while time.time() < deadline:
    c.pump(0.1)
    if c.pos == want:
        break

if c.pos != want:
    fail(f"an input applied {900} ms after it arrived never moved the actor: "
         f"still at {c.pos}, expected {want}")

# ...and it must have been acked, i.e. applied rather than quietly dropped.
deadline = time.time() + 3.0
while time.time() < deadline and c.ack != sent:
    c.pump(0.1)
if c.ack != sent:
    fail(f"the move landed but seq {sent} was never acked (ack={c.ack})")

print(f"input applied a second after arrival still moved: {start} -> {c.pos}")
PYEOF

SERVER_SRC="$ROOT_DIR/server/main.c"
# The wall-clock reset must not apply to a slot that still has a client in it:
# for those, apply_queued_input() is the only thing allowed to set joy.
grep -F "if (!zombie_mask[i] && !clients[i].in_use) {" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: the staleness reset can fight the input queue again" >&2
    exit 1
}
# It is still needed for a slot whose client is gone: that slot is not in
# apply_queued_input()'s loop, so nothing else would stop its actor walking.
grep -A3 -F "if (!zombie_mask[i] && !clients[i].in_use) {" "$SERVER_SRC" \
    | grep -F "INPUT_STALE_MS" >/dev/null || {
    echo "FAIL: a departed client's actor is never returned to neutral" >&2
    exit 1
}

echo "input stale apply smoke passed"
