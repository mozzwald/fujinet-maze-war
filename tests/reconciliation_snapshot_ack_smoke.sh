#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"

PORT=9103
LOG_FILE=$(mktemp)
SERVER_PID=

cleanup() {
  if [ -n "${SERVER_PID}" ]; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  rm -f "${LOG_FILE}"
}

trap cleanup EXIT INT TERM

make build/maze-war-server >/dev/null
./build/maze-war-server --port "${PORT}" --zombies 0 --debug >"${LOG_FILE}" 2>&1 &
SERVER_PID=$!

sleep 1

python3 - <<'PY' "${PORT}"
import socket
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

import sys
import time

port = int(sys.argv[1])


def expect_snapshot(sock, expected_slot, expected_ack):
    deadline = time.time() + 5.0
    while time.time() < deadline:
        packet = cobs_decode(recv_frame(sock, 256))
        if len(packet) < 20 or packet[0] != 0x40:
            continue
        flags = packet[2]
        recipient = (flags >> 1) & 0x03
        ack_valid = 1 if (flags & 0x80) else 0
        ack_seq = packet[19]
        if recipient != expected_slot:
            continue
        if ack_valid != 1 or ack_seq != expected_ack:
            raise SystemExit(
                f"slot {expected_slot} expected ack_valid=1 ack_seq={expected_ack}, "
                f"got ack_valid={ack_valid} ack_seq={ack_seq}"
            )
        print(f"slot{expected_slot}_ack ack_valid={ack_valid} ack_seq={ack_seq}")
        return
    raise SystemExit(f"timed out waiting for slot {expected_slot} snapshot")


sockets = []
for _ in range(2):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("127.0.0.1", port))
    sock.settimeout(0.5)
    sockets.append(sock)

try:
    send_frame(sockets[0], bytes([0x41, 7, 0, 0x07]))
    send_frame(sockets[1], bytes([0x41, 33, 1, 0x0E]))
    expect_snapshot(sockets[0], 0, 7)
    expect_snapshot(sockets[1], 1, 33)
finally:
    for sock in sockets:
      sock.close()
PY

# The assertions above inspect each recipient's actual TCP frame and ack. Do
# not require debug lines after both peers have closed: EOF handoff can run
# before the server reaches its next diagnostic print.
