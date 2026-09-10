#!/bin/sh

# The client must re-announce its name when the server echoes a different one.
#
# Every client/server frame is COBS framed and CRC-16 protected. The name retry
# still must compare the server's accepted echo with what the player typed: a
# stale or different echoed NAME should be corrected rather than treated as
# settled for the rest of the game.
#
# The retry used to ask only whether our slot had *any* name, so a wrong name
# looked settled and was never corrected. It has to compare the echo against
# what we typed, folded the way the server folds it: uppercase, space padded.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ATARI_SRC="$ROOT_DIR/clients/atari/maze-war.asm"

python3 - "$ATARI_SRC" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
lines = src.split('\n')


def fail(m):
    raise SystemExit("FAIL: " + m)


def body(label):
    """Source lines of the routine at `label`, up to the lone ';' that ends it."""
    for i, l in enumerate(lines):
        if re.match(r'^%s\b' % re.escape(label), l):
            out = []
            for l2 in lines[i + 1:]:
                if l2.rstrip() == ';':
                    break
                out.append(l2)
            return out
    fail("routine %s not found; has it been renamed?" % label)


retry = '\n'.join(body('NET_NAME_RETRY'))
if 'NET_NAME_ECHO_OK' not in retry:
    fail("NET_NAME_RETRY does not consult the echo check, so a name the server "
         "got wrong is never corrected")
if re.search(r'JSR\s+NET_NAME_HAS', retry):
    fail("NET_NAME_RETRY still settles for any name at all; a wrong name "
         "reads as present and sticks for the whole game")

echo = body('NET_NAME_ECHO_OK')
txt = '\n'.join(echo)

if 'NAMEBUF' not in txt:
    fail("the echo check does not read NAMEBUF, so it is not comparing "
         "against what the player typed")
if 'NET_NAMES' not in txt:
    fail("the echo check does not read NET_NAMES, so it is not comparing "
         "against what the server sent back")

# The server uppercases and space pads. Comparing raw would mismatch forever
# on any lowercase name and retry every two seconds for the whole session.
if not re.search(r"CMP\s+#'a'", txt) or not re.search(r"CMP\s+#'z'\+1", txt):
    fail("the echo check does not fold case, so a lowercase name never "
         "matches the server's uppercase echo and retries forever")
if not re.search(r'LDA\s+#\$20', txt):
    fail("the echo check does not pad short names with spaces, so any name "
         "under 8 characters never matches and retries forever")

# It indexes NET_NAMES by slot*8 and walks exactly NAME_LEN characters.
if txt.count('ASL') < 3:
    fail("the echo check does not scale the slot by 8 to reach its name block")
if not re.search(r'CPY\s+#NAME_LEN', txt):
    fail("the echo check does not walk exactly NAME_LEN characters")

# Z is the answer: NET_NAME_RETRY branches on BEQ.
if not re.search(r'BEQ\s+NNR_X', retry):
    fail("NET_NAME_RETRY must skip the resend when the echo matches (BEQ)")

print("name echo verify smoke passed")
PYEOF
