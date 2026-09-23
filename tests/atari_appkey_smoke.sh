#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"
LAB="$ROOT_DIR/build/maze-war.lab"
TAB=$(printf '\t')

make -C "$ROOT_DIR" HOST=qa.example.test ROOM_PORT_BASE=9100 ROOM_COUNT=3 \
    DEFAULT_PORT=9101 MAZEWAR_CREATOR_ID=0x3022 MAZEWAR_APP_ID=0x2A \
    BUILD_FLAVOR=QA \
    build/maze-war.xex >/dev/null

# Pin the actual FujiNet direct-SIO contract and the startup/menu boundaries.
grep -E "^APPKEY_READ_SIZE[$TAB ]*=[${TAB} ]*66" "$SRC" >/dev/null
grep -E "^APPKEY_SCRATCH_SIZE[$TAB ]*=[${TAB} ]*67" "$SRC" >/dev/null
grep -A55 -E "^APPKEY_OPEN" "$SRC" | grep -E "STA[$TAB ]+APPKEY_BUF\+4" >/dev/null
grep -A50 -E "^APPKEY_READ$" "$SRC" | grep -E "CMP[$TAB ]+#APPKEY_MAX\+1" >/dev/null
grep -A15 -E "^APPKEY_READ$" "$SRC" | grep -E "PHA" >/dev/null
grep -A15 -E "^APPKEY_LOBBY_READ$" "$SRC" | grep -E "PHA" >/dev/null
grep -A18 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+COLBK" >/dev/null
grep -A24 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+COLPF2" >/dev/null
grep -A24 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+COLOR2" >/dev/null
grep -A24 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+COLPF1" >/dev/null
grep -A24 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+COLOR1" >/dev/null
grep -A40 -E "^APPKEY_ROOM_CLEAR" "$SRC" | grep -E "LDA[$TAB ]+#MAZEWAR_APPKEY_ROOM_KEY" >/dev/null
grep -A40 -E "^APPKEY_ROOM_CLEAR" "$SRC" | grep -E "LDA[$TAB ]+#CFG_MAZEWAR_APP_ID" >/dev/null
grep -A12 -E "^APPKEY_LOBBY_HANDOFF_CLEAR" "$SRC" | grep -E "STA[$TAB ]+APPKEY_SCOPE" >/dev/null
grep -A12 -E "^APPKEY_LOBBY_HANDOFF_CLEAR" "$SRC" | grep -E "JMP[$TAB ]+APPKEY_CLEAR_KEY" >/dev/null
grep -A60 -E "^APPKEY_CLEAR_KEY" "$SRC" | grep -E "STA[$TAB ]+DAUX1" >/dev/null
grep -A70 -E "^APPKEY_USERNAME_WRITE" "$SRC" | grep -E "LDA[$TAB ]+#APPKEY_MAX" >/dev/null
grep -A25 -E "^UI_HOST_BOOT" "$SRC" | grep -E "STA[$TAB ]+APPKEY_AUTOJOIN" >/dev/null
grep -A25 -E "^UI_HOST_BOOT" "$SRC" | grep -F '#$04' >/dev/null

python3 - "$LAB" "$ROOT_DIR/build/maze-war.xex" "$SRC" <<'PYEOF'
import re
import struct
import sys

lab, xex, src = sys.argv[1:]
symbols = {}
for line in open(lab):
    parts = line.split()
    if len(parts) >= 3:
        try:
            symbols[parts[2]] = int(parts[1], 16)
        except ValueError:
            pass

assert symbols["CFG_MAZEWAR_CREATOR_ID"] == 0x3022
assert symbols["CFG_MAZEWAR_APP_ID"] == 0x2A
assert symbols["APPKEY_BUF"] == symbols["NET_BRICK_BUF"]
assert symbols["APPKEY_BUF"] + 67 <= symbols["NET_STATE_END"]
assert symbols["NET_HIGH_CODE_END"] < 0xA000

memory = {}
data = open(xex, "rb").read()
at = 2 if data[:2] == b"\xff\xff" else 0
while at + 4 <= len(data):
    lo, hi = struct.unpack("<HH", data[at:at + 4])
    if (lo, hi) == (0xFFFF, 0xFFFF):
        at += 2
        continue
    at += 4
    segment = data[at:at + hi - lo + 1]
    memory.update((lo + i, byte) for i, byte in enumerate(segment))
    at += len(segment)
prefix = symbols["APPKEY_TCP_PREFIX"]
assert bytes(memory[prefix + i] for i in range(6)) == b"tcp://"

def clean_name(raw):
    out = []
    for value in raw:
        c = chr(value)
        if "a" <= c <= "z":
            c = c.upper()
        if c.isascii() and ("A" <= c <= "Z" or "0" <= c <= "9" or c in " -."):
            if len(out) == 8:
                return None
            out.append(c)
    return "".join(out) or None

assert clean_name(b"mozz") == "MOZZ"
assert clean_name(b"M@o!z#z") == "MOZZ"
assert clean_name(b"A B-2.0") == "A B-2.0"
assert clean_name(b"bad$name") == "BADNAME"
assert clean_name(b"ninechars") is None
assert clean_name(bytes((1, 2, 3))) is None

source = open(src).read()
boot_start = source.index("\nAPPKEY_BOOT_LOAD\n") + 1
boot = source[boot_start:source.index("; Read key", boot_start)]
route = boot[boot.index("AKBL_ROOM\n"):]
saved_room = route.index("\nAKBL_SAVED_ROOM\n") + 1
assert route.index("LDA\t#CFG_MAZEWAR_APP_ID") < saved_room
assert route.index("JSR\tAPPKEY_LOBBY_HANDOFF_CLEAR") < saved_room
assert saved_room < route.index("LDA\t#MAZEWAR_APPKEY_ROOM_KEY")

host = "qa.example.test"
base, count = 9100, 3
def selected_url(value):
    if not 1 <= len(value) <= 64:
        return None
    match = re.fullmatch(r"tcp://([^:]+):([0-9]{1,5})", value)
    if not match or match.group(1) != host:
        return None
    port = int(match.group(2), 10)
    if not base <= port < base + count or port == 0 or port > 65535:
        return None
    return port

for port in range(base, base + count):
    assert selected_url(f"tcp://{host}:{port}") == port
for bad in (
    "", f"udp://{host}:9100", f"TCP://{host}:9100",
    "tcp://wrong.example.test:9100", f"tcp://{host}:9099",
    f"tcp://{host}:9103", f"tcp://{host}:65536",
    f"tcp://{host}:9100/room", f"tcp://{host}:9100?x=1",
    f"tcp://{host}:9100#frag", f"tcp://{host}:",
    f"tcp://{host}:91x0", "x" * 65,
):
    assert selected_url(bad) is None, bad

print("Atari AppKey buffer, sanitizer, URL, and startup routing checks passed")
PYEOF

echo "Atari AppKey smoke passed"
