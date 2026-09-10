#!/bin/sh

# One delta per predicted cell.
#
# The server drains at most one queued input per tick and drops overflow without
# acking it, so the client must not put more movement deltas on the wire than
# the server can apply. It also must not run slower than the server on a clean
# NTSC path: that creates empty authoritative ticks and visible 100/200 ms
# remote movement gaps even when TCP delivery is steady.
#
# The invariant this pins: deltas leave only from the periodic slot, whose
# period equals the predicted-cell period, so sends and predicted cells stay
# one to one.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

TAB=$(printf '\t')

# A predicted cell is NET_FRAME_DIV frames. INITMOVE_STEP runs phase 0 at once,
# then MOVEST cycles the remaining three phases at MOVRATE frames each. The
# renderer must be able to finish a cell before the next transmit grant, while
# the transmit grant itself matches the server's 10 Hz tick on NTSC.
frame_div=$(grep -E "^NET_FRAME_DIV${TAB}+=" "$ATARI_SRC" | awk '{print $3}')
movrate=$(grep -B1 -E "STA${TAB}+MOVRATE,X" "$ATARI_SRC" | grep -E "LDA${TAB}+#" | head -1 | sed 's/.*#//' | awk '{print $1}')
anim=$(( (4 - 1) * movrate ))
if [ "$frame_div" -lt "$anim" ]; then
    echo "FAIL: NET_FRAME_DIV=$frame_div is shorter than the $anim-frame cell" \
         "animation; a cell would be gated before it finished" >&2
    exit 1
fi
if [ "$frame_div" -ne 6 ]; then
    echo "FAIL: NET_FRAME_DIV=$frame_div is not the NTSC 10 Hz/server cadence;" \
         "clean TCP then produces either empty authoritative ticks or input" \
         "pressure above the server movement rate" >&2
    exit 1
fi

# Two transmit sites only: the periodic keepalive, and PLRMVE. The PLRMVE one
# must be gated on the stick compare -- that is what ties the packet to the
# decision it describes. A third site keyed to raw input edges is what used to
# overrun the server's one-input-per-tick drain.
delta_calls=$(grep -cE "JSR${TAB}+NET_TX_BUILD_DELTA" "$ATARI_SRC")
if [ "$delta_calls" -ne 1 ]; then
    echo "FAIL: expected exactly 1 JSR NET_TX_BUILD_DELTA (the periodic slot)," \
         "found $delta_calls. Extra send sites either overrun the server drain" \
         "or, if tied to the move commit, starve it during the move animation" >&2
    exit 1
fi

# The poll loop must not transmit on a raw input edge of its own any more.
if sed -n '/^NET_POLL[[:space:]]/,/^NP_TICK$/p' "$ATARI_SRC" | grep -qE "JSR${TAB}+NET_TX_BUILD_DELTA"; then
    echo "FAIL: the poll loop transmits on an input edge again" >&2
    exit 1
fi

# With edges no longer transmitting, a trigger tap between two slots would fall
# between samples. It has to be latched at sample time and consumed by the send.
grep -F "NET_TRIG_LATCH" "$ATARI_SRC" >/dev/null
grep -A12 -E "^NSI_TROK" "$ATARI_SRC" | grep -F "NET_TRIG_LATCH" >/dev/null
if ! grep -A12 -E "^NTB_TRIGUP" "$ATARI_SRC" | grep -F "NET_TRIG_LATCH" >/dev/null; then
    echo "FAIL: NET_TX_BUILD_DELTA does not consume the trigger latch;" \
         "a fire between periodic slots would be lost" >&2
    exit 1
fi

# The grant must be raised at the transmit slot itself, not merely somewhere in
# the poll loop: only a DELTA that actually left the machine may license a cell.
# NET_TX_BUILD_DELTA is skipped when the transmitter is busy, so the store has
# to sit after the JSR to inherit that skip.
if ! grep -A3 -E "JSR${TAB}+NET_TX_BUILD_DELTA" "$ATARI_SRC" | grep -qE "STA${TAB}+NET_MOVE_DUE"; then
    echo "FAIL: the transmit slot does not grant NET_MOVE_DUE. Without the" \
         "grant the local move decision can never run, or -- if the grant" \
         "moved earlier -- a skipped send still licenses a predicted cell" >&2
    exit 1
fi

# And consumed by the local move decision, which is what keeps the two clocks
# in phase. NET_FRAME_DIV and the MOVRATE cell period are the same rate but
# free-run independently; the gate is the only thing tying their phase.
if ! sed -n '/^STRTMOV[[:space:]]/,/^PLRMVE[[:space:]]/p' "$ATARI_SRC" \
     | grep -qE "LDA${TAB}+NET_MOVE_DUE"; then
    echo "FAIL: the local move decision no longer consumes NET_MOVE_DUE;" \
         "move decisions and transmit slots are free-running in phase again" >&2
    exit 1
fi

echo "input send cadence smoke passed"
