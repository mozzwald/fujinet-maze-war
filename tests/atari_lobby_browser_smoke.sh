#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"
LAB="$ROOT_DIR/build/maze-war.lab"
XEX="$ROOT_DIR/build/maze-war.xex"
TAB=$(printf '\t')

make -C "$ROOT_DIR" HOST=qa.example.test ROOM_PORT_BASE=9100 ROOM_COUNT=4 \
    DEFAULT_PORT=9100 LOBBY_BASE=https://qalobby.example.test \
    MAZEWAR_CREATOR_ID=0x3022 MAZEWAR_APP_ID=0x03 \
    BUILD_FLAVOR=QA build/maze-war.xex >/dev/null

# The production path must use direct N: SIO, fixed-size reads, the shared URL
# validator/AppKey writer, and explicit bounded pagination/timeouts.
grep -E '^LOBBY_RECORD_SIZE.*=.*189' "$SRC" >/dev/null
grep -A150 -E '^LOBBY_NET_OPEN' "$SRC" | grep -E "STA[$TAB ]+DDEVIC" >/dev/null
grep -A55 -E '^LOBBY_NET_READ' "$SRC" | grep -E "STA[$TAB ]+DAUX2" >/dev/null
grep -A170 -E '^LOBBY_RECORD_VALIDATE' "$SRC" | grep -E "JSR[$TAB ]+APPKEY_URL_APPLY" >/dev/null
grep -A65 -E '^LOBBY_SELECT_APPLY' "$SRC" | grep -E "JSR[$TAB ]+APPKEY_ROOM_WRITE" >/dev/null
grep -A35 -E '^LBM_KEYS' "$SRC" | grep -E 'MENU_KEY_UP' >/dev/null
grep -A35 -E '^LBM_KEYS' "$SRC" | grep -E 'MENU_KEY_DOWN' >/dev/null
grep -A35 -E '^LBM_KEYS' "$SRC" | grep -E 'MENU_KEY_LEFT' >/dev/null
grep -A35 -E '^LBM_KEYS' "$SRC" | grep -E 'MENU_KEY_RIGHT' >/dev/null
grep -E '^LOBBY_MAX_PAGES.*=.*8' "$SRC" >/dev/null

python3 - "$LAB" "$XEX" <<'PYEOF'
import re
import struct
import sys

lab, xex = sys.argv[1:]
symbols = {}
for line in open(lab, encoding="ascii"):
    parts = line.split()
    if len(parts) >= 3:
        try:
            symbols[parts[2]] = int(parts[1], 16)
        except ValueError:
            pass

assert symbols["LOBBY_RECORD_SIZE"] == 189
assert symbols["LOBBY_PAGE_SIZE"] == 4
assert symbols["LOBBY_MAX_PAGES"] == 8
assert symbols["LOBBY_BUF"] == symbols["NET_MAP_CELLS"]
assert symbols["LOBBY_BUF"] + 189 <= symbols["NET_STATE_END"]
assert symbols["APPKEY_BUF"] != symbols["LOBBY_BUF"]
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
    memory.update((lo + i, value) for i, value in enumerate(segment))
    at += len(segment)

query_at = symbols["LOBBY_QUERY"]
query = bytearray()
while memory[query_at + len(query)]:
    query.append(memory[query_at + len(query)])
assert query == b"view?bin=1&platform=atari&appkey="


def fixed(text, max_len):
    raw = text.encode("ascii")
    assert len(raw) <= max_len
    return raw + bytes(max_len + 1 - len(raw))


def record(*, appkey=3, game="Maze War", server="QA Room", url="tcp://qa.example.test:9100",
           client="tnfs://qa.example.test/maze-war.xex", region="us", online=1,
           players=1, maximum=4):
    result = (bytes((appkey,)) + fixed(game, 16) + fixed(server, 32) +
              fixed(url, 64) + fixed(client, 64) + fixed(region, 2) +
              bytes((online, players, maximum, 0, 0)))
    assert len(result) == 189
    return result


def field(raw, offset, size, minimum=1):
    part = raw[offset:offset + size]
    try:
        end = part.index(0)
    except ValueError:
        return None
    if end < minimum or any(part[end + 1:]):
        return None
    value = part[:end]
    if any(ch < 0x20 or ch > 0x7e for ch in value):
        return None
    return value.decode("ascii")


def parse_page(payload):
    if len(payload) < 3:
        raise ValueError("truncated header")
    count = payload[0]
    if count > 4 or payload[1:3] != b"\0\0":
        raise ValueError("bad header")
    if len(payload) != 3 + count * 189:
        raise ValueError("truncated or oversized response")
    visible = []
    for index in range(count):
        raw = payload[3 + index * 189:3 + (index + 1) * 189]
        game = field(raw, 1, 17)
        name = field(raw, 18, 33, 2)
        url = field(raw, 51, 65)
        client = field(raw, 116, 65)
        region = field(raw, 181, 3, 2)
        match = re.fullmatch(r"tcp://qa\.example\.test:([0-9]{1,5})", url or "")
        valid_port = bool(match and 9100 <= int(match.group(1)) <= 9103)
        maximum, players = raw[186], raw[185]
        if (raw[0] == 3 and game == "Maze War" and name and client and region and
                raw[184] == 1 and valid_port and 0 < maximum < 10 and players <= maximum):
            visible.append((name, int(match.group(1)), players, maximum))
    return visible, count == 4


# Zero, one, four/full-page, and a bounded second page.
assert parse_page(b"\0\0\0") == ([], False)
one = b"\1\0\0" + record()
assert parse_page(one)[0] == [("QA Room", 9100, 1, 4)]
page0 = b"\4\0\0" + b"".join(record(server=f"Room {i}",
    url=f"tcp://qa.example.test:{9100 + i}", players=i) for i in range(4))
rooms, more = parse_page(page0)
assert len(rooms) == 4 and more
page1 = b"\1\0\0" + record(server="Last Room", url="tcp://qa.example.test:9103")
assert parse_page(page1)[0][0][0] == "Last Room"

# Records returned despite query filtering still face all client-side gates.
bad_records = [
    record(online=0),
    record(appkey=4),
    record(game="Other Game"),
    record(url="tcp://wrong.example.test:9100"),
    record(url="tcp://qa.example.test:9099"),
    record(url="tcp://qa.example.test:9104"),
    record(players=5, maximum=4),
]
for bad in bad_records:
    assert parse_page(b"\1\0\0" + bad)[0] == []

malformed = bytearray(record())
malformed[18 + len("QA Room") + 1] = ord("X")  # nonzero byte after terminator
assert parse_page(b"\1\0\0" + malformed)[0] == []
for bad in (one[:-1], one + b"X", b"\5\0\0", b"\1\1\0" + record()):
    try:
        parse_page(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed/truncated fixture was accepted")

print("Atari Lobby binary fixtures and memory aliases passed")
PYEOF

echo "Atari Lobby browser smoke passed"
