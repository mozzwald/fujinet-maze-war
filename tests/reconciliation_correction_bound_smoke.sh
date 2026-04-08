#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make build/maze-war-client

asm=clients/atari/maze-war.asm

grep -Eq 'NET_RECON_P0' "$asm"
grep -Eq 'JSR[[:space:]]+NET_LOCAL_REPLAY_PENDING' "$asm"
grep -Eq 'NET_LOCAL_REPLAY_STEP' "$asm"
grep -Eq 'INITMOVE_STEP' "$asm"
grep -Eq 'INITMOVE' "$asm"
grep -Eq 'MOVEIM' "$asm"
grep -Eq 'SETSTIL' "$asm"
grep -Eq 'MOVRATE' "$asm"
grep -Eq 'MOVCLOK' "$asm"
grep -Eq 'CHKXDIR' "$asm"
grep -Eq 'CHKYDIR' "$asm"
grep -Eq 'GETAHEDM' "$asm"

if sed -n '/^CKMV_LOC$/,/^CKMVAP/p' "$asm" | grep -Eq 'JMP[[:space:]]+REMOTE_FOLLOW'; then
  echo "local correction still jumps to REMOTE_FOLLOW" >&2
  exit 1
fi

if sed -n '/^NET_LOCAL_REPLAY_STEP$/,/^NLRS_BLK/p' "$asm" | grep -Eq 'STA[[:space:]]+LOC[XY],X'; then
  echo "replay step writes LOCX/LOCY directly instead of using movement seams" >&2
  exit 1
fi

echo "reconciliation correction bound smoke: ok"
