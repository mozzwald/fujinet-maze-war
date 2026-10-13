#!/usr/bin/env python3
"""Preencode existing maze/player artwork; preserve counted tiles and guards."""
import argparse
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
art = (root / 'src/maze_art.h').read_text()
tiles = []
for name in ('floor_tile', 'wall_even', 'wall_odd', 'border_tile'):
    body = re.search(r'\b'+name+r'\[20\] = \{(.*?)\};', art, re.S)[1]
    raw = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', body)]
    assert len(raw) == 20
    sprite = []
    for y in range(5):
        row = raw[y*4:y*4+4]
        sprite += [7, 0xb8 | (row[0] >> 5)]
        sprite += [((row[i] << 3) | (row[i+1] >> 5)) & 255 for i in range(3)]
        sprite += [(row[3] << 3) & 255, 0]
    tiles.append(sprite + [0])
rows = re.findall(r'"([.CPWS]{8})"', art)
assert len(rows) == 25
pens = {'.': 0, 'C': 1, 'P': 2, 'W': 3, 'S': 4}
wizards = []
for image in range(8):
    sprite = []
    for y in range(5):
        row = rows[y] if image < 4 else rows[5+(image-4)*5+y]
        values = [pens[row[7-x if image < 4 and image & 1 else x]] for x in range(8)]
        if image < 4 and image & 2 and y == 2:
            values[0] = 3
        sprite += [6] + [values[x]*16+values[x+1] for x in range(0,8,2)] + [0]
    wizards.append(sprite + [0])
lines = ['/* Generated from maze_art.h by tools/generate_maze_sprites.py. */']
for name, sprites, size in [('tile_sprites', tiles, 36), ('wizard_sprites', wizards, 31)]:
    lines.append(f'static const uint8_t {name}[{len(sprites)}][{size}] = {{')
    for sprite in sprites:
        assert len(sprite) == size
        lines.append('    {' + ','.join(f'0x{x:02x}' for x in sprite) + '},')
    lines.append('};')
expected = '\n'.join(lines) + '\n'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
target = root / 'src/maze_sprites.h'
if args.check:
    if target.read_text() != expected:
        raise SystemExit('maze_sprites.h is stale; run tools/generate_maze_sprites.py')
else:
    target.write_text(expected)
