#!/usr/bin/env python3
"""Validate and summarize instruction-observer logs from completed Handy runs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from build_handy_memory_probe import symbol


def summarize(directory, linker_map):
    records = [json.loads(line.split('MAZE_MEMORY ',1)[1]) for line in
               (directory/'handy.log').read_text().splitlines() if line.startswith('MAZE_MEMORY ')]
    assert records and records[0]['event'] == 'armed', 'observer never armed'
    failures = [r for r in records if any(word in r['event'] for word in
                ('corruption','_write','exhausted','out_of_bounds','mismatch','unexpected'))]
    assert not failures, failures
    assert records[-1]['scans'] >= 2, 'insufficient guard scans'
    text = linker_map.read_text()
    sym = lambda name: symbol(text, name)
    capacity = sym('__STACKSIZE__')
    top = sym('__MAIN_START__')+sym('__MAIN_SIZE__')+capacity
    raw = min(r['raw_min'] for r in records)
    access = min(r['access_min'] for r in records)
    hardware = max(r['hardware_used'] for r in records)
    assert top-capacity <= raw <= access <= top and hardware < 256
    cartridge = directory/'cartridge.lnx'
    if not cartridge.exists(): cartridge = directory/'rom.lnx'
    assert cartridge.exists(), 'missing immutable tested cartridge copy'
    return dict(artifact_directory=str(directory),
                rom_sha256=hashlib.sha256(cartridge.read_bytes()).hexdigest(),
                software_conservative_bytes=top-raw, software_access_bytes=top-access,
                software_written_bytes=max(r['software_written'] for r in records),
                software_headroom_bytes=raw-(top-capacity),
                hardware_bytes=hardware, hardware_headroom_bytes=256-hardware,
                instructions=records[-1]['instructions'], scans=records[-1]['scans'],
                checkpoints=[r['event'] for r in records if r['event'].startswith('_') or r['event']=='initlib'],
                guard_or_code_failures=failures)


def main():
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directories', type=Path, nargs='+')
    p.add_argument('--map', type=Path, default=root/'build/maze-war-lynx.map')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    results = [summarize(directory,args.map) for directory in args.directories]
    text = json.dumps(results,indent=2)+'\n'
    if args.output: args.output.write_text(text)
    print(text,end='')


if __name__ == '__main__':
    main()
