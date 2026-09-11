#!/bin/sh

# HUD rows use one readable blue text color with DLI off. Four missile PMGs
# provide shirt-color swatches beside the names, so the maze palette is not
# split mid-screen and the HUD still identifies each slot's shirt color.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
LAB="$ROOT_DIR/build/maze-war.lab"
XEX="$ROOT_DIR/build/maze-war.xex"
TAB=$(printf '\t')

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

fail() { echo "FAIL: $*" >&2; exit 1; }

name_draw=$(awk '$1=="NET_NAME_DRAW"{on=1} on{print} on&&/^NND_SP/{exit}' "$ATARI_SRC")
printf '%s' "$name_draw" | grep -qE "LDA${TAB}+NAMECOL,X" \
  || fail "NET_NAME_DRAW does not select the HUD text color band"
printf '%s' "$name_draw" | grep -qE "ORA${TAB}+HOLDIT" \
  || fail "NET_NAME_DRAW does not OR the color band into each name character"
printf '%s' "$name_draw" | grep -qE "JSR${TAB}+NAME_SCR" \
  || fail "NET_NAME_DRAW no longer filters names through NAME_SCR"

scorelbl=$(awk '$1=="NET_SCORELBL"{on=1} on{print} on&&/^NSLBN/{seen=1} seen&&/RTS/{exit}' "$ATARI_SRC")
printf '%s' "$scorelbl" | grep -qE "LDA${TAB}+NAMECOL,X" \
  || fail "fallback WIZARD/ZOMBIE labels do not select the HUD text color band"
printf '%s' "$scorelbl" | grep -qE "JSR${TAB}+NAME_SCR" \
  || fail "fallback WIZARD/ZOMBIE labels do not use the same screen-code path as names"
if printf '%s' "$scorelbl" | grep -qE "slot\\*6|ADC${TAB}+POINTER"; then
    fail "fallback WIZARD/ZOMBIE labels still select legacy pre-colored label variants"
fi

brick_done=$(awk '$1=="NET_RX_50BAD"{on=1} on{print} on&&/STA[[:space:]]+NMIEN/{exit}' "$ATARI_SRC")
printf '%s' "$brick_done" | grep -qF "LDA${TAB}#\$40" \
  || fail "gameplay map sync enables DLI; HUD markers should avoid the flickering DLI path"

hudpm=$(awk '$1=="HUD_PM_INIT"{on=1} on{print} on&&/RTS/{exit}' "$ATARI_SRC")
for ref in HPOSM0 SIZEM 'PMAREA+$300' HUD_MISSILE_Y HUD_MISSILE_BITS; do
    printf '%s' "$hudpm" | grep -qF "$ref" \
      || fail "HUD_PM_INIT does not reference $ref"
done
grep -qE "JSR${TAB}+HUD_PM_INIT" "$ATARI_SRC" \
  || fail "HUD_PM_INIT is never called before PM output is restored"

python3 - "$LAB" "$XEX" <<'PYEOF'
import sys

lab, xex = sys.argv[1], sys.argv[2]
addr = {}
for line in open(lab):
    f = line.split()
    if len(f) >= 3:
        addr[f[2]] = int(f[1], 16)

def fail(msg):
    raise SystemExit("FAIL: " + msg)

for sym in ("NAMECOL", "COLTBL", "HUD_MISSILE_Y", "HUD_MISSILE_BITS", "PLRTXT", "ZOMTXT"):
    if sym not in addr:
        fail(f"{sym} missing from label file")

d = open(xex, "rb").read()
i = 2 if d[:2] == b"\xff\xff" else 0
segs = []
while i + 4 <= len(d):
    s = d[i] | (d[i + 1] << 8)
    e = d[i + 2] | (d[i + 3] << 8)
    if s == 0xFFFF:
        i += 2
        continue
    n = e - s + 1
    segs.append((s, e, d[i + 4:i + 4 + n]))
    i += 4 + n

def read_bytes(name, n):
    a = addr[name]
    for s, e, b in segs:
        if s <= a and a + n - 1 <= e:
            return list(b[a - s:a - s + n])
    fail(f"{name} not found in any loaded segment")

bands = read_bytes("NAMECOL", 4)
if bands != [0x40, 0x40, 0x40, 0x40]:
    fail(f"NAMECOL is {bands!r}, not all-blue HUD text")

colors = read_bytes("COLTBL", 4)
if colors != [0xC8, 0x86, 0x58, 0x28]:
    fail(f"player shirt colors changed unexpectedly: {colors!r}")

ys = read_bytes("HUD_MISSILE_Y", 4)
bits = read_bytes("HUD_MISSILE_BITS", 4)
if ys != [192, 200, 208, 216]:
    fail(f"HUD missile rows are {ys!r}, not the four HUD rows")
if bits != [0x03, 0x0C, 0x30, 0xC0]:
    fail(f"HUD missile bits are {bits!r}, not missiles 0..3")

if bytes(read_bytes("PLRTXT", 6)) != b"WIZARD":
    fail("PLRTXT is not the clean ASCII WIZARD fallback label")
if bytes(read_bytes("ZOMTXT", 6)) != b"ZOMBIE":
    fail("ZOMTXT is not the clean ASCII ZOMBIE fallback label")

print("hud shirt markers: DLI off, blue text, missile PMGs mark slot colors")
PYEOF

echo "HUD name color smoke passed"
