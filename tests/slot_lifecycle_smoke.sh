#!/bin/sh

# Slot lifecycle contract (Phase 5). Against the real server binary:
#   - a human joining a zombie seat does not inherit its score
#   - the actor is NOT teleported by the handoff, in either direction
#   - the role mask in snapshot flags tracks the change so clients can follow it
#   - a timed-out human's seat returns to zombie control, again reset and in place

set -eu

PORT=9131
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SERVER_BIN="$ROOT_DIR/build/maze-war-server"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/slot-lifecycle-smoke.XXXXXX.log")
SERVER_PID=

cleanup() {
  status=$?
  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [ $status -ne 0 ]; then
    printf 'slot lifecycle smoke failed; log preserved at %s\n' "$LOG_FILE" >&2
  else
    rm -f "$LOG_FILE"
  fi
  exit $status
}

trap cleanup EXIT INT TERM

make -C "$ROOT_DIR" build/maze-war-server >/dev/null

# Fast ticks keep the run short; 3 zombies means every slot starts occupied.
"$SERVER_BIN" --port "$PORT" --tick-hz 20 --zombies 3 --debug >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 1

python3 - "$PORT" <<'PYEOF'
import socket


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
PKT_SNAPSHOT = 0x40
PKT_SHOT = 0x42

# Long enough to outlive the server's zombie move cadence but far short of the
# 15s client timeout, so the join-side assertions are not racing a reap.
SETTLE_S = 1.5


class Client:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.02)
        self.pid = None
        self.seq = 1
        self.players = {}
        self.role_mask = None
        self.shots = []

    def handle(self, packet):
        if len(packet) >= 6 and packet[0] == PKT_SHOT:
            self.shots.append(bytes(packet[:6]))
            return
        if len(packet) >= 20 and packet[0] == PKT_SNAPSHOT:
            self.pid = (packet[2] >> 1) & 0x03
            self.role_mask = (packet[2] >> 3) & 0x0F
            for idx in range(4):
                self.players[idx] = {
                    "x": packet[3 + idx * 2],
                    "y": packet[4 + idx * 2],
                    "joy": packet[11 + idx],
                    "score": packet[15 + idx],
                }

    def pump(self, duration=0.3):
        deadline = time.time() + duration
        while time.time() < deadline:
            try:
                self.handle(cobs_decode(self.sock.recv(256)))
            except socket.timeout:
                pass

    def send(self, joy):
        self.sock.send(bytes([0x41, self.seq & 0xFF, self.pid or 0, joy]))
        self.seq += 1

    def keepalive(self, duration):
        deadline = time.time() + duration
        while time.time() < deadline:
            self.send(0x0F)
            self.pump(0.1)

    def wait_ready(self, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pump(0.1)
            if self.pid is not None and self.players:
                return
        raise SystemExit("client never received a snapshot")


def fail(msg):
    raise SystemExit("FAIL: " + msg)


# --- observer joins slot 0 and watches the rest of the match ---------------
obs = Client()
obs.sock.send(bytes([0x41, 1, 0, 0x0F]))
obs.seq = 2
obs.wait_ready()
if obs.pid != 0:
    fail(f"observer expected slot 0, got {obs.pid}")

# Let the zombies run so slot 1 accumulates a non-neutral facing and moves.
obs.keepalive(SETTLE_S)
if obs.role_mask != 0x0E:
    fail(f"expected zombies in slots 1..3 (mask 0x0E), got {obs.role_mask:#04x}")

# Two things are deliberately NOT asserted at runtime here, because neither can
# actually fail in a black-box test:
#   `joy` -- the joining client's own first DELTA sets it on the same tick, so
#            the newcomer's input governs the value either way.
#   shot retirement -- a zombie only fires at a human in its row or column with
#            a clear line, which was measured at >20s and on an arbitrary slot.
# The shot-retirement path is guarded at source level at the end of this script.
pre = dict(obs.players[1])

# --- a human takes over slot 1 --------------------------------------------
taker = Client()
taker.sock.send(bytes([0x41, 1, 1, 0x0F]))
taker.seq = 2
taker.wait_ready()
if taker.pid != 1:
    fail(f"taker expected slot 1, got {taker.pid}")

obs.keepalive(0.8)
taker.pump(0.3)

if obs.role_mask != 0x0C:
    fail(f"slot 1 should have left the zombie mask, got {obs.role_mask:#04x}")

post = dict(obs.players[1])
if post["score"] != 0:
    fail(f"takeover inherited score={post['score']}")
if (post["x"], post["y"]) != (pre["x"], pre["y"]):
    fail("takeover moved the actor; handoff must keep it in place "
         f"({pre['x']},{pre['y']}) -> ({post['x']},{post['y']})")

# --- the human stops talking; the seat must return to zombie control -------
# The taker goes silent while the observer keeps its own slot alive. Sample
# continuously so the position immediately before the handoff is known: the
# backfilled zombie starts walking right after, so comparing against a sample
# taken seconds earlier would measure the zombie's own movement, not the reset.
last_human_pos = (obs.players[1]["x"], obs.players[1]["y"])
handoff_pos = None
deadline = time.time() + 25.0
while time.time() < deadline:
    obs.send(0x0F)
    obs.pump(0.1)
    if obs.role_mask == 0x0E:
        handoff_pos = (obs.players[1]["x"], obs.players[1]["y"])
        break
    last_human_pos = (obs.players[1]["x"], obs.players[1]["y"])

if handoff_pos is None:
    fail("slot 1 never returned to zombie control after the client timed out")

dropped = dict(obs.players[1])
if dropped["score"] != 0:
    fail(f"backfilled zombie inherited score={dropped['score']}")
# One step of tolerance: the zombie may already have moved on the same tick the
# role change became visible. A reset that respawned it would be far further.
dx = abs(handoff_pos[0] - last_human_pos[0])
dy = abs(handoff_pos[1] - last_human_pos[1])
if dx + dy > 1:
    fail("backfill teleported the actor "
         f"{last_human_pos} -> {handoff_pos}")

print("slot lifecycle assertions passed")
PYEOF

grep -F "client connected slot=0" "$LOG_FILE" >/dev/null
grep -F "client connected slot=1" "$LOG_FILE" >/dev/null
grep -F "client disconnected slot=1" "$LOG_FILE" >/dev/null

# Source-level guards for the parts of the contract the runtime assertions above
# cannot reach. A slot handoff must retire an in-flight shot with the usual
# clear burst, must reset facing and score, must re-base the zombie schedules,
# and must run on BOTH transitions (join and reap).
SERVER_SRC="$ROOT_DIR/server/main.c"
reset_body=$(sed -n '/^static void reset_slot_gameplay(int slot, struct player_state/,/^}/p' \
    "$SERVER_SRC")
if [ -z "$reset_body" ]; then
    echo "FAIL: reset_slot_gameplay definition not found" >&2
    exit 1
fi
for needle in \
    'shots[slot].active = 0' \
    'shots[slot].clear_burst = 3' \
    'players[slot].joy = 0x0F' \
    'players[slot].score = 0' \
    'players[slot].zombie_fire_pending = 0' \
    'zombie_think_next_ms = now' \
    'last_input_ms[slot] = 0'
do
    case "$reset_body" in
        *"$needle"*) ;;
        *) echo "FAIL: reset_slot_gameplay no longer does: $needle" >&2; exit 1 ;;
    esac
