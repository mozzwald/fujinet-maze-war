#!/bin/sh

set -eu

PORT=9101
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/transport-normalize-smoke.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'transport smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

"$SERVER_BIN" --port "$PORT" --zombies 0 --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])
cases = [
    ("primary", bytes([0x41, 0x01, 0x00, 0x0F])),
    ("swapped", bytes([0x41, 0x00, 0x02, 0x0E])),
    ("extra-0x41", bytes([0x41, 0x41, 0x03, 0x00, 0x0D])),
]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for _label, payload in cases:
    sock.sendto(payload, ("127.0.0.1", port))
    time.sleep(0.2)
sock.close()
time.sleep(0.5)
PY

kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=

grep -F "transport accepted slot=0 format=primary" "$LOG_FILE" >/dev/null
grep -F "transport accepted slot=0 format=swapped" "$LOG_FILE" >/dev/null
grep -F "transport accepted slot=0 format=extra-41" "$LOG_FILE" >/dev/null
