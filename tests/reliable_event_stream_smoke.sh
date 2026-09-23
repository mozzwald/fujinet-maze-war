#!/bin/sh

# Reliable NAME/BRICK/RESPAWN events ride inside 0x53 frames with cumulative
# 0x45 ACKs. A duplicate ACK for the previous revision must trigger a prompt
# byte-identical retransmit of the unacknowledged event.

set -eu

PORT=9178
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/reliable-events.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'reliable event stream smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

"$SERVER_BIN" --port "$PORT" --tick-hz 20 --zombies 0 --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket
import sys
import time
from tcp_frames import decode_frame, recv_frame, send_frame, set_auto_ack

port = int(sys.argv[1])
PKT_NAME = 0x43
PKT_RELIABLE_ACK = 0x45
PKT_RELIABLE_EVENT = 0x53
NAME_LEN = 8


def fail(msg):
    raise SystemExit("FAIL: " + msg)


def wait_reliable(sock, timeout=1.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            pkt = decode_frame(recv_frame(sock, 256))
        except socket.timeout:
            continue
        if pkt and pkt[0] == PKT_RELIABLE_EVENT:
            return pkt
    fail("reliable event not received")


with socket.create_connection(("127.0.0.1", port), timeout=1.0) as sock:
    sock.settimeout(0.05)
    set_auto_ack(sock, False)
    send_frame(sock, bytes([PKT_NAME, 1, 0]) + b"RELIABLE")

    first = wait_reliable(sock)
    if first[4] != PKT_NAME or first[7:7 + NAME_LEN] != b"RELIABLE":
        fail(f"unexpected reliable payload: {first.hex()}")
    rev = first[2] | (first[3] << 8)
    if rev != 3:
        fail(f"first application reliable revision was {rev}, expected 3")

    # Duplicate ACK of the previous cumulative revision asks for the missing
    # follower before the normal retransmit timer has to fire.
    start = time.monotonic()
    send_frame(sock, bytes([PKT_RELIABLE_ACK, 2, 2, 0]))
    repeat = wait_reliable(sock, timeout=0.25)
    elapsed = time.monotonic() - start
    if repeat != first:
        fail("fast retransmit was not byte-identical")
    if elapsed >= 0.25:
        fail(f"fast retransmit took {elapsed:.3f}s")

    # ACKing revision 3 should advance to revision 4. The server may have
    # queued later name-rotation events by now, but stop-and-wait must send only
    # the next oldest reliable event, not a whole pending window.
    send_frame(sock, bytes([PKT_RELIABLE_ACK, 3, rev & 0xff, rev >> 8]))
    second = wait_reliable(sock)
    rev2 = second[2] | (second[3] << 8)
    if rev2 != 4:
        fail(f"reliable stream did not advance to revision 4, got {rev2}")
    time.sleep(0.7)
    sock.settimeout(0.01)
    extras = []
    deadline = time.monotonic() + 0.1
    while time.monotonic() < deadline:
        try:
            pkt = decode_frame(recv_frame(sock, 256))
        except socket.timeout:
            continue
        if pkt and pkt[0] == PKT_RELIABLE_EVENT:
            extras.append(pkt[2] | (pkt[3] << 8))
    if extras and any(extra != 4 for extra in extras):
        fail(f"stop-and-wait retransmitted beyond oldest unacked rev: {extras}")

print("reliable event stream fast retransmit and stop-and-wait passed")
PYEOF

grep -F "TX reliable fast slot=0 rev=3" "$LOG_FILE" >/dev/null
grep -F "ACK reliable slot=0 rev=3" "$LOG_FILE" >/dev/null

echo "reliable event stream smoke passed"
