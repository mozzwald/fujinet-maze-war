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
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
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
TICK = 0.25


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.seq = 1

    def send(self, joy):
        send_frame(self.sock, bytes([0x41, self.seq & 0xFF, 0, joy]))
        self.seq += 1
        return (self.seq - 1) & 0xFF

    def ack(self, secs):
        end = time.time() + secs
        out = None
        while time.time() < end:
            try:
                p = cobs_decode(recv_frame(self.sock, 256))
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

# A repeat of the joy already queued is still its own input. The ack must walk
# one applied sequence per tick and never jump to the newest received. Folding a
# repeat into the waiting entry used to advance that entry's sequence, so
# applying it acked inputs that had never run; the client discards its pending
# ring up to the ack and kept the cells it had already predicted, diverging by
# one cell per fold with nothing left pending to show for it.
reps = [c.send(0x0D) for _ in range(4)]
first = c.ack(TICK + 0.02)
if first == reps[-1]:
    fail(f"ack jumped to {reps[-1]} after a single tick; repeats were folded "
         "and the ack claims inputs the server never applied")
seen2 = []
for _ in range(8):
    a = c.ack(TICK + 0.02)
    if a is not None and (not seen2 or seen2[-1] != a):
        seen2.append(a)
    if seen2 and seen2[-1] == reps[-1]:
        break
if not seen2 or seen2[-1] != reps[-1]:
    fail(f"ack stalled at {seen2[-1] if seen2 else None}, never reached "
         f"{reps[-1]}: a repeated input was swallowed")
print(f"repeats each applied in turn, ack walked {seen2}")
PYEOF

# Keepalive spam must not have overflowed the queue.
if grep -F "queue-full" "$LOG_FILE" >/dev/null; then
    echo "FAIL: queue overflowed on a handful of repeats" >&2
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
# The server must never walk the actor on a tick the client did not ask for.
# Repeating the last direction over a late packet moved a cell the client never
# predicted, and the client had already been acked, so it was never reconciled.
if grep -E "input_repeat_left" "$SERVER_SRC" >/dev/null; then
    echo "FAIL: the server repeats the last direction on an empty queue again;" \
         "that invents movement the client never predicted" >&2
    exit 1
fi

# Folding is allowed only for idle keepalives. Folding a directional repeat
# advances the queued entry's sequence, so applying it acks inputs the client
# already predicted cells for, and those cells are never reconciled.
if grep -F "input_q[last].seq = delta.seq" "$SERVER_SRC" >/dev/null; then
    grep -F "(delta.joy & 0x1F) == 0x0F" "$SERVER_SRC" >/dev/null || {
        echo "FAIL: repeats are folded without restricting the fold to" \
             "neutral keepalives; the ack will over-report what was applied" >&2
        exit 1
    }
fi

echo "input queue smoke passed"
