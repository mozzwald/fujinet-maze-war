#!/bin/sh

# Player-missile memory must be cleared before PM DMA is switched on.
#
# PMAREA and the player pages are declared with .DS, which reserves without
# emitting, so on real hardware they hold power-up RAM. Emulation zeroes memory,
# so this class of bug is invisible there. Phase 04-04 fixed the old accidental
# missile DMA bug by clearing all PM pages and leaving missiles off.
#
# Phase 04-07 intentionally uses the four missile PMGs as small shirt-colour HUD
# swatches. That is safe only if missile DMA is enabled after HUD_PM_INIT has
# positioned all missiles, sized them, cleared PMAREA+$300, and written the HUD
# row bytes. This test keeps the original clear-order guard and permits bit 0 in
# GRACTL only behind that initializer.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
XEX="$ROOT_DIR/build/maze-war.xex"
LAB="$ROOT_DIR/build/maze-war.lab"

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

python3 - "$ATARI_SRC" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
lines = src.split('\n')


def fail(m):
    raise SystemExit("FAIL: " + m)


def parse_lda_before(i):
    for j in range(i - 1, max(-1, i - 6), -1):
        m = re.match(r'^\s*\S*\s*LDA\s+#\$?([0-9A-Fa-f]+)', lines[j])
        if m:
            return int(m.group(1), 16) if '$' in lines[j] else int(m.group(1))
    return None


# --- 1. GRACTL may enable missile DMA only after HUD_PM_INIT -------------
enables = []
for i, l in enumerate(lines):
    if re.match(r'^\s+STA\s+GRACTL', l):
        val = parse_lda_before(i)
        if val is None:
            fail("a STA GRACTL at line %d has no traceable immediate LDA" % (i + 1))
        enables.append((i + 1, val))
        if val & 0x01:
            window = '\n'.join(lines[max(0, i - 5):i])
            if 'JSR\tHUD_PM_INIT' not in window and 'JSR HUD_PM_INIT' not in window:
                fail("GRACTL write at line %d enables missile DMA without a "
                     "nearby HUD_PM_INIT call" % (i + 1))

if not enables:
    fail("no GRACTL writes found at all; has the register been renamed?")

# --- 2. the startup clear must precede the DMA programming ---------------
def first(pat):
    for i, l in enumerate(lines):
        if re.match(pat, l):
            return i
    return None


clear = first(r'^PM_CLEAR')
if clear is None:
    fail("PM_CLEAR is gone; PM memory is uninitialised on hardware again")

for reg in ('PMBASE', 'DMACTL', 'GRACTL'):
    w = first(r'^\s+STA\s+' + reg)
    if w is None:
        fail("no STA %s found" % reg)
    if w < clear:
        fail("%s is programmed at line %d, before the PM clear at line %d" %
             (reg, w + 1, clear + 1))

# --- 3. the startup clear must cover all eight PM pages -------------------
body = []
for i in range(clear, len(lines)):
    body.append(lines[i])
    if re.match(r'^\s+BNE\s+PMCLR_LP', lines[i]):
        break
else:
    fail("PM_CLEAR has no PMCLR_LP loop")
body = '\n'.join(body)
want = ['PMAREA,Y', 'PMAREA+$100,Y', 'PMAREA+$200,Y', 'PMAREA+$300,Y',
        'PL0,Y', 'PL1,Y', 'PL2,Y', 'PL3,Y']
missing = [w for w in want if w not in body]
if missing:
    fail("PM_CLEAR does not cover %s" % ", ".join(missing))

# --- 4. intentional missile HUD initializer must be complete -------------
m = re.search(r'^HUD_PM_INIT\n(?P<body>.*?\n\s+RTS)', src, re.M | re.S)
if not m:
    fail("HUD_PM_INIT is missing")
hud = m.group('body')
for ref in ('HPOSM0', 'HPOSM0+1', 'HPOSM0+2', 'HPOSM0+3', 'SIZEM',
            'PMAREA+$300,Y', 'HUD_MISSILE_Y,X', 'HUD_MISSILE_BITS,X'):
    if ref not in hud:
        fail("HUD_PM_INIT does not reference %s" % ref)

print("  %d GRACTL writes; missile DMA only after HUD_PM_INIT" % len(enables))
print("  PM clear covers all 8 pages and precedes PMBASE/DMACTL/GRACTL")
PYEOF

# SETSUIT copies bytes 8..0 from its eight-byte PM frames. The next frame's
# leading zero supplies byte nine except for MOVEST=3/DIR=up, which needs an
# explicit sentinel. WINPLYR accidentally supplied it before 08-01 deleted
# that unreachable allocation; the next byte then became SMOKE's $1C and
# produced a shirt-colour trail after upward movement.
python3 - "$XEX" "$LAB" <<'PYEOF'
import sys

xex, lab = sys.argv[1:]
sym = {}
for line in open(lab):
    fields = line.split()
    if len(fields) >= 3:
        try:
            sym[fields[2]] = int(fields[1], 16)
        except ValueError:
            pass

for name in ("SUITS", "SUITS_PAD", "SMOKE"):
    if name not in sym:
        raise SystemExit("FAIL: %s missing from symbol map" % name)

if sym["SUITS_PAD"] != sym["SUITS"] + 0x80:
    raise SystemExit("FAIL: SUITS_PAD is not the ninth byte after the final suit frame")
if sym["SMOKE"] != sym["SUITS_PAD"] + 1:
    raise SystemExit("FAIL: data was inserted between SUITS_PAD and SMOKE")

data = open(xex, "rb").read()
i = 2 if data[:2] == b"\xff\xff" else 0
pad = None
while i + 4 <= len(data):
    lo = data[i] | (data[i + 1] << 8)
    hi = data[i + 2] | (data[i + 3] << 8)
    i += 4
    size = hi - lo + 1
    body = data[i:i + size]
    i += size
    if lo <= sym["SUITS_PAD"] <= hi:
        pad = body[sym["SUITS_PAD"] - lo]
        break
if pad != 0:
    value = 0xff if pad is None else pad
    raise SystemExit("FAIL: final suit-frame clear byte is $%02X, not $00; upward movement leaves PM trails" % value)

print("  final suit-frame ninth byte is an explicit zero in the assembled XEX")
PYEOF

echo "pm init smoke passed"