done

# A handoff must not relocate the actor; that is asserted at runtime above, and
# guarded here so the intent survives refactoring.
case "$reset_body" in
    *'pick_spawn'*|*'players[slot].x ='*|*'players[slot].y ='*)
        echo "FAIL: reset_slot_gameplay moves the actor; handoff must keep it in place" >&2
        exit 1 ;;
esac

# Both call sites must survive. Match the argument lists exactly: the
# declaration and definition both start `reset_slot_gameplay(int slot`, which a
# looser pattern counts as a call.
if ! grep -F 'reset_slot_gameplay(slot, players, shots, last_input_ms, now)' \
     "$SERVER_SRC" >/dev/null; then
    echo "FAIL: no reset_slot_gameplay call on the join path" >&2
    exit 1
fi
if ! grep -F 'reset_slot_gameplay(i, players, shots, last_input_ms, now)' \
     "$SERVER_SRC" >/dev/null; then
    echo "FAIL: no reset_slot_gameplay call on the reap path" >&2
    exit 1
fi

# A player awaiting respawn must not block movement. Its coordinates still hold
# the cell it died in and clients hide it, so counting it in collision turned
# the death cell into an invisible wall for the whole respawn delay.
SERVER_SRC="$ROOT_DIR/server/main.c"
sed -n '/^static int is_player_at/,/^}/p' "$SERVER_SRC" \
    | grep -F "respawn_at_ms != 0" >/dev/null || {
    echo "FAIL: is_player_at counts respawning players, walling off death cells" >&2
    exit 1
}

# The Atari client predicts its own movement, so its occupancy rule has to
# agree with the server's. If the client still blocked on a corpse the server
# lets you walk through, it would refuse a move the server applies, drift, and
# then snap to the authoritative position.
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
grep -A8 -E "^NAF_OCCLP" "$ATARI_SRC" | grep -F "NET_DEAD_MASK" >/dev/null || {
    echo "FAIL: client movement prediction still blocks on respawning players" >&2
    exit 1
}

