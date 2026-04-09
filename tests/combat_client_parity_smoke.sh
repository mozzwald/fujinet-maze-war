#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

make -C "$ROOT_DIR" build/maze-war-client >/dev/null

grep -F "NET_STAGE_COMMIT" "$ATARI_SRC" >/dev/null
grep -F "NET_RESP_COMMIT" "$ATARI_SRC" >/dev/null
grep -F "NET_RESP_APPLY_WRK" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_APPLY_PEND" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_APPLY" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_VISIBLE_ORIGIN" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_PEND" "$ATARI_SRC" >/dev/null
rg -n 'CMP[[:space:]]+NET_LOCAL_PID' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_PX_X,X' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_PX_Y,X' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_PJOY,X' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_DEAD_MASK' "$ATARI_SRC" >/dev/null
rg -n 'ORA[[:space:]]+NET_ERASE_MASK' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_RESP_WRK\+5' "$ATARI_SRC" >/dev/null
rg -n 'LDA[[:space:]]+NET_SNAP_BUF\+15,X' "$ATARI_SRC" >/dev/null
