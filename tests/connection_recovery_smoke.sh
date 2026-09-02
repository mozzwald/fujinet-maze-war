#!/bin/sh

# Guard the connection reliability contract:
#   - a stream that opens but never delivers snapshots must time out
#   - a server that goes silent mid-game must time out the same way
#   - both paths return to the host prompt with an explanation instead of
#     freezing, and NS_INIT failures must not spin through RESTART forever
#   - the server must not share its UDP port with another binder

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
grep -A2 -F "HD_PR${TAB}LDA${TAB}HOSTPROMPT,Y" "$ATARI_SRC" | grep -E "CMP[$TAB ]+#\\\$FF" >/dev/null

# the server must fail loudly instead of silently sharing the port with
# FujiNet-PC's netstream socket, which swallowed the client's datagrams
if grep -E "SO_REUSEADDR" "$SERVER_SRC" | grep -v '^\s*/\*' | grep -q "setsockopt"; then
    echo "FAIL: server re-enabled SO_REUSEADDR on the game socket" >&2
    exit 1
fi
grep -F "Another process already holds that port" "$SERVER_SRC" >/dev/null

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

echo "connection recovery smoke passed"
