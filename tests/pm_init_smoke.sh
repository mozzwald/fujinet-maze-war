#!/bin/sh

# Player-missile memory must be cleared before PM DMA is switched on.
#
# PMAREA and the player pages are declared with .DS, which reserves without
# emitting, so on real hardware they hold power-up RAM. Emulation zeroes memory,
# so this class of bug is invisible here and shows up only on a real machine --
# it was reported from hardware as a vertical dotted column tracking the player.
#
# It had two halves. Nothing cleared $3800-$3BFF, and GRACTL was set to $03,
# whose bit 0 enables missile DMA. In single-line resolution the missiles live
# at PMBASE+$300 = $3B00, inside exactly the range nothing cleared, and the
# client never writes HPOSM0-3 or any missile graphics at all. So missiles were
# switched on, never positioned, and fed uninitialised RAM.
#
# Both halves are pinned here, because either one alone brings the artefact
# back.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

python3 - "$ATARI_SRC" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
lines = src.split('\n')


def fail(m):
    raise SystemExit("FAIL: " + m)


# --- 1. no GRACTL write may enable missile DMA -------------------------
enables = []
for i, l in enumerate(lines):
    if re.match(r'^\s+STA\s+GRACTL', l):
        val = None
        for j in range(i - 1, max(-1, i - 6), -1):
            m = re.match(r'^\s*\S*\s*LDA\s+#\$?([0-9A-Fa-f]+)', lines[j])
            if m:
                t = m.group(1)
                val = int(t, 16) if '$' in lines[j] else int(t)
                break
        if val is None:
            fail("a STA GRACTL at line %d has no traceable LDA; cannot tell "
                 "whether it enables missiles" % (i + 1))
        enables.append((i + 1, val))

if not enables:
    fail("no GRACTL writes found at all; has the register been renamed?")
for ln, val in enables:
    if val & 0x01:
        fail("GRACTL write at line %d sets $%02X, whose bit 0 enables missile "
             "DMA. The client writes no HPOSM and no missile graphics, so that "
             "DMA can only fetch uninitialised memory." % (ln, val))

# --- 2. the clear must precede the DMA programming ---------------------
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
        fail("%s is programmed at line %d, before the PM clear at line %d. "
             "Enabling DMA over memory that has not been cleared yet is the "
             "whole bug; the ordering is the fix." % (reg, w + 1, clear + 1))

# --- 3. the clear must actually cover all eight PM pages ---------------
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
    fail("PM_CLEAR does not cover %s; the missile quarter is PMAREA+$300 and "
         "is the one that produced the artefact" % ", ".join(missing))

print("  %d GRACTL writes, none enabling missile DMA" % len(enables))
print("  PM clear covers all 8 pages and precedes PMBASE/DMACTL/GRACTL")
PYEOF

echo "pm init smoke passed"
