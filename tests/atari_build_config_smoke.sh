#!/bin/sh

# Generated Atari build settings are part of the executable contract. Verify
# changed values reach both MADS data and the NS_INIT port bytes, while an
# identical invocation leaves the XEX untouched.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
XEX="$ROOT_DIR/build/maze-war.xex"
LAB="$ROOT_DIR/build/maze-war.lab"

make -C "$ROOT_DIR" HOST=10.24.3.8 ROOM_PORT_BASE=9100 ROOM_COUNT=3 \
    DEFAULT_PORT=9101 LOBBY_BASE=https://qa.example.test MAZEWAR_APPKEY=0x2A \
    KILL_LIMIT=7 BUILD_FLAVOR=QA build/maze-war.xex >/dev/null

python3 - "$XEX" "$LAB" <<'PYEOF'
import struct
import sys

xex, lab = sys.argv[1:]
symbols = {}
for line in open(lab):
    fields = line.split()
    if len(fields) >= 3:
        try:
            symbols[fields[2]] = int(fields[1], 16)
        except ValueError:
            pass
memory = {}
raw = open(xex, "rb").read()
at = 2 if raw[:2] == b"\xff\xff" else 0
while at + 4 <= len(raw):
    lo, hi = struct.unpack("<HH", raw[at:at + 4])
    at += 4
    data = raw[at:at + hi - lo + 1]
    memory.update((lo + offset, byte) for offset, byte in enumerate(data))
    at += len(data)
def blob(name, length):
    base = symbols[name]
    return bytes(memory[base + offset] for offset in range(length))

assert blob("CFG_HOST", 10) == b"10.24.3.8\0", blob("CFG_HOST", 10)
assert blob("CFG_PORT_TEXT", 5) == b"9101\0", blob("CFG_PORT_TEXT", 5)
assert blob("CFG_LOBBY_BASE", 24) == b"https://qa.example.test\0", blob("CFG_LOBBY_BASE", 24)
assert blob("NET_PORT_ARG_A", 2) == bytes((0x23, 0x8D)), blob("NET_PORT_ARG_A", 2)
assert symbols["CFG_ROOM_PORT_BASE"] == 9100
assert symbols["CFG_ROOM_COUNT"] == 3
assert symbols["CFG_BUILD_FLAVOR"] == 1
assert symbols["CFG_MAZEWAR_APPKEY"] == 0x2A
assert symbols["CFG_KILL_LIMIT"] == 7
print("generated Atari configuration bytes passed")
PYEOF

before=$(stat -c %Y "$XEX")
sleep 1
make -C "$ROOT_DIR" HOST=10.24.3.8 ROOM_PORT_BASE=9100 ROOM_COUNT=3 \
    DEFAULT_PORT=9101 LOBBY_BASE=https://qa.example.test MAZEWAR_APPKEY=0x2A \
    KILL_LIMIT=7 BUILD_FLAVOR=QA build/maze-war.xex >/dev/null
after=$(stat -c %Y "$XEX")
[ "$before" = "$after" ] || {
    echo "FAIL: unchanged generated settings rebuilt the Atari XEX" >&2
    exit 1
}

# Restore LAN defaults without relying on smoke-test ordering.
make -C "$ROOT_DIR" build/maze-war.xex >/dev/null

for bad in \
    "HOST=bad_name" \
    "ROOM_PORT_BASE=65535 ROOM_COUNT=2" \
    "DEFAULT_PORT=9104" \
    "KILL_LIMIT=11" \
    "MAZEWAR_APPKEY=not-a-key"; do
    if make -C "$ROOT_DIR" $bad build/maze-war.xex >/dev/null 2>&1; then
        echo "FAIL: invalid build configuration was accepted: $bad" >&2
        exit 1
    fi
done

echo "Atari build configuration smoke passed"
