#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make build/maze-war-client

asm=clients/atari/maze-war.asm

grep -Eq '^NET_TX_BUILD_DELTA$' "$asm"
grep -Eq '^NET_LOCAL_INPUT_PUSH$' "$asm"
grep -Eq '^CHKTRIG[[:space:]]+LDA[[:space:]]+#0' "$asm"
grep -Eq '^NET_LOCAL_REPLAY_PENDING$' "$asm"
grep -Eq '^NET_LOCAL_REPLAY_STEP$' "$asm"
grep -Eq 'INITSHOT' "$asm"
grep -Eq 'INITMOVE_STEP' "$asm"
grep -Eq 'SETSTIL' "$asm"

tx_block="$(sed -n '/^NET_TX_BUILD_DELTA$/,/^NET_LOCAL_INPUT_PUSH/p' "$asm")"
chktrig_block="$(sed -n '/^CHKTRIG\b/,/^CHKTRG_BLK/p' "$asm")"
replay_pending_block="$(sed -n '/^NET_LOCAL_REPLAY_PENDING$/,/^NET_AUTH_REPOS/p' "$asm")"
replay_step_block="$(sed -n '/^NET_LOCAL_REPLAY_STEP$/,/^NET_RESP_COMMIT/p' "$asm")"

if ! grep -Fq 'ORA	#$10' <<<"$tx_block"; then
  echo "delta builder no longer preserves trigger-bearing joy bytes" >&2
  exit 1
fi

if ! grep -Eq 'LDA[[:space:]]+NET_RX_TRIG' <<<"$chktrig_block"; then
  echo "local fire parsing no longer reads the debounced trigger latch" >&2
  exit 1
fi

if ! grep -Eq 'JMP[[:space:]]+INITSHOT' <<<"$chktrig_block"; then
  echo "local fire parsing no longer routes trigger presses through INITSHOT" >&2
  exit 1
fi

if ! grep -Eq 'JMP[[:space:]]+INITMOVE' <<<"$chktrig_block"; then
  echo "local fire parsing no longer routes movement intent through INITMOVE" >&2
  exit 1
fi

if ! grep -Eq 'STA[[:space:]]+NET_REPLAY_MOVE' <<<"$replay_pending_block"; then
  echo "replay pending loop does not track final movement intent" >&2
  exit 1
fi

if ! grep -Eq 'JSR[[:space:]]+INITMOVE_STEP' <<<"$replay_pending_block"; then
  echo "replay pending loop no longer restores movement through INITMOVE_STEP" >&2
  exit 1
fi

if ! grep -Fq 'AND	#$10' <<<"$replay_step_block"; then
  echo "replay step does not guard trigger-bearing input before movement replay" >&2
  exit 1
fi

if grep -Eq 'JSR[[:space:]]+INITMOVE_STEP' <<<"$replay_step_block"; then
  echo "trigger-aware replay step still reaches INITMOVE_STEP directly" >&2
  exit 1
fi

if ! grep -Eq 'JSR[[:space:]]+SETSTIL' <<<"$replay_step_block"; then
  echo "replay step no longer restores facing through SETSTIL" >&2
  exit 1
fi

echo "reconciliation fire input smoke: ok"
