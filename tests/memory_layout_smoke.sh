#!/bin/sh

# Where the Atari client's data lands is a correctness property, not a detail.
#
# ANTIC only increments the low 10 bits of the display list counter, so a list
# that crosses a 1K boundary wraps to the start of its own 1K page and executes
# garbage. And the display buffers are .DS -- reserved, never loaded -- so any
# loaded segment that reaches into them shares memory with the screen.
#
# Both have already happened. The display lists used to land wherever the code
# happened to end; trimming some counters once pushed the GAME list across
# $6C00. The fix, aligning the whole data block to 1K, then failed the other
# way: the block outgrew its page, ALIGN pushed it to $7000, and it landed on
# top of HOSTSCR. Neither failure is visible in a build log.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
XEX="$ROOT_DIR/build/maze-war.xex"
LAB="$ROOT_DIR/build/maze-war.lab"

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

python3 - "$XEX" "$LAB" <<'PYEOF'
import struct, sys

xex, lab = sys.argv[1], sys.argv[2]

sym = {}
for line in open(lab):
    parts = line.split()
    if len(parts) >= 3:
        try:
            sym[parts[2]] = int(parts[1], 16)
        except ValueError:
            pass


def fail(m):
    raise SystemExit("FAIL: " + m)


segs = []
d = open(xex, "rb").read()
i = 2 if d[0:2] == b"\xff\xff" else 0
while i + 4 <= len(d):
    a, b = struct.unpack("<HH", d[i:i + 4])
    if (a, b) == (0xFFFF, 0xFFFF):
        i += 2
        continue
    segs.append((a, b))
    i += 4 + (b - a + 1)
if not segs:
    fail("no segments in the executable")

# 1. The live display lists sit together, from a page boundary, so neither
#    them can cross a 1K boundary however the code before them grows.
lists = ["HOSTDISP", "GAME"]
for n in lists:
    if n not in sym:
        fail(f"{n} is not in the symbol table")
first = sym[lists[0]]
if first & 0xFF:
    fail(f"the display list group starts at ${first:04X}, not on a page boundary")
last = max(sym[n] for n in lists)
if last - first >= 0x100:
    fail(f"the display lists span ${first:04X}..${last:04X}, more than one page; "
         "they are no longer guaranteed to share a 1K page")
for n in lists:
    a = sym[n]
    if (a & 0xFC00) != ((a + 0x3F) & 0xFC00):
        fail(f"{n} at ${a:04X} can cross a 1K boundary; ANTIC would wrap it")

# 2. The removed title is now the bounded round/menu UI allocation. Phase 8-3
# consumes part of it for persistent transition handlers; it must never grow
# into the fixed maze data.
ui_lo = sym["UI_DATA_START"]
ui_hi = sym["MAZEDAT"] - 1
if ui_hi - ui_lo + 1 < 0x100:
    fail(f"reclaimed UI range is only {ui_hi - ui_lo + 1} bytes")
ui_used_hi = sym["UI_DATA_END"] - 1
if ui_used_hi > ui_hi:
    fail(f"UI data ends at ${ui_used_hi:04X}, past maze boundary ${ui_hi:04X}")

# 3. Nothing may be loaded into the display buffers. They are reserved with .DS
#    and written at runtime, so anything loaded there is shared memory.
buf_lo = sym["HOSTSCR"]
buf_hi = sym["SCORE"] + 69 - 1
for a, b in segs:
    if a <= buf_hi and b >= buf_lo:
        fail(f"segment ${a:04X}-${b:04X} overlaps the display buffers "
             f"${buf_lo:04X}-${buf_hi:04X}")

# 4. ANTIC fetches screen data with a 4K counter, so each buffer the game
#    display list points at has to stay inside one 4K page.
for name, size in (("GAMESCR", 760), ("BOTSCRN", 11 + 69)):
    a = sym[name]
    if (a & 0xF000) != ((a + size - 1) & 0xF000):
        fail(f"{name} at ${a:04X} crosses a 4K boundary")

# 5. The fixed loaded-core, zero-page and NetStream-state budgets must retain
# their existing safety margins. The handler uses $EE, while state may grow
# only up to $7F00. Round presentation plus 08-05 session teardown occupy the
# deliberately isolated high-code reserve. Phase 08-06 adds the reachable
# title/direct-connect flow and immutable defaults; 08-07 extends the guarded
# segment below $A000 for direct-SIO AppKey, Lobby browser, and URL validation.
if sym["CORE_DATA_END"] > 0x6F00:
    fail(f"loaded core ends at ${sym['CORE_DATA_END']:04X}, leaving less than $100 before display buffers")
if sym["ZP_END"] > 0xE9:
    fail(f"zero page ends at ${sym['ZP_END']:04X}, leaving less than five bytes before handler $EE")
if sym["NET_STATE_END"] > 0x7F00:
    fail(f"NetStream state ends at ${sym['NET_STATE_END']:04X}, leaving less than $100 before $8000")
if sym["NET_HIGH_CODE_END"] > 0xA000:
    fail(f"high code ends at ${sym['NET_HIGH_CODE_END']:04X}, at or above the BASIC ROM window")

# The default AppKey response is a two-byte count plus up to 64 payload bytes;
# one more byte is needed for a local terminator. It aliases packet staging only
# while NetStream is stopped instead of becoming a permanent 67-byte buffer.
if sym["APPKEY_BUF"] != sym["NET_BRICK_BUF"]:
    fail("AppKey scratch no longer aliases inactive brick staging")
if sym["APPKEY_BUF"] + 67 > sym["NET_STATE_END"]:
    fail("AppKey response and terminator exceed contiguous runtime state")

# 6. Segments must not overlap each other either.
for j in range(len(segs)):
    for k in range(j + 1, len(segs)):
        a1, b1 = segs[j]
        a2, b2 = segs[k]
        if a1 <= b2 and a2 <= b1:
            fail(f"segments ${a1:04X}-${b1:04X} and ${a2:04X}-${b2:04X} overlap")

print("memory layout ok: lists at " +
      ", ".join(f"{n}=${sym[n]:04X}" for n in lists) +
      f"; UI allocation ${ui_lo:04X}-${ui_used_hi:04X} of ${ui_lo:04X}-${ui_hi:04X}"
      f"; core end ${sym['CORE_DATA_END']:04X}; ZP end ${sym['ZP_END']:04X}"
      f"; state end ${sym['NET_STATE_END']:04X}"
      f"; buffers ${buf_lo:04X}-${buf_hi:04X} clear of every segment")
PYEOF

echo "memory layout smoke passed"
