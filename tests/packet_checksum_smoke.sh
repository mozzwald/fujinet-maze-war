#!/bin/sh

# Server->client packets are COBS framed with a trailing sum checksum.
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
# The checksum makes a misframed packet fail closed instead of being applied.

set -eu

PORT=9271
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
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

port = int(sys.argv[1])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("127.0.0.1", port))
s.settimeout(0.2)


def fail(m):
    raise SystemExit("FAIL: " + m)


seq = 1


def send(payload):
    global seq
    s.send(bytes(payload))
    seq = (seq + 1) & 0xFF


# Join, name ourselves and keep firing, so snapshots, names, shots and brick
# packets all cross the wire during the sample window.
send([0x41, seq, 0, 0x0F])
send([0x43, seq, 0] + list(b"checksum"))

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
        p = s.recv(512)
    except socket.timeout:
        send([0x41, seq, 0, 0x17])
        continue
    if len(p) < 3:
        continue
    # framing: exactly one zero, and it terminates the datagram
    if p[-1] != 0 or 0 in p[:-1]:
        unframed.append(p[:8].hex())
        continue
    dec = cobs_decode(p[:-1])
    if dec is None or len(dec) < 2:
        bad.append(("undecodable", p[:8].hex()))
        continue
    want = 0
    for b in dec[:-1]:
        want = (want + b) & 0xFF
    seen[dec[0]] = seen.get(dec[0], 0) + 1
    if dec[-1] != want:
        bad.append((hex(dec[0]), len(dec), dec[-1], want))

if unframed:
    fail(f"{len(unframed)} datagrams were not COBS framed (interior zero, or "
         f"no trailing delimiter): {unframed[:3]}")
if bad:
    fail(f"{len(bad)} frames failed to decode or carried a wrong checksum: "
         f"{bad[:4]}")
if 0x40 not in seen:
    fail("never received a snapshot; the assertion proves nothing")
print("COBS framing + checksum verified on " + ", ".join(
    f"{hex(t)} x{n}" for t, n in sorted(seen.items())))
PYEOF

echo "packet checksum smoke passed"
