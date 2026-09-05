#!/bin/sh

# Every character a player name can contain must render as that character.
#
# The embedded font is the original game's, and the original game only ever
# drew compile-time text. Letters its own strings never used had their glyph
# slots reused for title-screen artwork: F, H, J, Q, V and X were artwork, not
# letters, which is why the name MOZZXL drew three dots for the X on real
# hardware. That was safe until names became arbitrary user input.
#
# The six letters now have letterforms and the artwork moved into slots
# whose characters a name has no use for: ( ) * + , and /. Five more artwork slots ($1E, $1F and $3B-$3F) are still
# reachable from HOST_SCR as ">?" and "[\]^_", and have no letter to be, so
# HOST_SCR folds them to a space.
#
# This test walks every byte a name can carry through a transcription of
# HOST_SCR and checks where it lands in the font.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
make -C "$ROOT_DIR" >/dev/null

ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"
TAB=$(printf '\t')

# The Python below transcribes HOST_SCR by hand, so it cannot notice the real
# routine losing its fold. Pin the fold in the source as well, or the
# transcription drifts from the code it claims to model and every assertion
# after it becomes decoration.
scr=$(awk '$1=="HOST_SCR"{on=1} on{print} on&&/^HOST_SCSP/{exit}' "$ATARI_SRC")
printf '%s' "$scr" | grep -qE "^HOST_SCART" \
  || { echo "FAIL: HOST_SCR has no HOST_SCART fold; artwork slots are reachable \
from a name again" >&2; exit 1; }
printf '%s' "$scr" | grep -qE "JMP${TAB}+HOST_SCART" \
  || { echo "FAIL: the \$60-\$7F branch of HOST_SCR bypasses the fold" >&2; exit 1; }
for bound in '#\$3B' '#\$21' '#\$10'; do
    printf '%s' "$scr" | grep -qE "CMP${TAB}+$bound" \
      || { echo "FAIL: HOST_SCR is missing its $bound whitelist bound" >&2; exit 1; }
done
# HOST_SCL0 must fall through into the fold, not return before it, and the
# fold's own body must be reachable -- a single early RTS anywhere inside it
# would let every code through while leaving the comparisons in place for a
# grep to find.
if printf '%s' "$scr" | awk '/^HOST_SCL0/{on=1} on&&/^HOST_SCART/{exit} on' \
     | grep -qE "^${TAB}+RTS"; then
    echo "FAIL: HOST_SCL0 returns before reaching the fold" >&2; exit 1
fi
if printf '%s' "$scr" | awk '/^HOST_SCART/{on=1} on&&/^HSC_OK/{exit} on' \
     | grep -qE "^${TAB}+RTS"; then
    echo "FAIL: HOST_SCR folds nothing -- an RTS inside HOST_SCART returns \
before the whitelist is applied" >&2; exit 1
fi

python3 - "$ROOT_DIR" <<'PYEOF'
import re, sys
root = sys.argv[1]
src = open(root + "/clients/atari/maze-war.asm").read()

# --- the font, from the assembled binary --------------------------------
d = open(root + "/build/maze-war.xex", "rb").read()
i = 2 if d[:2] == b"\xff\xff" else 0
segs = []
while i + 4 <= len(d):
    s = d[i] | (d[i+1] << 8); e = d[i+2] | (d[i+3] << 8)
    if s == 0xFFFF:
        i += 2; continue
    n = e - s + 1
    segs.append((s, e, d[i+4:i+4+n])); i += 4 + n


def glyph(code):
    a = 0x4000 + code * 8
    for s, e, b in segs:
        if s <= a and a + 7 <= e:
            return bytes(b[a-s:a-s+8])
    raise SystemExit("FAIL: glyph $%02X is outside every segment" % code)


def fail(m):
    raise SystemExit("FAIL: " + m)


# --- HOST_SCR, transcribed ----------------------------------------------
# Kept in step with clients/atari/maze-war.asm by hand. The fold at the end is
# what keeps artwork slots unreachable.
def host_scr(c):
    if ord('a') <= c <= ord('z'):
        c -= 0x20
    if c < 0x20 or c >= 0x80:
        return 0x00
    c = c - 0x40 if c >= 0x60 else c - 0x20
    # the whitelist: space, '-', '.', $10-$1D, and the letters
    if c == 0x00 or c == 0x0D or c == 0x0E:
        return c
    if c < 0x10:
        return 0x00
    if c < 0x1E:
        return c
    if c < 0x21 or c >= 0x3B:
        return 0x00
    return c


# --- which glyphs does the title screen still treat as artwork? ---------
m = re.search(r'\.BYTE\s+(\$9E,[^\n;]*)', src)
if not m:
    fail("could not find the title-screen artwork row; has it been renamed?")
art = {int(v.strip()[1:], 16) & 0x7F for v in m.group(1).split(',')
       if v.strip().startswith('$')}
art.discard(0x00)
if not art:
    fail("title artwork row parsed as empty")

reachable = {host_scr(c) for c in range(256)}
reachable.discard(0x00)

# 1. a name must never be able to paint artwork
overlap = sorted(reachable & art)
if overlap:
    fail("screen codes %s are reachable from a name AND used as title artwork; "
         "a name containing them would draw part of the logo"
         % ", ".join("$%02X" % c for c in overlap))

# 2. every reachable glyph must actually be drawn
for c in sorted(reachable):
    if glyph(c) == bytes(8):
        fail("screen code $%02X is reachable from a name but its glyph is "
             "blank; that character would vanish" % c)

# 3. no two reachable glyphs may be identical -- two letters that look the
#    same is the same defect wearing a different hat
seen = {}
for c in sorted(reachable):
    g = glyph(c)
    if g in seen:
        fail("screen codes $%02X and $%02X render identically" % (seen[g], c))
    seen[g] = c

# 4. the six repaired letters specifically must not have reverted to the
#    artwork they used to be, which now lives at $08-$0D
letters = {0x26: 'F', 0x28: 'H', 0x2A: 'J', 0x31: 'Q', 0x36: 'V', 0x38: 'X'}
moved = [0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F]
for code, name in letters.items():
    if code not in reachable:
        fail("%s ($%02X) is no longer reachable from a name" % (name, code))
    if glyph(code) in {glyph(a) for a in moved}:
        fail("%s ($%02X) is artwork again, not a letterform" % (name, code))

print("  %d screen codes reachable from a name, all distinct letterforms"
      % len(reachable))
print("  %d artwork glyphs, none of them reachable"
      % len(art | set(moved)))
PYEOF

echo "font coverage smoke passed"
