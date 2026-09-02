#!/bin/sh

# Player name contract. Against the real server binary:
#   - a client names its own slot, and every client is told
#   - names are sanitized to what the Atari charset can draw
#   - a client cannot rename another slot by lying about pid
#   - a joining client is told the names already in play
#   - a slot handoff clears the name

set -eu

PORT=9151
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/player-names-smoke.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'player names smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
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
import socket, sys, time

port = int(sys.argv[1])
NAME_LEN = 8
PKT_NAME = 0x43


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.05)
        self.names = {}

    def name(self, pid, text):
        n = text.ljust(NAME_LEN)[:NAME_LEN].encode("latin1")
        self.sock.send(bytes([PKT_NAME, 1, pid]) + n)

    def pump(self, secs=0.8):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = self.sock.recv(256)
            except socket.timeout:
                continue
            if len(p) == 3 + NAME_LEN and p[0] == PKT_NAME:
                self.names[p[2]] = bytes(p[3:]).decode("latin1")


def fail(m):
    raise SystemExit("FAIL: " + m)


a = C()
a.name(0, "mozzwald")
a.pump(0.6)

# junk bytes and a lowercase name, while claiming to be slot 0
b = C()
b.name(0, "j\x01e\xffn<>!")
a.pump(0.8)
b.pump(0.8)

if a.names.get(0) != "MOZZWALD":
    fail(f"slot 0 name wrong: {a.names.get(0)!r}")

other = [p for p in a.names if p != 0]
if not other:
    fail("second client's name never broadcast")
pid_b = other[0]
if pid_b == 0:
    fail("a client renamed slot 0 by spoofing pid")
if a.names[pid_b] != "JEN     ":
    fail(f"name not sanitized/padded: {a.names[pid_b]!r}")
if a.names != b.names:
    fail(f"clients disagree: {a.names} vs {b.names}")

# a late joiner must be told the names already in play
c = C()
c.name(0, "LATE")
c.pump(1.0)
if c.names.get(0) != "MOZZWALD":
    fail(f"joining client not told existing names: {c.names}")

print("player name assertions passed")
PYEOF

grep -F 'NAME slot=0 name="MOZZWALD"' "$LOG_FILE" >/dev/null

# The name lives with the rest of the slot's transient state, so the handoff
# reset must drop it too.
SERVER_SRC="$ROOT_DIR/server/main.c"
grep -F 'memset(clients[i].name, 0, NAME_LEN)' "$SERVER_SRC" >/dev/null || {
    echo "FAIL: claiming a slot does not clear the previous occupant's name" >&2
    exit 1
}

# Names must be repeated so a lost NAME heals, and must go out one at a time.
# Bursting them behind the 51-byte BRICK_FULL made the Atari drop the map.
grep -F "broadcast_next_name(sock, clients, &seq, &name_rotate)" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: names are not re-broadcast on the tick rotation" >&2
    exit 1
}
if grep -A6 -E "build_brick_full\(seq\+\+, brick_bits, bfull" "$SERVER_SRC" \
    | grep -E "broadcast_(names|next_name)"; then
    echo "FAIL: name packets burst alongside BRICK_FULL; that loses the map" >&2
    exit 1
fi

# Client side: renders names in the HUD, retries until echoed, 8 chars.
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
TAB=$(printf '\t')
grep -E "NAME_LEN[$TAB ]*=[$TAB ]*8" "$ATARI_SRC" >/dev/null
grep -E "^NET_NAME_DRAW" "$ATARI_SRC" >/dev/null
grep -E "^NET_NAME_RETRY" "$ATARI_SRC" >/dev/null
grep -E "^NET_TX_BUILD_NAME" "$ATARI_SRC" >/dev/null
# zombie slots keep their ZOMBIE label whatever name is stored
grep -A5 -E "^NSLBLP" "$ATARI_SRC" | grep -F "ZOMTXT" >/dev/null

echo "player names smoke passed"
