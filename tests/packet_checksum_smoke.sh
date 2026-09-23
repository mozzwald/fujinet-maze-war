#!/bin/sh

# Both directions use COBS framing with a CRC-16/CCITT-FALSE trailer.
#
# The Atari receives over SIO as a byte stream. A dropped or duplicated byte
# shifts framing and payload bytes start being read as packet type markers, and
# bounds checks alone let far too much of that through on real hardware:
# corrupt positions put actors on the border, where ERASMAN blanks two
# characters and ate the border cell; corrupt scores flickered; a corrupt brick
# delta cleared a random cell, which the 3s map repair then repainted seconds
# later; and a corrupt sequence number parked the client ~100 ticks in the
# future, so every genuine snapshot was dropped as stale for seconds.
#
# The CRC makes a misframed packet fail closed instead of being applied.

set -eu

PORT=9271
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  exit $status
}
trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

"$SERVER_BIN" --port "$PORT" --tick-hz 10 --zombies 1 >/dev/null 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket, sys, time
from tcp_frames import crc16_ccitt_false, recv_frame, send_frame

port = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", port))
s.settimeout(0.2)


def fail(m):
    raise SystemExit("FAIL: " + m)


seq = 1


def send(payload):
    global seq
    send_frame(s, bytes(payload))
    seq = (seq + 1) & 0xFF


# Join, name ourselves and keep firing, so snapshots, names, shots and brick
# packets all cross the wire during the sample window.
send([0x41, seq, 0, 0x0F])
send([0x43, seq, 0] + list(b"crc-frame"))

def cobs_decode(frame):
    """frame excludes the trailing delimiter"""
    out = bytearray()
    rd = 0
    while rd < len(frame):
        code = frame[rd]
        rd += 1
        if code == 0:
            return None
        for _ in range(code - 1):
            if rd >= len(frame):
                return None
            out.append(frame[rd])
            rd += 1
        if code != 0xFF and rd < len(frame):
            out.append(0)
    return bytes(out)


seen = {}
bad = []
unframed = []
end = time.time() + 4.0
while time.time() < end:
    try:
        p = recv_frame(s, 512)
    except socket.timeout:
        send([0x41, seq, 0, 0x17])
        continue
    if len(p) < 3:
        continue
    # framing: exactly one zero, and it terminates the frame
    if p[-1] != 0 or 0 in p[:-1]:
        unframed.append(p[:8].hex())
        continue
    dec = cobs_decode(p[:-1])
    if dec is None or len(dec) < 3:
        bad.append(("undecodable", p[:8].hex()))
        continue
    want = crc16_ccitt_false(dec[:-2])
    seen[dec[0]] = seen.get(dec[0], 0) + 1
    if dec[-2:] != bytes((want & 0xff, want >> 8)):
        bad.append((hex(dec[0]), len(dec), dec[-2:].hex(), hex(want)))

if unframed:
    fail(f"{len(unframed)} frames were not COBS framed (interior zero, or "
         f"no trailing delimiter): {unframed[:3]}")
if bad:
    fail(f"{len(bad)} frames failed to decode or carried a wrong CRC: "
         f"{bad[:4]}")
if 0x40 not in seen:
    fail("never received a snapshot; the assertion proves nothing")
print("COBS framing + CRC-16 verified on " + ", ".join(
    f"{hex(t)} x{n}" for t, n in sorted(seen.items())))
PYEOF

echo "packet CRC smoke passed"
