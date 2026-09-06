#!/bin/sh

# A slot id off the wire must be bounded before it indexes anything.
#
# Packets carry a pid byte. The apply paths check it against 4 before using it
# as an index, but the publish paths re-read the same byte at the end of the
# copy loop and used it raw:
#
#     LDX NET_SHOT_PKT+2
#     INC NET_SHOT_SEQ,X      ; NET_SHOT_SEQ is four bytes
#
# X can be anything 0..255 there, so a corrupt or hostile pid incremented a byte
# up to 255 past a four-entry array. NET_SHOT_SEQ sits at $7B6F and net state
# including NET_PX_X ($7C15) and NET_PEND_COUNT ($7C2A) is inside that reach --
# a single wire byte could walk the client's authoritative position.
#
# Every site that loads a pid from a packet must bound it before indexing.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

python3 - "$ATARI_SRC" <<'PYEOF'
import re, sys
lines = open(sys.argv[1]).read().split('\n')


def fail(m):
    raise SystemExit("FAIL: " + m)


sites = []
for i, l in enumerate(lines):
    m = re.match(r'^\s+LD([XY])\s+(NET_\w*PKT\+2|NET_\w*WRK\+2)\b', l)
    if m:
        sites.append((i, m.group(1), m.group(2)))

if not sites:
    fail("no packet-pid loads found at all; have the packet buffers been "
         "renamed? This test would then be silently vacuous.")

bad = []
for i, reg, src in sites:
    # look ahead a few instructions for a bound check that guards an indexed use
    window = lines[i+1:i+7]
    guarded = any(re.match(r'^\s+CP%s\s+#\s*4\b' % reg, w) or
                  re.match(r'^\s+CMP\s+#\s*4\b', w) for w in window)
    indexed = [w for w in window
               if re.search(r'^\s+(INC|DEC|STA|LDA|ASL|LSR|ROL|ROR)\s+\w+,%s\b' % reg, w)]
    if indexed and not guarded:
        bad.append((i + 1, src, indexed[0].strip()))

if bad:
    msg = "; ".join("line %d loads %s then does '%s' with no bound check"
                    % b for b in bad)
    fail("a packet pid indexes memory unbounded: " + msg)

print("  %d packet-pid load sites, all bounded before indexing" % len(sites))
PYEOF

echo "packet index bounds smoke passed"
