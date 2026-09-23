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
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
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
NAME_LEN = 8
PKT_NAME = 0x43


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.05)
        self.names = {}

    def name(self, pid, text):
        n = text.ljust(NAME_LEN)[:NAME_LEN].encode("latin1")
        send_frame(self.sock, bytes([PKT_NAME, 1, pid]) + n)

    def pump(self, secs=0.8):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = cobs_decode(recv_frame(self.sock, 256))
            except socket.timeout:
                continue
            if len(p) >= 3 + NAME_LEN and p[0] == PKT_NAME:  # CRC trailer stripped by recv_frame
                self.names[p[2]] = bytes(p[3:3 + NAME_LEN]).decode("latin1")


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

# The rotation announces one slot per second, so give it a full cycle rather
# than assuming everything has landed.
deadline = time.time() + 8.0
while time.time() < deadline:
    a.pump(0.4)
    b.pump(0.4)
    named = [p for p in a.names if a.names[p].strip()]
    if a.names.get(0, "").strip() and len(named) >= 2:
        break

if a.names.get(0) != "MOZZWALD":
    fail(f"slot 0 name wrong: {a.names.get(0)!r}")

other = [p for p in a.names if p != 0 and a.names[p].strip()]
if not other:
    fail("second client's name never broadcast")
pid_b = other[0]
if pid_b == 0:
    fail("a client renamed slot 0 by spoofing pid")
if a.names[pid_b] != "JEN     ":
    fail(f"name not sanitized/padded: {a.names[pid_b]!r}")
# Compare only slots both clients have actually heard about.
shared = set(a.names) & set(b.names)
for p in sorted(shared):
    if a.names[p] != b.names[p]:
        fail(f"clients disagree on slot {p}: {a.names[p]!r} vs {b.names[p]!r}")
if not shared:
    fail("clients share no name state at all")

# a late joiner must be told the names already in play
c = C()
c.name(0, "LATE")
deadline = time.time() + 8.0
while time.time() < deadline:
    c.pump(0.4)
    if c.names.get(0, "").strip():
        break
if c.names.get(0) != "MOZZWALD":
    fail(f"joining client not told existing names: {c.names}")

print("player name assertions passed")
PYEOF

grep -F 'NAME slot=0 name="MOZZWALD"' "$LOG_FILE" >/dev/null

# The name lives with the rest of the slot's transient state, so the handoff
# reset must drop it too.
SERVER_SRC="$ROOT_DIR/server/main.c"
grep -F 'memset(client->name, 0, NAME_LEN)' "$SERVER_SRC" >/dev/null || {
    echo "FAIL: claiming a slot does not clear the previous occupant's name" >&2
    exit 1
}

# Names must be repeated so a lost NAME heals, and must go out one at a time.
# Bursting them behind the 51-byte BRICK_FULL made the Atari drop the map.
grep -F "broadcast_next_name(room, debug)" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: names are not re-broadcast" >&2
    exit 1
}
grep -F "broadcast_reliable_event(clients, pkt, sizeof(pkt), seq, now_ms(), debug)" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: name rotation is not mirrored into the reliable event stream" >&2
    exit 1
}
# ...and on their own slow timer, not once per tick. At tick rate the name
# packet doubled the inbound packet count and starved BRICK_DELTA, which made
# destroyed bricks linger until the next full resync.
grep -E "NAME_ROTATE_MS" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: name rotation has no slow timer" >&2
    exit 1
}
# The one call site must sit inside the NAME_ROTATE_MS timer block, not the
# game tick. Anchor on the timer assignment that immediately precedes it.
if ! grep -B2 -F "broadcast_next_name(room, debug)" \
     "$SERVER_SRC" | grep -F "room->last_name_rotate_ms = now;" >/dev/null; then
    echo "FAIL: name rotation is not driven by its own timer" >&2
    exit 1
fi
if [ "$(grep -c "broadcast_next_name(room, debug);" "$SERVER_SRC")" != "1" ]; then
    echo "FAIL: expected exactly one name rotation call site" >&2
    exit 1
