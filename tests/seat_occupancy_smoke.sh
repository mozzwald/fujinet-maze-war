#!/bin/sh

# Seat occupancy contract (0x44 SEATS). Against the real server binary:
#   - the mask names exactly the slots a client holds
#   - it is broadcast when a client joins, without waiting for the repeat timer
#   - it keeps repeating, so a lost SEATS heals
#   - zombie slots are NOT in it; the snapshot's zombie mask covers those
# Clients need this to tell an empty seat from a human standing still: without
# it a server with two zombies and one player still listed four names and drew
# four wizards.

set -eu

PORT=9171
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/seat-occupancy-smoke.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'seat occupancy smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

"$SERVER_BIN" --port "$PORT" --tick-hz 20 --zombies 2 >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket, sys, time


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
PKT_SNAPSHOT = 0x40
PKT_DELTA = 0x41
PKT_SEATS = 0x44


class C:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.05)
        self.seq = 0
        self.seats = []
        self.zombie_mask = None
        self.pid = None

    def poke(self):
        """Any valid packet claims a slot; a neutral stick moves nobody."""
        self.sock.send(bytes([PKT_DELTA, self.seq, 0, 0x0F]))
        self.seq = (self.seq + 1) & 0xFF

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            try:
                p = cobs_decode(self.sock.recv(256))
            except socket.timeout:
                continue
            if len(p) >= 3 and p[0] == PKT_SEATS:  # trailing checksum byte
                self.seats.append(p[2])
            elif len(p) >= 20 and p[0] == PKT_SNAPSHOT:
                self.pid = (p[2] >> 1) & 0x03
                self.zombie_mask = (p[2] >> 3) & 0x0F


def fail(m):
    raise SystemExit("FAIL: " + m)


a = C()
a.poke()
# The join broadcast must not wait on the 1s repeat timer.
t0 = time.time()
while time.time() - t0 < 1.0 and not a.seats:
    a.pump(0.1)
if not a.seats:
    fail("no SEATS packet after a client joined")
if a.seats[-1] != 0b0001:
    fail(f"one client should be seat mask 0b0001, got {a.seats[-1]:#06b}")

# Zombies fill slots the mask must leave alone: they are not clients.
a.pump(0.5)
if a.zombie_mask is None:
    fail("no snapshot received")
if a.zombie_mask != 0b0110:
    fail(f"expected zombies in slots 1-2, got {a.zombie_mask:#06b}")
if a.seats[-1] & a.zombie_mask:
    fail("a zombie slot was reported as a client seat")

# A second client takes a free slot -- with two zombies already placed that is
# slot 3, since a free non-zombie slot is preferred over displacing the AI.
b = C()
b.poke()
t0 = time.time()
while time.time() - t0 < 2.0:
    a.pump(0.2)
    b.pump(0.2)
    if bin(a.seats[-1]).count("1") == 2:
        break
if bin(a.seats[-1]).count("1") != 2:
    fail(f"second client not in the seat mask: {a.seats[-1]:#06b}")
if not (a.seats[-1] & 0b0001):
    fail(f"the first client lost its seat: {a.seats[-1]:#06b}")
if b.seats and b.seats[-1] != a.seats[-1]:
    fail(f"clients disagree: {a.seats[-1]:#06b} vs {b.seats[-1]:#06b}")

# It repeats, so a lost SEATS heals rather than stranding a blank HUD line.
before = len(a.seats)
a.poke()
a.pump(2.5)
if len(a.seats) - before < 2:
    fail("SEATS is not repeated on its own timer")

print("seat occupancy assertions passed")
PYEOF

SERVER_SRC="$ROOT_DIR/server/main.c"
grep -E "PKT_SEATS = 0x44" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: server has no SEATS packet type" >&2; exit 1; }
grep -E "SEAT_REPEAT_MS" "$SERVER_SRC" >/dev/null || {
    echo "FAIL: SEATS has no repeat timer" >&2; exit 1; }
