#!/bin/sh

# Pin the Atari/FujiNet and Linux clean-leave implementation seams. The live
# socket policy itself is exercised by leave_grace_smoke.sh.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ASM="$ROOT_DIR/clients/atari/maze-war.asm"

make -C "$ROOT_DIR" build/maze-war.xex build/maze-war-client \
  build/maze-war-client-sdl >/dev/null

python3 - "$ASM" "$ROOT_DIR/clients/linux/main.c" \
  "$ROOT_DIR/clients/linux/sdl_main.c" <<'PYEOF'
import re
import sys

asm = open(sys.argv[1]).read()
linux = open(sys.argv[2]).read()
sdl = open(sys.argv[3]).read()


def block(text, start, end):
    match = re.search(rf"(?ms)^{re.escape(start)}.*?(?=^{re.escape(end)})", text)
    if not match:
        raise AssertionError(f"missing block {start}..{end}")
    return match.group(0)


main_loop = block(asm, "STRTCN", ";MAIN PROGRAM SUBROUTINES")
assert main_loop.index("JSR\tNET_LEAVE_INPUT") < main_loop.index("JSR\tNET_POLL")
assert main_loop.index("JSR\tNET_POLL") < main_loop.index("JSR\tNET_LEAVE_TICK")

poll = block(asm, "NET_POLL", "NET_SAMPLE_INPUT")
assert "NET_REL_ACK_PEND" in poll
assert "NET_LEAVING" in poll and "JSR\tNET_TX_BUILD_LEAVE" in poll

dispatcher = block(asm, "NET_FRAME_DISPATCH", "; Network shots")
assert "CMP\t#$57" in dispatcher
assert "CMP\tNET_LEAVE_SEQ" in dispatcher
assert "STA\tNET_LEAVE_ACKED" in dispatcher

reset = block(asm, "NET_SESSION_RESET", "; Session-control builders")
for symbol in ("NET_STATE_CLEAR", "ACTFLAG", "MOVEST", "SHOTDIR", "SCRPND"):
    assert symbol in reset
state_clear = block(asm, "NET_STATE_CLEAR", "; --- NET snapshot apply")
assert "NET_INIT_ARGS" in state_clear
assert "HOSTBUF" not in state_clear and "PORTBUF" not in state_clear

endc = block(asm, "NET_ENDC", "; OPTION, ESC, and Q leave")
assert "JSR\tNS_END" in endc and "JSR\tNET_FW_CLOSE" in endc
fw_close = block(asm, "NET_FW_CLOSE", "NET_HIGH_CODE_END")
for token in ("#$70", "#$3F", "STA\tDDEVIC", "STA\tDCOMND", "JSR\tSIOV"):
    assert token in fw_close

for label, end in (("RESTART", "STIMER"), ("NET_HOSTRET", ";SILENCE WATCHDOG")):
    teardown = block(asm, label, end)
    assert teardown.index("JSR\tVBIOFF") < teardown.index("JSR\tNET_ENDC")

host_return = block(asm, "NET_HOSTRET", "NHR_PMCLR")
assert host_return.index("JSR\tNET_ENDC") < host_return.index("JSR\tAPPKEY_ROOM_CLEAR")

for source in (linux, sdl):
    assert "PKT_LEAVE_ROOM = 0x56" in source
    assert "PKT_LEAVE_ACK = 0x57" in source
    clean = block(source, "static void clean_leave", "static int round_is_newer")
    assert "+ 750" in clean
    assert "reply[0] == PKT_LEAVE_ACK" in clean

print("bounded session teardown and shared reset source contract passed")
PYEOF

echo "session teardown smoke passed"
