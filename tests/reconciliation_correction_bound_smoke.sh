#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make build/maze-war-client

asm=clients/atari/maze-war.asm

grep -Eq 'NET_RECON_P0' "$asm"
grep -Eq 'JSR[[:space:]]+NET_LOCAL_REPLAY_PENDING' "$asm"
grep -Eq 'NET_LOCAL_REPLAY_STEP' "$asm"
grep -Eq '^NET_AUTH_REPOS$' "$asm"
grep -Eq 'INITMOVE_STEP' "$asm"
grep -Eq 'INITMOVE' "$asm"
grep -Eq 'MOVEIM' "$asm"
grep -Eq 'SETSTIL' "$asm"
grep -Eq 'MOVRATE' "$asm"
grep -Eq 'MOVCLOK' "$asm"
grep -Eq 'CHKXDIR' "$asm"
grep -Eq 'CHKYDIR' "$asm"
grep -Eq 'GETAHEDM' "$asm"
grep -Eq 'NET_GUARD_MASK' "$asm"
grep -Eq 'NET_ERASE_MASK' "$asm"

grep -Eq 'NET_LOCAL_REPLAY_PENDING[\$[:space:][:alnum:]_]*' "$asm"

if ! sed -n '/^NET_LOCAL_REPLAY_PENDING$/,/^NET_AUTH_REPOS/p' "$asm" | grep -Eq 'JSR[[:space:]]+NET_AUTH_REPOS'; then
  echo "local replay does not use shared authoritative reposition helper" >&2
  exit 1
fi

if ! sed -n '/^CKMVAP\b/,/^CKMVCK/p' "$asm" | grep -Eq 'JSR[[:space:]]+NET_AUTH_REPOS'; then
  echo "local correction does not use shared authoritative reposition helper" >&2
  exit 1
fi

if ! sed -n '/^RF_SNAP$/,/^RF_DONE/p' "$asm" | grep -Eq 'JSR[[:space:]]+NET_AUTH_REPOS'; then
  echo "remote snap recovery does not use shared authoritative reposition helper" >&2
  exit 1
fi

helper_block="$(sed -n '/^NET_AUTH_REPOS$/,/^NET_LOCAL_REPLAY_STEP/p' "$asm")"

if ! grep -Eq 'JSR[[:space:]]+ERASMAN' <<<"$helper_block"; then
  echo "shared helper does not erase stale actor state before reposition" >&2
  exit 1
fi

if ! grep -Eq 'NET_GUARD_MASK' <<<"$helper_block"; then
  echo "shared helper does not update guard bookkeeping" >&2
  exit 1
fi

if ! grep -Eq 'NET_ERASE_MASK' <<<"$helper_block"; then
  echo "shared helper does not touch erase bookkeeping" >&2
  exit 1
fi

if sed -n '/^CKMV_LOC$/,/^CKMVAP/p' "$asm" | grep -Eq 'JMP[[:space:]]+REMOTE_FOLLOW'; then
  echo "local correction still jumps to REMOTE_FOLLOW" >&2
  exit 1
fi

if sed -n '/^NET_LOCAL_REPLAY_STEP$/,/^NLRS_BLK/p' "$asm" | grep -Eq 'STA[[:space:]]+LOC[XY],X'; then
  echo "replay step writes LOCX/LOCY directly instead of using movement seams" >&2
  exit 1
fi

echo "reconciliation correction bound smoke: ok"
