#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
SRC="$ROOT_DIR/clients/atari/maze-war.asm"
SDL="$ROOT_DIR/clients/linux/sdl_main.c"
SERVER="$ROOT_DIR/server/main.c"

make -C "$ROOT_DIR" build/maze-war.xex build/maze-war-client-sdl >/dev/null

python3 - "$SRC" "$SDL" "$SERVER" <<'PYEOF'
import re
import sys

atari = open(sys.argv[1]).read()
sdl = open(sys.argv[2]).read()
server = open(sys.argv[3]).read()

def block(start, end):
    match = re.search(rf"(?ms)^{start}\b(.*?)(?=^{end}\b)", atari)
    if not match:
        raise SystemExit(f"FAIL: missing {start}..{end} block")
    return match.group(1)

vbi = block("VBI", "VBI_PLAY")
if "JSR\tNET_STAGE_COMMIT" not in vbi or "JSR\tROUND_PRESENT_TICK" not in vbi:
    raise SystemExit("FAIL: results are not serviced by the live deferred VBI")
if "NET_RESP_COMMIT" in vbi:
    raise SystemExit("FAIL: stale respawns can commit before the results branch")

match = block("UI_REL_MATCH", "UI_REL_START")
for frozen in ("ROUND_ACTIVE_MASK", "ROUND_FINAL_ROLE",
               "ROUND_ZOMBIE_HISTORY", "ROUND_FINAL_SCORE"):
    if frozen not in match:
        raise SystemExit(f"FAIL: MATCH_END does not freeze {frozen}")
if match.find("STA\tROUND_PRESENT_STATE") < match.find("STA\tROUND_FINAL_SCORE,X"):
    raise SystemExit("FAIL: VBI presentation is published before result data is frozen")

present = block("ROUND_PRESENT_TICK", "NET_HIGH_CODE_END")
for state in ("RP_BEGIN", "RP_LOSER_EVAP", "RP_WINNER_DANCE",
              "RP_WINNER_EVAP", "RP_FADE", "RP_RESULTS"):
    if state not in present:
        raise SystemExit(f"FAIL: state machine omits {state}")
for required in ("NET_SHOT_WATCH_TICK", "NET_RESP_APPLYSEQ,X", "PALNTS",
                 "EVAPRTE", "PCOLR0,X", "HOST_CLR", "HOSTDISP",
                 "#$E0", "ROUND_PRESENT_RESTORE", "NET_SHOT_CLEAR_X"):
    if required not in present:
        raise SystemExit(f"FAIL: presentation/restore invariant missing: {required}")
if re.search(r"\b(?:AUDC3|AUDC4|AUDCTL|SKCTL)\b", present):
    raise SystemExit("FAIL: presentation touches NetStream-owned POKEY channels")
for timer_part in ("#44", "#1", "#250", "ROUND_PRESENT_TIMER_HI"):
    if timer_part not in present:
        raise SystemExit(f"FAIL: five-second PAL/NTSC timer omits {timer_part}")

if "STA\tNET_ROUND_PHASE" not in block("UI_REL_START", "NET_ROUND_CHECK_READY"):
    raise SystemExit("FAIL: ROUND_START cannot interrupt the presentation")
if re.search(r"STA\s+VDSLST|^DLI\b", atari, re.M):
    raise SystemExit("FAIL: obsolete DLI was re-enabled")

for text in ("result_active_mask = buf[3]", "zombie_history_mask = buf[10]",
             "BEATS ZOMBIES", "%02u", "NEXT ROUND"):
    if text not in sdl:
        raise SystemExit(f"FAIL: SDL synchronized results omit {text}")

if "DEFAULT_INTERMISSION_MS = 15000" not in server:
    raise SystemExit("FAIL: default intermission does not preserve eight seconds of results")

print("round presentation state, frozen data, restore, and SDL result view passed")
PYEOF

echo "round presentation smoke passed"
