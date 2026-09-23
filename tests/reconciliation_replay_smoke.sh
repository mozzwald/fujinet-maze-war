#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

make build/maze-war-client

asm=clients/atari/maze-war.asm

grep -Eq 'NET_LOCAL_INPUT_PUSH' "$asm"
grep -Eq 'NET_LOCAL_ACK_DISCARD' "$asm"
grep -Eq 'NET_LOCAL_REPLAY_PENDING' "$asm"
grep -Eq 'NET_PEND_SEQ[[:space:]]+\.DS[[:space:]]+8' "$asm"
grep -Eq 'NET_PEND_JOY[[:space:]]+\.DS[[:space:]]+8' "$asm"
grep -Eq 'JSR[[:space:]]+NET_LOCAL_INPUT_PUSH' "$asm"
grep -Eq 'JSR[[:space:]]+NET_LOCAL_ACK_DISCARD' "$asm"
grep -Eq 'JSR[[:space:]]+NET_LOCAL_REPLAY_PENDING' "$asm"

echo "reconciliation replay smoke: ok"
