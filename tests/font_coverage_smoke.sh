#!/bin/sh

# Every character a player name can contain must render as that character.
#
# The embedded font is the original game's, and the original game only ever
# drew compile-time text. Letters its own strings never used had their glyph
# slots reused for title-screen artwork: F, H, J, Q, V and X were artwork, not
# letters, which is why the name MOZZXL drew three dots for the X on real
# hardware. That was safe until names became arbitrary user input.
#
# The six missing letters now have letterforms in their own slots. $08-$0F are
# still PL0CHR, eight per-player coalesce tiles SETFUZZ rewrites at runtime;
# no name may reach them. Other unsafe embedded-font slots are filtered only
# by NAME_SCR. HOST_SCR remains permissive because the host prompt uses ROM
# characters and must accept hostnames such as 127.0.0.1.
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
scr=$(awk '$1=="NAME_SCR"{on=1} on{print} on&&/^NMSC_SP/{exit}' "$ATARI_SRC")
printf '%s' "$scr" | grep -qE "^NAME_SCR" \
  || { echo "FAIL: NAME_SCR is gone; names would be drawn straight from \
HOST_SCR, which passes codes the embedded font uses for coalesce tiles and \
title artwork" >&2; exit 1; }
for bound in '#\$3B' '#\$21' '#\$10' '#\$1E'; do
    printf '%s' "$scr" | grep -qE "CMP${TAB}+$bound" \
      || { echo "FAIL: NAME_SCR is missing its $bound bound" >&2; exit 1; }
done
if printf '%s' "$scr" | awk '/^NAME_SCR/{on=1} on&&/^NMSC_OK/{exit} on' \
     | grep -qE "^${TAB}+RTS"; then
    echo "FAIL: NAME_SCR returns before its filter runs" >&2; exit 1
fi
# and the scoreboard must actually use it
awk '$1=="NET_NAME_DRAW"{on=1} on{print} on&&/^NND_SP/{exit}' "$ATARI_SRC" \
  | grep -qE "JSR${TAB}+NAME_SCR" \
  || { echo "FAIL: the name draw path does not call NAME_SCR" >&2; exit 1; }
# HOST_SCR itself must stay permissive: the prompt runs on the ROM charset and
# filtering there blanked '.' in a typed IP address
host=$(awk '$1=="HOST_SCR"{on=1} on{print} on&&/^HOST_SCSP/{exit}' "$ATARI_SRC")
printf '%s' "$host" | grep -qE "CMP${TAB}+#\$3B" \
  && { echo "FAIL: HOST_SCR filters again; that blanks '.' and '-' on the host \
prompt, which renders them fine from the ROM charset" >&2; exit 1; }

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
def name_scr(c):
    """HOST_SCR, then NAME_SCR's filter -- what a NAME can put on screen.

    The host prompt is deliberately NOT filtered: it runs on the ROM charset
    where all of these codes are real glyphs.
    """
    if ord('a') <= c <= ord('z'):
        c -= 0x20
    if c < 0x20 or c >= 0x80:
        return 0x00
    c = c - 0x40 if c >= 0x60 else c - 0x20
    if c == 0x00:
        return c
    if c < 0x10:            # mask tables and PL0CHR coalesce tiles
        return 0x00
    if c < 0x1E:            # digits, ':' ';' '<' '='
        return c
    if c < 0x21 or c >= 0x3B:   # '>' '?' '@' and '[\]^_'
        return 0x00
    return c


reachable = {name_scr(c) for c in range(256)}
reachable.discard(0x00)

# 1. Every reachable glyph must actually be drawn.
for c in sorted(reachable):
    if glyph(c) == bytes(8):
        fail("screen code $%02X is reachable from a name but its glyph is "
             "blank; that character would vanish" % c)

# 2. No two reachable glyphs may be identical -- two letters that look the
#    same is the same defect wearing a different hat
seen = {}
for c in sorted(reachable):
    g = glyph(c)
    if g in seen:
        fail("screen codes $%02X and $%02X render identically" % (seen[g], c))
    seen[g] = c

# 3. The six repaired letters must still be letterforms.
letters = {0x26: 'F', 0x28: 'H', 0x2A: 'J', 0x31: 'Q', 0x36: 'V', 0x38: 'X'}
for code, name in letters.items():
    if code not in reachable:
        fail("%s ($%02X) is no longer reachable from a name" % (name, code))
    if glyph(code) == bytes(8):
        fail("%s ($%02X) is blank again" % (name, code))

# 5. $08-$0F belong to the GAME, not to the font.
#
# PL0CHR is at CHRSET+$40, i.e. glyph $08, and SETFUZZ writes glyph $08+slot
# and $0C+slot as it animates a coalescing wizard -- eight per-player scratch
# tiles that are blank in the assembled font only because the game fills them
# at runtime. They were mistaken for free slots once; title artwork and '-'
# and '.' were moved into them, and the wizard's own image fought the
# letterforms for the same bytes. Nothing addressable as text may live there,
# and no name may reach them.
pl0 = None
for line in open(root + "/build/maze-war.lab"):
    f = line.split()
    if len(f) >= 3 and f[2] == 'PL0CHR':
        pl0 = int(f[1], 16)
if pl0 is None:
    fail("PL0CHR not found in the label file")
first = (pl0 - 0x4000) // 8
scratch = set(range(first, first + 8))
if scratch != set(range(0x08, 0x10)):
    print("  note: PL0CHR moved; scratch tiles are now "
          + ",".join("$%02X" % g for g in sorted(scratch)))
clash = sorted(reachable & scratch)
if clash:
    fail("screen codes %s are reachable from a name but are PL0CHR coalesce "
         "tiles that SETFUZZ overwrites at runtime; text there fights the "
         "wizard's own image" % ", ".join("$%02X" % c for c in clash))
for g in sorted(scratch):
    if glyph(g) != bytes(8):
        fail("glyph $%02X is a PL0CHR scratch tile but carries artwork in the "
             "assembled font; the game owns those bytes" % g)

print("  %d screen codes reachable from a name, all distinct letterforms"
      % len(reachable))
print("  %d PL0CHR scratch tiles left to the game" % len(scratch))
PYEOF

echo "font coverage smoke passed"
