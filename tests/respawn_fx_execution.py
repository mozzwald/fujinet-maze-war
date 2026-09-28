#!/usr/bin/env python3
"""Execute the built respawn cue on a 6502, including every legal spawn/slot.

Build the Atari XEX first, then run: uv run --with py65 tests/respawn_fx_execution.py
This focused instruction test complements the real Atari800 boot check.
"""
from pathlib import Path
import struct

from py65.devices.mpu6502 import MPU


ROOT = Path(__file__).resolve().parents[1]
symbols = {}
for line in (ROOT / "build/maze-war.lab").read_text().splitlines():
    fields = line.split()
    if len(fields) >= 3:
        symbols[fields[2]] = int(fields[1], 16)

image = [0] * 65536
blob = (ROOT / "build/maze-war.xex").read_bytes()
offset = 0
while offset < len(blob):
    start = struct.unpack_from("<H", blob, offset)[0]
    offset += 2
    if start == 0xFFFF:
        continue
    end = struct.unpack_from("<H", blob, offset)[0]
    offset += 2
    length = end - start + 1
    image[start:end + 1] = blob[offset:offset + length]
    offset += length


def put(cpu, name, value, index=0):
    cpu.memory[symbols[name] + index] = value


def get(cpu, name):
    return cpu.memory[symbols[name]]


def call(cpu, name, slot=0):
    cpu.x = slot
    cpu.stPushWord(0x05FF)
    cpu.pc = symbols[name]
    for _ in range(20000):
        cpu.step()
        if cpu.pc == 0x0600:
            return
    raise AssertionError(f"{name} did not return")


def fresh(slot):
    cpu = MPU(memory=image.copy())
    put(cpu, "NET_LOCAL_PID", slot)
    put(cpu, "NET_GAME_SHOW", 1)
    put(cpu, "NET_SEAT_MASK", 15)
    call(cpu, "HUD_PM_INIT")
    return cpu


def respawn(cpu, slot, x, y, flags=3):
    for i, value in enumerate((0x52, 1, slot, x, y, flags)):
        put(cpu, "NET_RESP_WRK", value, i)
    call(cpu, "NET_RESP_APPLY_WRK", slot)


missiles = symbols["PMAREA"] + 0x300
cases = 0
for slot in range(4):
    for x in range(1, 19):
        for y in range(1, 18):
            cpu = fresh(slot)
            hud = cpu.memory[missiles:missiles + 256].copy()
            respawn(cpu, slot, x, y)
            assert get(cpu, "RESPAWN_FX_TIMER") == 32
            assert get(cpu, "RESPAWN_FX_X") == x * 8 + 52
            assert get(cpu, "RESPAWN_FX_Y") == y * 8 + 36
            players = cpu.memory[symbols["PL0"]:symbols["PL0"] + 1024].copy()
            screen = cpu.memory[symbols["GAMESCR"]:symbols["SCORE"] + 69].copy()
            for frame in range(32):
                call(cpu, "RESPAWN_FX_TICK")
                assert get(cpu, "RESPAWN_FX_TIMER") == 31 - frame
                assert cpu.memory[symbols["PL0"]:symbols["PL0"] + 1024] == players
                assert cpu.memory[symbols["GAMESCR"]:symbols["SCORE"] + 69] == screen
                pm = cpu.memory[missiles:missiles + 256]
                if frame < 31:
                    assert cpu.memory[symbols["SIZEM"]] == 0x55
                    assert not any(pm[:32]) and not any(pm[184:])
                    assert sum(bool(b) for b in pm) <= 8, "missile trails left behind"
                    remaining = 32 - frame
                    radius = remaining + remaining // 4
                    for missile in range(4):
                        xpos = cpu.memory[symbols["HPOSM0"] + missile]
                        expected = x * 8 + 52 + (radius if missile & 1 else -radius)
                        assert xpos == (expected if 48 <= expected < 205 else 0)
                        start_y = y * 8 + 36 + (radius if missile >= 2 else -radius)
                        expected_rows = list(range(start_y, start_y + 4)) if 32 <= start_y < 181 else []
                        mask = 3 << (missile * 2)
                        assert [row for row, value in enumerate(pm) if value & mask] == expected_rows
                else:
                    assert pm == hud, "HUD bitmap was not restored exactly"
                    assert cpu.memory[symbols["SIZEM"]] == 0x55
                    assert cpu.memory[symbols["HPOSM0"]:symbols["HPOSM0"] + 4] == [58] * 4
            # A final-spawn echo after completion must not start another cue.
            respawn(cpu, slot, x, y)
            assert get(cpu, "RESPAWN_FX_TIMER") == 0
            cases += 1

for slot in range(4):
    cpu = fresh(slot)
    respawn(cpu, (slot + 1) % 4, 9, 9)
    assert get(cpu, "RESPAWN_FX_TIMER") == 0, "remote spawn triggered local cue"
    respawn(cpu, slot, 9, 9)
    call(cpu, "RESPAWN_FX_TICK")
    respawn(cpu, slot, 9, 9)
    assert get(cpu, "RESPAWN_FX_TIMER") == 31, "in-flight echo restarted cue"
    respawn(cpu, slot, 9, 9, flags=1)
    call(cpu, "RESPAWN_FX_TICK")
    assert get(cpu, "RESPAWN_FX_TIMER") == 0
    assert get(cpu, "RESPAWN_FX_SEEN") == 0
    respawn(cpu, slot, 10, 10)
    assert get(cpu, "RESPAWN_FX_TIMER") == 32, "next local death did not rearm cue"
    for condition in ("NET_LEAVING", "NET_DEAD_MASK", "NET_VACANT_MASK", "NET_LOCAL_PID"):
        cpu = fresh(slot)
        respawn(cpu, slot, 9, 9)
        call(cpu, "RESPAWN_FX_TICK")
        put(cpu, condition, (slot + 1) % 4 if condition == "NET_LOCAL_PID"
            else 1 << slot if condition.endswith("MASK") else 1)
        call(cpu, "RESPAWN_FX_TICK")
        assert get(cpu, "RESPAWN_FX_TIMER") == 0, condition
    cpu = fresh(slot)
    respawn(cpu, slot, 9, 9)
    put(cpu, "NET_GAME_SHOW", 0)
    call(cpu, "RESPAWN_FX_TICK")
    assert get(cpu, "RESPAWN_FX_TIMER") == 32, "hidden reveal consumed cue"

print(f"respawn cue execution passed: {cases} spawn/slot cases, clipping, HUD restore, "
      "local-only trigger, echoes, rearming, cancellation, reveal barrier")
