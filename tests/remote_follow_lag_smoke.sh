#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
SERVER_SRC="$ROOT_DIR/server/main.c"

make -C "$ROOT_DIR" build/maze-war-client build/maze-war-server >/dev/null

fail() { echo "FAIL: $*" >&2; exit 1; }

grep -Eq '^NET_FRAME_DIV[[:space:]]*=[[:space:]]*6\b' "$ATARI_SRC" \
  || fail "client no longer sends at the 10 Hz NTSC/server cadence"

setallp=$(awk '$1=="SETALLP"{on=1} on{print} on&&/STA[[:space:]]+MOVCLOK,X/{exit}' "$ATARI_SRC")
printf '%s' "$setallp" | grep -Eq 'LDA[[:space:]]+#1' \
  || fail "actor animation is back below the 10 Hz network movement budget"

rf=$(awk '$1=="RF_SYNCCHK"{on=1} on{print} on&&/^RF_DONE/{exit}' "$ATARI_SRC")
printf '%s' "$rf" | grep -Eq 'STA[[:space:]]+NET_RF_DIST' \
  || fail "REMOTE_FOLLOW does not save the render gap in dedicated scratch"
printf '%s' "$rf" | grep -Eq 'LDA[[:space:]]+#0[[:space:]]*$' \
  || fail "REMOTE_FOLLOW rightward fallback no longer loads direction 0"
printf '%s' "$rf" | grep -Eq 'JMP[[:space:]]+RF_1SET' \
  || fail "REMOTE_FOLLOW rightward fallback can fall through into left again"
if printf '%s' "$rf" | awk '
  /JSR[[:space:]]+NET_AHEAD_FREE_RND/ {after=1}
  after && /LDA[[:space:]]+NET_RX_TMP[[:space:]]/ {bad=1}
  /^RF_STEPFAR/ {exit}
  END {exit bad ? 0 : 1}
'; then
  fail "REMOTE_FOLLOW reads NET_RX_TMP after NET_AHEAD_FREE_RND clobbers it"
fi
printf '%s' "$rf" | grep -Eq 'LDA[[:space:]]+#\$08' \
  || fail "RF_SNAP is no longer counted as a remote correction diagnostic"

replay=$(awk '$1=="NET_LOCAL_REPLAY_PENDING"{on=1} on{print} on&&/^NLRP_X/{exit}' "$ATARI_SRC")
printf '%s' "$replay" | grep -Eq 'LDA[[:space:]]+NET_PEND_COUNT' \
  || fail "local replay no longer reads the pending input count"
printf '%s' "$replay" | grep -Eq 'BEQ[[:space:]]+NLRP_X' \
  || fail "local replay can underflow or skip incorrectly when the ring is empty"
if printf '%s' "$replay" | grep -Eq 'STA[[:space:]]+NET_REPLAY_MOVE[[:space:]]*$[[:space:]]*BEQ[[:space:]]+NLRP_X'; then
  fail "local replay once again exits immediately after clearing NET_REPLAY_MOVE"
fi

grep -Eq 'next_tick[[:space:]]*=[[:space:]]*tick_deadline[[:space:]]*\+[[:space:]]*tick_ms' "$SERVER_SRC" \
  || fail "server tick scheduling is back to drifting from now instead of fixed deadlines"

echo "remote follow lag smoke: ok"