fi
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
# Stored HUD names are outside NET_STATE_CLEAR's zeroed block; START must blank
# them to spaces so stale RAM cannot render junk before name packets arrive.
grep -A70 -E "^START" "$ATARI_SRC" | grep -E "JSR[$TAB ]+NET_NAME_CLR" >/dev/null || {
    echo "FAIL: START does not blank stored HUD names" >&2; exit 1; }
# Fallback labels are plain ASCII. The old pre-colored variants made zombie slot
# 2 start at a lowercase/precolored byte and render as a semicolon-like glyph.
grep -F 'PLRTXT	.BYTE	87,73,90,65,82,68' "$ATARI_SRC" >/dev/null || {
    echo "FAIL: WIZARD fallback label is not clean ATASCII" >&2; exit 1; }
grep -F 'ZOMTXT	.BYTE	90,79,77,66,73,69' "$ATARI_SRC" >/dev/null || {
    echo "FAIL: ZOMBIE fallback label is not clean ATASCII" >&2; exit 1; }
grep -F 'NAMECOL	.BYTE	$40,$40,$40,$40' "$ATARI_SRC" >/dev/null || {
    echo "FAIL: HUD names are not all blue text" >&2; exit 1; }
# zombie slots keep their ZOMBIE label whatever name is stored
grep -A12 -E "^NSLBLP" "$ATARI_SRC" | grep -F "ZOMTXT" >/dev/null

# Both Linux clients speak the same NAME contract as the Atari.
for c in "$ROOT_DIR/clients/linux/main.c" "$ROOT_DIR/clients/linux/sdl_main.c"; do
    grep -E "PKT_NAME = 0x43" "$c" >/dev/null || {
        echo "FAIL: $c does not know PKT_NAME" >&2; exit 1; }
    grep -E "NAME_LEN = 8" "$c" >/dev/null || {
        echo "FAIL: $c name length disagrees with the HUD field" >&2; exit 1; }
    # renders the name, falling back to the role label
    grep -F "name_is_set" "$c" >/dev/null || {
        echo "FAIL: $c does not fall back to the role label" >&2; exit 1; }
    grep -F "ZOMBIE" "$c" >/dev/null || {
        echo "FAIL: $c lost the ZOMBIE label" >&2; exit 1; }
    # re-sends until the server echoes the name back
    grep -F "NAME_RESEND_MS" "$c" >/dev/null || {
        echo "FAIL: $c does not retry its name" >&2; exit 1; }
done

# Atari: the text fields show where typing lands.
grep -E "^TXT_CURSOR" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: no text cursor on the Atari prompts" >&2; exit 1; }
grep -A2 -E "^HI_LOOP" "$ATARI_SRC" | grep -E "JSR[$TAB ]+TXT_CURSOR" >/dev/null || {
    echo "FAIL: cursor is not driven from the key wait loop" >&2; exit 1; }
# leaving a field must not strand a block on it
grep -A2 -E "^HI_DONE" "$ATARI_SRC" | grep -E "JSR[$TAB ]+TXT_CUROFF" >/dev/null || {
    echo "FAIL: cursor is left behind when the field loses focus" >&2; exit 1; }

# A destroyed brick is announced more than once. Losing the single packet used
# to leave the wall painted until the next full resync, seconds later.
grep -E "BRICK_ECHO_REPEATS" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: brick destruction is still announced once and forgotten" >&2
    exit 1
}
grep -F "queue_brick_echo(" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: no brick echo queue" >&2; exit 1; }
# every break site must register an echo
breaks=$(grep -c "clear_brick(" "$SERVER_SRC" || true)
echoes=$(grep -c "queue_brick_echo(" "$SERVER_SRC" || true)
if [ "${echoes:-0}" -lt 4 ]; then
    echo "FAIL: a brick break site does not queue an echo ($echoes for $breaks)" >&2
    exit 1
fi
# echoes go out one per tick, ahead of the step, so a break and its echo never
# share a tick
grep -A8 -F "flush_brick_echo(room, debug);" "$SERVER_SRC" \
    | grep -F "step_players(" >/dev/null || {
    echo "FAIL: brick echo no longer flushes before the step" >&2
    exit 1
}

echo "player names smoke passed"
