#!/bin/sh

# One delta per predicted cell.
#
# The server drains at most one queued input per tick and drops the overflow
# without acking it, so the client must not put more deltas on the wire than
# the server can apply. It used to send on every stick edge as well as on the
# periodic slot: 60 rapid direction changes produced 18 applied and 33 dropped,
# and every dropped input was a cell the client had already predicted. Nothing
# reconverges that while the player is moving, so drift climbed past
# NET_RECON_P0 and snapped the wizard back mid-turn.
#
# The invariant this pins: deltas leave only from the periodic slot, whose
# period equals the predicted-cell period, so sends and predicted cells stay
# one to one.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

TAB=$(printf '\t')

# A predicted cell is NET_FRAME_DIV frames: INITMOVE_STEP runs phase 0 at once,
# then MOVEST cycles the remaining three phases at MOVRATE frames each. Both
# come off the same frame counter, so the two rates only stay locked while
# NET_FRAME_DIV == (4 - 1) * MOVRATE.
frame_div=$(grep -E "^NET_FRAME_DIV${TAB}+=" "$ATARI_SRC" | awk '{print $3}')
movrate=$(grep -B1 -E "STA${TAB}+MOVRATE,X" "$ATARI_SRC" | grep -E "LDA${TAB}+#" | head -1 | sed 's/.*#//' | awk '{print $1}')
if [ "$frame_div" != "6" ] || [ "$movrate" != "2" ]; then
    echo "FAIL: cadence changed (NET_FRAME_DIV=$frame_div MOVRATE=$movrate);" \
         "sends are no longer one per predicted cell" >&2
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

echo "input send cadence smoke passed"
