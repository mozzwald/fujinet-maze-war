#!/bin/sh

# Guard the connection reliability contract:
#   - a stream that opens but never delivers snapshots must time out
#   - a server that goes silent mid-game must time out the same way
#   - both paths return to the host prompt with an explanation instead of
#     freezing, and NS_INIT failures must not spin through RESTART forever
#   - the TCP listener must reject a second active listener

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
SERVER_SRC="$ROOT_DIR/server/main.c"

TAB=$(printf '\t')

# watchdog state, limit, and the once-per-frame call site
grep -E "NET_WAIT_MAX[$TAB ]*=" "$ATARI_SRC" >/dev/null
grep -E "NET_WAIT_LO[$TAB ]+\.DS" "$ATARI_SRC" >/dev/null
grep -E "NET_WAIT_HI[$TAB ]+\.DS" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+NET_WAIT_TICK" "$ATARI_SRC" >/dev/null

# the counter measures silence: cleared on init and on every accepted snapshot
grep -A3 -F "NSNAP_ACC" "$ATARI_SRC" | grep -E "STA[$TAB ]+NET_WAIT_LO" >/dev/null

# recovery path exists and is reachable from both timeout and init failure
grep -E "^NET_HOSTRET" "$ATARI_SRC" >/dev/null
if [ "$(grep -cE "JMP[$TAB ]+NET_HOSTRET" "$ATARI_SRC")" -lt 2 ]; then
    echo "FAIL: expected both the watchdog and NS_INIT failure to reach NET_HOSTRET" >&2
    exit 1
fi

# recovery must close the stream and stop the game VBI before re-prompting
grep -A40 -E "^NET_HOSTRET" "$ATARI_SRC" | grep -E "JSR[$TAB ]+NET_ENDC" >/dev/null
grep -A40 -E "^NET_HOSTRET" "$ATARI_SRC" | grep -E "JSR[$TAB ]+VBIOFF" >/dev/null
grep -A40 -E "^NET_HOSTRET" "$ATARI_SRC" | grep -E "JSR[$TAB ]+HOST_BOOT" >/dev/null

# player graphics are hidden for the prompt and restored before play resumes
grep -A40 -E "^NET_HOSTRET" "$ATARI_SRC" | grep -E "STA[$TAB ]+GRACTL" >/dev/null

# NS_INIT failures are bounded rather than retried forever through RESTART
grep -E "NET_INIT_TRIES[$TAB ]*=" "$ATARI_SRC" >/dev/null
grep -A8 -E "^NET_INITF" "$ATARI_SRC" | grep -E "CMP[$TAB ]+#NET_INIT_TRIES" >/dev/null

# each failure reason names itself on the host screen
for msg in MSG_CONNECT MSG_NOSRV MSG_LOST MSG_INITFAIL; do
    grep -E "^$msg[$TAB ]+\.BYTE" "$ATARI_SRC" >/dev/null || {
        echo "FAIL: missing status string $msg" >&2
        exit 1
    }
done
grep -E "JSR[$TAB ]+HOST_MSGDRAW" "$ATARI_SRC" >/dev/null

# strings are $FF-terminated because screen code $00 is a space
grep -E "^HOSTPROMPT[$TAB ]+\.BYTE.*\\\$FF" "$ATARI_SRC" >/dev/null
grep -A2 -F "HD_PR${TAB}LDA${TAB}(INPROM),Y" "$ATARI_SRC" | grep -E "CMP[$TAB ]+#\\\$FF" >/dev/null

# TCP allows SO_REUSEADDR for restart after TIME_WAIT. The live duplicate-bind
# and restart assertions are in tcp_transport_smoke.sh.
grep -F 'SOCK_STREAM' "$SERVER_SRC" >/dev/null
grep -F 'listen(sock, MAX_PLAYERS)' "$SERVER_SRC" >/dev/null

# Slot lifecycle resets on the client: a slot that changes role, or a local pid
# that moves, must not keep latches describing the previous occupant.
grep -E "^NET_ROLE_RESET" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+NET_ROLE_RESET" "$ATARI_SRC" >/dev/null
grep -E "^NET_LOCAL_PID_RESET" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+NET_LOCAL_PID_RESET" "$ATARI_SRC" >/dev/null

# The role reset must be driven by the slots that actually changed, not run blind
grep -E "NET_ROLE_CHG[$TAB ]+\.DS" "$ATARI_SRC" >/dev/null
grep -A12 -E "^NET_ROLE_RESET" "$ATARI_SRC" | grep -E "AND[$TAB ]+PLRMSK,X" >/dev/null

# Shot publish latches are dropped (ghost shots); respawn latches deliberately
# are not, because a dropped respawn would leave that actor hidden.
grep -A12 -E "^NET_ROLE_RESET" "$ATARI_SRC" | grep -E "STA[$TAB ]+NET_SHOT_SEQ,X" >/dev/null
if grep -A12 -E "^NET_ROLE_RESET" "$ATARI_SRC" | grep -E "STA[$TAB ]+NET_RESP_SEQ,X"; then
    echo "FAIL: role reset clears respawn latches; a dropped respawn hides the actor" >&2
    exit 1
fi

# A changed local pid must invalidate the prediction ring keyed to the old slot
grep -A8 -E "^NET_LOCAL_PID_RESET" "$ATARI_SRC" | grep -E "STA[$TAB ]+NET_PEND_COUNT" >/dev/null
grep -A8 -E "^NET_LOCAL_PID_RESET" "$ATARI_SRC" | grep -E "STA[$TAB ]+NET_PRED_TTL" >/dev/null

# Map self-healing. BRICK_DELTA is sent once and never acknowledged, so a lost
# one used to desync the client maze permanently: a square drawn blank that
# still blocks, or drawn solid that is walkable. The server re-broadcasts the
# full map periodically and the client applies it as a repair.
grep -E "BRICK_RESYNC_MS" "$SERVER_SRC" >/dev/null
grep -F "TX brick_full resync" "$SERVER_SRC" >/dev/null

# The client must accept later BRICK_FULLs, not discard them.
if grep -A3 -E "^NET_RX_WFULL50" "$ATARI_SRC" | grep -E "BNE[$TAB ]+NET_RX_EXIT"; then
    echo "FAIL: client still ignores BRICK_FULL after the first sync" >&2
    exit 1
fi
# ...and must consume a BRICK_DELTA it will not apply, or its payload bytes get
# rescanned as packet markers (a stray \$51 clears an arbitrary map cell).
if grep -A3 -E "^NET_RX_WBRD51" "$ATARI_SRC" | grep -E "BEQ[$TAB ]+NET_RX_EXIT"; then
    echo "FAIL: client abandons a BRICK_DELTA mid-packet" >&2
    exit 1
fi

# A repair must not repaint cells that already agree, or it would flicker the
# playfield and erase drawn shots every resync.
grep -E "NET_BRICK_RESYNC" "$ATARI_SRC" >/dev/null
grep -A8 -E "^NBF_PUT" "$ATARI_SRC" | grep -E "CMP[$TAB ]+NET_RX_TMP0" >/dev/null

# The outer wall is immutable; the old guards compared against 20/19 after a
# BCS that already excluded those values, so x=0 and y=0 were never rejected.
grep -A4 -E "^NET_BRICK_DELTA_APPLY" "$ATARI_SRC" | grep -E "BEQ[$TAB ]+NBRK_X" >/dev/null

echo "connection recovery smoke passed"
