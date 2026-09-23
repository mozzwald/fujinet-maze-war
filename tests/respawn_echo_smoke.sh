#!/bin/sh

# RESPAWN must be repeated, like every other once-only packet in this server.
#
# RESPAWN hides an actor (pending) and un-hides it somewhere else (final), and
# it was the last transition packet still broadcast exactly once. A SHOT clear
# bursts three times, a BRICK_DELTA echoes, NAME rotates, the map resyncs --
# each of those repeats exists because a single lost packet left a stale sprite.
#
# Losing a pending RESPAWN leaves the victim's wizard standing on the cell it
# died on until the final spawn moves it two seconds later, with a hole where
# the killing shot's own clear blanked the characters it had drawn over it.
# Losing a final RESPAWN is worse: the client keeps that actor hidden until its
# next death, because only an explicit final spawn clears the hide.
#
# None of this reproduces on loopback, which never drops a packet, so the test
# asserts the repeat itself rather than the visible symptom.

set -eu

PORT=9174
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/respawn-echo.XXXXXX.log")
BRICK_FILE=$(mktemp "${TMPDIR:-/tmp}/respawn-echo-bricks.XXXXXX.txt")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$BRICK_FILE"
  if [ $status -ne 0 ]; then
    printf 'respawn echo smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

# Open map: two clients can line up and shoot without any pathfinding.
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

"$SERVER_BIN" --port "$PORT" --tick-hz 20 --zombies 0 --brick "$BRICK_FILE" \
    --debug >"$LOG_FILE" 2>&1 &
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
PKT_SNAPSHOT, PKT_DELTA, PKT_RESPAWN = 0x40, 0x41, 0x52
STICK = {"right": 0x07, "down": 0x0D, "left": 0x0B, "up": 0x0E}
NEUTRAL = 0x0F


def fail(m):
    raise SystemExit("FAIL: " + m)


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.seq = 0
        self.pid = None
        self.pos = {}
        self.respawns = []      # (pid, flags)

    def send(self, joy):
        send_frame(self.sock, bytes([PKT_DELTA, self.seq,
                              self.pid if self.pid is not None else 0, joy]))
        self.seq = (self.seq + 1) & 0xFF

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = cobs_decode(recv_frame(self.sock, 256))
            except socket.timeout:
                continue
            if len(p) >= 20 and p[0] == PKT_SNAPSHOT:
                self.pid = (p[2] >> 1) & 0x03
                for i in range(4):
                    self.pos[i] = (p[3 + i * 2], p[4 + i * 2])
            elif len(p) >= 6 and p[0] == PKT_RESPAWN:
                self.respawns.append((p[2], p[5]))


a, b = C(), C()
for c in (a, b):
    c.send(NEUTRAL)
for _ in range(10):
    a.pump(0.1)
    b.pump(0.1)
if a.pid is None or b.pid is None or a.pid == b.pid:
    fail(f"two clients expected in different slots, got {a.pid} and {b.pid}")

# Hunt: b walks onto a's row, closes in, and fires. The map is open, so this is
# straight lines only.
deadline = time.time() + 30.0
while time.time() - deadline < 0 and not any(f & 0x01 for _, f in a.respawns):
    a.pump(0.05)
    b.pump(0.05)
    if b.pid not in b.pos or a.pid not in b.pos:
        continue
    me, t = b.pos[b.pid], b.pos[a.pid]
    if t == (255, 255):
        break
    dy = t[1] - me[1]
    dx = t[0] - me[0]
    if dy:
        b.send(STICK["down"] if dy > 0 else STICK["up"])
    elif abs(dx) > 1:
        b.send(STICK["right"] if dx > 0 else STICK["left"])
    else:
        b.send((STICK["right"] if dx > 0 else STICK["left"]) | 0x10)

# Keep listening: the echoes go out one per tick after the original, so
# counting the moment the first one lands would only ever see one.
a.pump(0.6)
b.pump(0.1)

pending = [r for r in a.respawns if r[1] & 0x01 and not (r[1] & 0x02)]
if not pending:
    fail("no client was ever killed; the test never exercised RESPAWN")

victim = pending[0][0]
n_pending = sum(1 for p, f in a.respawns if p == victim and f == 0x01)
if n_pending < 2:
    fail(f"pending RESPAWN for pid {victim} sent {n_pending} time(s); "
         "one lost packet leaves the corpse standing until the final spawn")

# ...and the final spawn, whose loss hides that actor until its next death.
a.pump(3.0)
b.pump(0.5)
n_final = sum(1 for p, f in a.respawns if p == victim and f & 0x02)
if n_final < 2:
    fail(f"final RESPAWN for pid {victim} sent {n_final} time(s); "
         "one lost packet leaves that actor invisible")

print(f"RESPAWN repeated: pending x{n_pending}, final x{n_final} for pid {victim}")
PYEOF

SERVER_SRC="$ROOT_DIR/server/main.c"
# Every broadcast of a RESPAWN must queue its echo, or a transition goes out
# once again without anyone noticing.
sends=$(grep -c "build_respawn((\*seq)++" "$SERVER_SRC" || true)
echoes=$(grep -c "queue_respawn_echo(" "$SERVER_SRC" || true)
if [ "${echoes:-0}" -lt "$((sends + 1))" ]; then
    echo "FAIL: $sends RESPAWN broadcasts but only $((echoes - 1)) echo(es)" >&2
    exit 1
fi
# One repeat per tick, ahead of the step, like the brick echo -- a burst is what
# loses packets in the first place.
grep -A4 -F "flush_respawn_echo(room, debug);" "$SERVER_SRC" \
    | grep -F "apply_queued_input(" >/dev/null || {
    echo "FAIL: the respawn echo no longer flushes ahead of the step" >&2
    exit 1
}

echo "respawn echo smoke passed"