# A timed-out seat must be reported free on the same pass that frees it, so the
# broadcast has to sit after the reap, not before it.
grep -A12 -F "reap_timed_out_clients(clients, now_ms(), debug, players, shots," \
    "$SERVER_SRC" | grep -F "compute_seat_mask(clients)" >/dev/null || {
    echo "FAIL: seat mask is not recomputed after the client reap" >&2; exit 1; }

# Atari client: parses 0x44, and an unheld slot gets no label and no score.
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
TAB=$(printf '\t')
grep -E "^NET_RX_44DONE" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: Atari client ignores SEATS" >&2; exit 1; }
grep -E "^NET_SEAT_HAS" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: Atari client has no seat test" >&2; exit 1; }
grep -E "^NET_LBL_BLANK" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: Atari client cannot blank an empty HUD line" >&2; exit 1; }
# the label pass consults the seat mask before falling back to WIZARD
grep -A6 -E "^NSLBLP" "$ATARI_SRC" | grep -E "JSR[$TAB ]+NET_SEAT_HAS" >/dev/null || {
    echo "FAIL: HUD labels do not consult the seat mask" >&2; exit 1; }
# ...and the per-snapshot score pass must not paint a 0 back over a blank line
grep -A8 -E "^NSNAP_SCORE" "$ATARI_SRC" | grep -E "JSR[$TAB ]+NET_SEAT_HAS" >/dev/null || {
    echo "FAIL: snapshot scores ignore the seat mask" >&2; exit 1; }
# our own slot counts as occupied even before the first SEATS arrives
grep -A6 -E "^NET_SEAT_HAS" "$ATARI_SRC" | grep -F "NET_STAGE_LOCAL_PID" >/dev/null || {
    echo "FAIL: the local slot is not assumed occupied" >&2; exit 1; }
# an unheld slot must not stand on the board either
grep -E "^NET_VACANT_UPDATE" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: Atari client still draws a wizard in an empty seat" >&2; exit 1; }
# it hides through the same dead/erase masks a respawn uses...
grep -A24 -E "^NET_VACANT_UPDATE" "$ATARI_SRC" | grep -F "NET_DEAD_MASK" >/dev/null || {
    echo "FAIL: vacant slots are not hidden through NET_DEAD_MASK" >&2; exit 1; }
grep -A24 -E "^NET_VACANT_UPDATE" "$ATARI_SRC" | grep -F "NET_ERASE_MASK" >/dev/null || {
    echo "FAIL: hiding a vacant slot never erases what is drawn" >&2; exit 1; }
# ...but tracks what it hid, so filling a seat cannot reveal an actor that is
# genuinely awaiting respawn
grep -E "^NET_VACANT_MASK" "$ATARI_SRC" >/dev/null || {
    echo "FAIL: no record of which slots were hidden as vacant" >&2; exit 1; }
# It runs every VBI, ahead of the move loop that performs the erase -- not only
# on a HUD refresh. See vacant_slot_collision_smoke.sh, which pins the placement
# and the sprite flash that gating it on NET_SCORE_PEND caused.

# Both Linux clients honour the same mask.
for c in "$ROOT_DIR/clients/linux/main.c" "$ROOT_DIR/clients/linux/sdl_main.c"; do
    grep -E "PKT_SEATS = 0x44" "$c" >/dev/null || {
        echo "FAIL: $c does not know PKT_SEATS" >&2; exit 1; }
    grep -F "seat_mask" "$c" >/dev/null || {
        echo "FAIL: $c does not track the seat mask" >&2; exit 1; }
    # the same test gates the HUD line and the sprite, so they cannot diverge
    grep -F "slot_in_play" "$c" >/dev/null || {
        echo "FAIL: $c still draws an actor in an empty seat" >&2; exit 1; }
    if [ "$(grep -c "slot_in_play(" "$c")" -lt 3 ]; then
        echo "FAIL: $c applies the seat test to only one of HUD/board" >&2
        exit 1
    fi
done

echo "seat occupancy smoke passed"
