#!/bin/sh

# Guard the netstream POKEY contract: the handler owns channels 3+4 (joined
# 16-bit baud timer), AUDCTL, and SKCTL after net init. Game sound must stay
# on channels 1+2 through the SND_SEL/SND_OFF allocator.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
OBX="$ROOT_DIR/NSENGINE.OBX"

TAB=$(printf '\t')

# allocator + state present and wired
grep -F "SND_SEL" "$ATARI_SRC" >/dev/null
grep -F "SND_OFF" "$ATARI_SRC" >/dev/null
grep -F "SND_CH2_PID" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+SND_SEL" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+SND_OFF" "$ATARI_SRC" >/dev/null
grep -E "CPX[$TAB ]+NET_LOCAL_PID" "$ATARI_SRC" >/dev/null

# NS_GetStatus sticky error observability present
grep -E "NS_STAT[$TAB ]*=[$TAB ]*NS_BASE\+21" "$ATARI_SRC" >/dev/null
grep -E "JSR[$TAB ]+NS_STAT" "$ATARI_SRC" >/dev/null
grep -E "ORA[$TAB ]+NET_NS_ERRS" "$ATARI_SRC" >/dev/null

# no instruction may store to channel 3/4 registers (equates may remain)
if grep -nE "(STA|STX|STY)[$TAB ]+(AUDC3|AUDC4|AUDF3|AUDF4)" "$ATARI_SRC"; then
    echo "FAIL: direct store to POKEY channel 3/4 register" >&2
    exit 1
fi

# AUDCTL/SKCTL stores allowed only in the HOST_DONE-guarded cold-start block
audctl_stores=$(grep -cE "STA[$TAB ]+AUDCTL" "$ATARI_SRC" || true)
skctl_stores=$(grep -cE "STA[$TAB ]+SKCTL" "$ATARI_SRC" || true)
if [ "${audctl_stores:-0}" != "1" ] || [ "${skctl_stores:-0}" != "1" ]; then
    echo "FAIL: expected exactly one guarded STA AUDCTL and one STA SKCTL" >&2
    exit 1
fi
grep -B6 -E "STA[$TAB ]+AUDCTL" "$ATARI_SRC" | grep -F "HOST_DONE" >/dev/null

# the old per-slot Y=pid*2 idiom must not reappear next to an AUD write
if grep -A4 -E "^${TAB}TXA" "$ATARI_SRC" | grep -E 'AUD(C|F)[0-9]'; then
    echo "FAIL: TXA/ASL/TAY channel indexing near an AUD register write" >&2
    exit 1
fi

# handler binary: DOS header loading at $2800, sized like the current build
header=$(od -An -tx1 -N4 "$OBX" | tr -d ' ')
if [ "$header" != "ffff0028" ]; then
    echo "FAIL: NSENGINE.OBX header/load address changed (got $header)" >&2
    exit 1
fi
size=$(wc -c < "$OBX")
if [ "$size" -lt 1200 ]; then
    echo "FAIL: NSENGINE.OBX smaller than current handler build ($size bytes)" >&2
    exit 1
fi

echo "sound channel guard smoke passed"
