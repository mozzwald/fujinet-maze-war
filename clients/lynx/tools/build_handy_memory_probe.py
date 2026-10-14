#!/usr/bin/env python3
"""Build an isolated, instruction-level Handy observer for an unchanged ROM.

Copies source into --output; never edits installed Handy, original source,
Lynx ROM or FujiNet firmware. Run harnesses with MAZE_HANDY_BINARY=<output>/handy_sdl.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def symbol(text, name):
    match = re.search(r'\b' + re.escape(name) + r'\s+([0-9A-F]{6})\s+\w+', text)
    if not match:
        raise ValueError(f'missing linker symbol {name}')
    return int(match[1], 16)


def config(rom, linker_map):
    data = rom.read_bytes()
    assert data[:4] == b'LYNX'
    text = linker_map.read_text()
    sym = lambda name: symbol(text, name)
    directory = 64 + 0xcb
    block = data[directory]
    offset = int.from_bytes(data[directory+1:directory+3], 'little')
    destination = int.from_bytes(data[directory+4:directory+6], 'little')
    payload = 64 + block * int.from_bytes(data[4:6], 'little') + offset
    start = sym('__STARTUP_RUN__')
    startup = data[payload+start-destination:payload+start-destination+sym('__STARTUP_SIZE__')]
    sp = sym('c_sp')
    pattern = bytes([0x85, sp, 0x86, sp+1])  # crt0's STA c_sp / STX c_sp+1
    assert startup.count(pattern) == 1, 'unrecognized crt0 stack initialization'
    arm = start + startup.index(pattern) + len(pattern)
    top = sym('__MAIN_START__') + sym('__MAIN_SIZE__') + sym('__STACKSIZE__')
    values = dict(ARM_PC=arm, C_SP=sp, STACK_TOP=top,
                  STACK_BOTTOM=top-sym('__STACKSIZE__'),
                  BSS_END=sym('__BSS_RUN__')+sym('__BSS_SIZE__'),
                  APPZP_END=sym('__APPZP_RUN__')+sym('__APPZP_SIZE__'),
                  CODE_START=sym('__CODE_RUN__'),
                  CODE_END=sym('__RODATA_RUN__')+sym('__RODATA_SIZE__'))
    expected_hash = 2166136261
    for b in data[payload+values['CODE_START']-destination:payload+values['CODE_END']-destination]:
        expected_hash = ((expected_hash ^ b) * 16777619) & 0xffffffff
    values['EXPECTED_HASH'] = expected_hash
    assert values['BSS_END'] <= values['STACK_BOTTOM'] < values['STACK_TOP'] <= 0xfc00
    assert values['APPZP_END'] <= 0x100
    assert values['CODE_START'] < values['CODE_END'] <= sym('__DATA_RUN__')
    names = ['initlib' , '_main', '_startup_run', '_setup_load_name', '_setup_save_name',
             '_command_complete', '_netstream_start',
             '_render_init', '_render_draw', '_render_small_text', '_playfield_draw',
             '_game_receive', '_netstream_retry']
    names = [name for name in names if re.search(r'\b'+re.escape(name)+r'\s+[0-9A-F]{6}\s', text)]
    lines = [f'#define MAZE_{key} 0x{value:x}' for key, value in values.items()]
    lines += [f'#define MAZE_CHECKPOINT_COUNT {len(names)}',
              'static const unsigned maze_checkpoints[] = {'+','.join(str(sym(n)) for n in names)+'};',
              'static const char *maze_checkpoint_names[] = {'+','.join('"'+n+'"' for n in names)+'};']
    return '\n'.join(lines)+'\n'


def main():
    root = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=Path.home()/'build/handy-sdl')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--rom', type=Path, default=root/'build/maze-war-lynx.lnx')
    p.add_argument('--map', type=Path, default=root/'build/maze-war-lynx.map')
    args = p.parse_args()
    assert not args.output.resolve().is_relative_to(args.source.resolve()), 'output must be outside original Handy source'
    assert not args.output.exists(), 'use a new, empty output directory'
    args.output.mkdir(parents=True)
    shutil.copytree(args.source/'src', args.output/'src')
    shutil.copyfile(args.source/'Makefile', args.output/'Makefile')
    core = args.output/'src/handy-0.95'
    shutil.copyfile(root/'tests/memory/handy_memory_probe.h', core/'handy_memory_probe.h')
    (core/'maze_memory_config.h').write_text(config(args.rom, args.map))
    path = core/'c65c02.h'
    text = path.read_text()
    needle = '#include "c6502mak.h"'
    assert text.count(needle) == 1
    text = text.replace(needle, '#include "handy_memory_probe.h"\n'+needle)
    needle = 'inline void Update(void)\n\t\t{'
    assert text.count(needle) == 1
    text = text.replace(needle, needle+'\n            maze_memory_instruction(mRamPointer, mPC, mSP);')
    needle = 'if(m1<0xfc00) mRamPointer[m1]=m2;'
    assert text.count(needle) == 1
    text = text.replace(needle, 'maze_memory_write(m1); '+needle)
    needle = 'if(gSystemIRQ && !mI)\n\t{'
    assert text.count(needle) == 1
    text = text.replace(needle, needle+'\n        maze_memory_irq_enter();')
    path.write_text(text)
    path = core/'c6502mak.h'
    text = path.read_text()
    needle = '{ CPU_POKE(0x0100+mSP,m); mSP--;'
    assert text.count(needle) == 1
    text = text.replace(needle, '{ maze_memory_push(mSP,mPC); CPU_POKE(0x0100+mSP,m); mSP--;')
    needle = '#define\txRTI()'
    start = text.index(needle)
    end = text.index('#define\txRTS()',start)
    section = text[start:end]
    needle = 'mPC|=tmp<<8;'
    assert section.count(needle) == 1
    section = section.replace(needle, needle+' maze_memory_irq_exit();')
    path.write_text(text[:start]+section+text[end:])
    subprocess.run(['make', '-j2'], cwd=args.output, check=True)
    print('MAZE_HANDY_BINARY='+str(args.output/'handy_sdl'))


if __name__ == '__main__':
    main()
