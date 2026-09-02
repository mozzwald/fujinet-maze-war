#!/bin/sh

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
LINUX_MAIN="$ROOT_DIR/clients/linux/main.c"
LINUX_SDL="$ROOT_DIR/clients/linux/sdl_main.c"

make -C "$ROOT_DIR" build/maze-war-client build/maze-war-client-sdl >/dev/null

grep -F "NET_STAGE_COMMIT" "$ATARI_SRC" >/dev/null
grep -F "NET_RESP_COMMIT" "$ATARI_SRC" >/dev/null
grep -F "NET_RESP_APPLY_WRK" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_APPLY_PEND" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_APPLY" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_VISIBLE_ORIGIN" "$ATARI_SRC" >/dev/null
grep -F "NET_SHOT_PEND" "$ATARI_SRC" >/dev/null
grep -E 'CMP[[:space:]]+NET_LOCAL_PID' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_PX_X,X' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_PX_Y,X' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_PJOY,X' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_DEAD_MASK' "$ATARI_SRC" >/dev/null
grep -E 'ORA[[:space:]]+NET_ERASE_MASK' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_RESP_WRK\+5' "$ATARI_SRC" >/dev/null
grep -E 'LDA[[:space:]]+NET_SNAP_BUF\+15,X' "$ATARI_SRC" >/dev/null

grep -F "PKT_SNAPSHOT = 0x40" "$LINUX_MAIN" >/dev/null
grep -F "PKT_SHOT = 0x42" "$LINUX_MAIN" >/dev/null
grep -F "PKT_BRICK_DELTA = 0x51" "$LINUX_MAIN" >/dev/null
grep -F "PKT_RESPAWN = 0x52" "$LINUX_MAIN" >/dev/null
grep -E 'buf\[0\] == PKT_BRICK_DELTA' "$LINUX_MAIN" >/dev/null
grep -E 'buf\[0\] == PKT_RESPAWN' "$LINUX_MAIN" >/dev/null
grep -E 'buf\[0\] == PKT_SHOT' "$LINUX_MAIN" >/dev/null
grep -E 'buf\[0\] == PKT_SNAPSHOT' "$LINUX_MAIN" >/dev/null
grep -E 'players\[rp\]\.x = 255' "$LINUX_MAIN" >/dev/null
grep -E 'players\[0\]\.score = buf\[15\]' "$LINUX_MAIN" >/dev/null

grep -F "PKT_SNAPSHOT = 0x40" "$LINUX_SDL" >/dev/null
grep -F "PKT_SHOT = 0x42" "$LINUX_SDL" >/dev/null
grep -F "PKT_BRICK_DELTA = 0x51" "$LINUX_SDL" >/dev/null
grep -F "PKT_RESPAWN = 0x52" "$LINUX_SDL" >/dev/null
grep -E 'buf\[0\] == PKT_BRICK_DELTA' "$LINUX_SDL" >/dev/null
grep -E 'buf\[0\] == PKT_RESPAWN' "$LINUX_SDL" >/dev/null
grep -E 'buf\[0\] == PKT_SHOT' "$LINUX_SDL" >/dev/null
grep -E 'buf\[0\] == PKT_SNAPSHOT' "$LINUX_SDL" >/dev/null
grep -E 'clear_brick_cell\(g, x, y\)' "$LINUX_SDL" >/dev/null
grep -E 'g->shots\[pid\]\.dir = \(uint8_t\)\(\(flags >> 1\) & 0x03\)' "$LINUX_SDL" >/dev/null
grep -E 'flags & 0x02' "$LINUX_SDL" >/dev/null
grep -E 'flags & 0x01' "$LINUX_SDL" >/dev/null
grep -E 'g->players\[0\]\.score = buf\[15\]' "$LINUX_SDL" >/dev/null