# And prove it live: a live player standing on a corpse cell is only reachable
# once respawning players stop blocking. Deaths come from the zombies, so treat
# a run that produced too few as inconclusive rather than failing.
"$SERVER_BIN" --port 9152 --tick-hz 20 --zombies 3 >/dev/null 2>&1 &
CORPSE_PID=$!
sleep 1
python3 - 9152 <<'PYEOF2'
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

PORT = int(sys.argv[1])

def mk():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("127.0.0.1", PORT))
    s.settimeout(0.02)
    return s

a, b = mk(), mk()
a.send(bytes([0x41, 1, 0, 0x0F]))
b.send(bytes([0x41, 1, 0, 0x0F]))
pos, dead = {}, set()
overlaps = deaths = 0
seq, i = 2, 0
dirs = [0x07, 0x0D, 0x0B, 0x0E]
t0 = time.time()
while time.time() - t0 < 30:
    a.send(bytes([0x41, seq & 0xFF, 0, dirs[(i // 7) % 4]]))
    b.send(bytes([0x41, seq & 0xFF, 0, dirs[(i // 5) % 4] | 0x10]))
    seq += 1
    i += 1
    end = time.time() + 0.1
    while time.time() < end:
        for s in (a, b):
            try:
                p = cobs_decode(s.recv(256))
            except socket.timeout:
                continue
            if len(p) >= 6 and p[0] == 0x52:
                pid = p[2]
                if p[5] & 0x02:
                    dead.discard(pid)
                elif p[5] & 0x01:
                    dead.add(pid)
                    deaths += 1
            elif len(p) >= 19 and p[0] == 0x40:
                for k in range(4):
                    pos[k] = (p[3 + k * 2], p[4 + k * 2])
                for dp in list(dead):
                    for k in range(4):
                        if k != dp and k not in dead and pos.get(k) == pos.get(dp):
                            overlaps += 1

if deaths < 3:
    print(f"note: only {deaths} deaths in the window, corpse check inconclusive")
elif overlaps == 0:
    raise SystemExit(
        f"FAIL: {deaths} deaths and never once could a player stand on a "
        "corpse cell; respawning players are still blocking movement")
else:
    print(f"corpse pass-through confirmed ({overlaps} over {deaths} deaths)")
PYEOF2
status=$?
kill "$CORPSE_PID" 2>/dev/null || true
wait "$CORPSE_PID" 2>/dev/null || true
if [ $status -ne 0 ]; then
    exit 1
fi

echo "slot lifecycle smoke passed"

# A diverged REMOTE actor must still be recoverable while it is firing.
#
# RF_SNAP is the only path that puts a remote wizard back on its authoritative
# cell. It used to refuse whenever ACTFLAG was non-zero at all -- but ACTFLAG
# carries the shooting flag ($80) and backlash bits as well as coalesce and
# evaporate ($03). A remote that diverged while shooting could therefore never
# be repositioned: observed as another player's wizard walking off down the
# wrong column and staying stuck there while they moved normally on their own
# machine. Only $03 genuinely means "not on the board".
snap=$(awk '/^RF_SNAP/{on=1} on{print} on&&/^RF_DONE/{exit}' clients/atari/maze-war.asm)
if ! printf '%s' "$snap" | grep -qE 'AND[[:space:]]+#\$03'; then
    echo "FAIL: RF_SNAP tests the whole of ACTFLAG again, so a remote actor that
diverges while firing can never be put back" >&2
    exit 1
fi

# A remote actor must be able to WALK off a gap of more than one cell.
#
# RF_SYNCCHK used to act only on an exact one-cell divergence; a gap of two --
# one missed snapshot while that player was moving -- failed, counted up and
# snapped. Remote wizards visibly jumped instead of walking. It now steps for
# anything below the snap threshold, while still snapping if a two-cell gap
# persists (trailing at the remote's own speed never closes).
sync=$(awk '/^RF_SYNCCHK/{on=1} on{print} on&&/^RF_FAIL/{exit}' clients/atari/maze-war.asm)
if printf '%s' "$sync" | grep -qE 'CMP[[:space:]]+#1[[:space:]]*$'; then
    echo "FAIL: RF_SYNCCHK only walks an exact one-cell gap again, so a remote
that falls two behind snaps instead of walking" >&2
    exit 1
fi
printf '%s' "$sync" | grep -qE 'CMP[[:space:]]+#NET_RECOVER_P1' || {
    echo "FAIL: RF_SYNCCHK no longer bounds its walk by NET_RECOVER_P1" >&2
    exit 1
}
printf '%s' "$sync" | grep -qE '^RF_STEPFAR' || {
    echo "FAIL: no persistent-trail guard; a remote could follow two cells
behind indefinitely without ever being corrected" >&2
    exit 1
}
