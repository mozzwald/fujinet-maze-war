#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"
XEX="$ROOT_DIR/build/maze-war.xex"
LAB="$ROOT_DIR/build/maze-war.lab"
TAB=$(printf '\t')

make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

grep -E "^PORT_MAX[$TAB ]*=[${TAB} ]*5" "$SRC" >/dev/null
grep -E "^PORTPROMPT[$TAB ]+\.BYTE[$TAB ]+\"PORT: \"" "$SRC" >/dev/null
grep -F '$39,$30,$30,$30,0' "$SRC" >/dev/null
grep -A18 -E "^PORT_FIELD" "$SRC" | grep -F 'HOSTSCR+40' >/dev/null
# 08-07 moved the controller into guarded high code to retain the core/display
# margin; HOST_SETUP remains the stable entry point used by the title.
grep -A45 -E "^UI_HOST_SETUP" "$SRC" | grep -E "JSR[$TAB ]+PORT_PARSE" >/dev/null
grep -A2 -E "^HOST_SETUP" "$SRC" | grep -E "JMP[$TAB ]+UI_HOST_SETUP" >/dev/null
grep -A12 -E "^PORT_PARSE" "$SRC" | grep -E "CMP[$TAB ]+#'9'\+1" >/dev/null

# The parsed high/low host-order bytes must replace the former immediate
# constants at the actual NS_INIT call.
grep -B3 -E "JSR[$TAB ]+NS_INIT" "$SRC" \
    | grep -E "LDA[$TAB ]+NET_PORT_ARG_A" >/dev/null
grep -B3 -E "JSR[$TAB ]+NS_INIT" "$SRC" \
    | grep -E "LDX[$TAB ]+NET_PORT_ARG_X" >/dev/null

python3 - "$XEX" "$LAB" <<'PYEOF'
import struct
import sys

xex, lab = sys.argv[1:]
symbols = {}
for line in open(lab):
    parts = line.split()
    if len(parts) >= 3:
        try:
            symbols[parts[2]] = int(parts[1], 16)
        except ValueError:
            pass

memory = {}
data = open(xex, "rb").read()
at = 2 if data[:2] == b"\xff\xff" else 0
while at + 4 <= len(data):
    lo, hi = struct.unpack("<HH", data[at:at + 4])
    if (lo, hi) == (0xffff, 0xffff):
        at += 2
        continue
    at += 4
    segment = data[at:at + hi - lo + 1]
    memory.update((lo + i, byte) for i, byte in enumerate(segment))
    at += len(segment)

def loaded(name, size):
    base = symbols[name]
    return bytes(memory[base + i] for i in range(size))

assert symbols["NAMEBUF"] - symbols["PORTBUF"] == 6
assert loaded("PORTBUF", 5) == b"9000\0"
assert loaded("NET_PORT_ARG_A", 2) == bytes((0x23, 0x28))
print("Atari port field layout and default NS_INIT bytes passed")
PYEOF

echo "Atari port prompt smoke passed"
