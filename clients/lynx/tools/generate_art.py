#!/usr/bin/env python3
"""Validate editable Maze War art and encode hardware-safe Suzy graphics."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'art/maze-war-tiles.json'
# Dimensions, binding order and pen semantics are renderer contracts.
SPECS = [(key, 8, 5, 'palette', 'terrain') for key in
         ('floor_tile', 'wall_even', 'wall_odd', 'border_tile')]
SPECS += [('player_'+str(i), 8, 5, 'player', 'player') for i in range(4)]
SPECS += [('dance_'+str(i), 8, 5, 'player', 'dance') for i in range(1, 5)]
SPECS += [('explosion_'+str(i), 12, 9, 'palette', 'explosion') for i in range(1, 5)]
SPECS += [('projectile', 2, 2, 'palette', 'effect'), ('muzzle', 2, 1, 'palette', 'effect')]
SPECS += [('glyph_'+str(ord(c)), 3, 4, 'font', 'font') for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./']


def default_palette():
    words = re.findall(r'>\$([0-9a-fA-F]{3})', (ROOT / 'src/lynx_tgi.s').read_text())[:16]
    return ['#'+word[2]*2+word[0]*2+word[1]*2 for word in words]


def validate(project):
    if not isinstance(project, dict) or project.get('projectType') != 'maze-war-lynx-tiles' or type(project.get('version')) is not int or project['version'] != 1:
        raise ValueError('Expected Maze War Lynx tiles project version 1')
    if project.get('palette') != default_palette():
        raise ValueError('Palette must match the resident Lynx TGI palette; edit pixels, not palette metadata')
    entries = project.get('entries')
    if not isinstance(entries, list) or len(entries) != len(SPECS):
        raise ValueError(f'Expected {len(SPECS)} images')
    images = {}
    for e, (key, w, h, mode, kind) in zip(entries, SPECS):
        if not isinstance(e, dict) or e.get('id') != key or e.get('mode') != mode or e.get('kind') != kind or type(e.get('width')) is not int or type(e.get('height')) is not int or (e['width'], e['height']) != (w, h):
            raise ValueError(f'{key}: binding, dimensions and mode must remain unchanged')
        if not isinstance(e.get('name'), str) or not e['name'].strip():
            raise ValueError(f'{key}: name must be nonempty text')
        rows = e.get('rows')
        allowed = '01' if mode == 'font' else '01234' if mode == 'player' else '0123456789abcdefABCDEF'
        if not isinstance(rows, list) or len(rows) != h or any(not isinstance(row, str) or len(row) != w or any(c not in allowed for c in row) for row in rows):
            raise ValueError(f'{key}: expected {h} rows of {w} pixels using {allowed}')
        images[key] = [[int(c, 16) for c in row] for row in rows]
    return images


def literal(rows):
    out = []
    for row in rows:
        out += [len(row)//2+2] + [a*16+b for a,b in zip(row[::2], row[1::2])] + [0]
    return out + [0]


def counted(rows):
    # Eight pixels in one literal packet inside a counted row. End guard and
    # terminator retained: real Suzy can read past byte-aligned literal data.
    out = []
    for row in rows:
        bits = '10111' + ''.join(f'{v:04b}' for v in row)
        bits += '0' * ((-len(bits)) % 8)
        out += [7] + [int(bits[i:i+8], 2) for i in range(0,len(bits),8)] + [0]
    return out + [0]


def array(name, values):
    size = len(values[0])
    assert all(len(v) == size for v in values)
    return f'static const uint8_t {name}[{len(values)}][{size}] = {{\n' + ''.join('    {'+','.join(f'0x{v:02x}' for v in row)+'},\n' for row in values) + '};\n'


def outputs(project):
    images = validate(project)
    banner = '/* Generated from art/maze-war-tiles.json by tools/generate_art.py; do not edit. */\n'
    tiles = [images[key] for key, *_ in SPECS[:4]]
    players = [images[key] for key, *_ in SPECS[4:12]]
    maze = banner + array('tile_sprites', [counted(rows) for rows in tiles])
    maze += array('wizard_sprites', [literal(rows) for rows in players])
    maze += 'static const uint8_t shot_sprite[] = {'+','.join(f'0x{v:02x}' for v in literal(images['projectile']))+'};\n'
    maze += f"#define ART_MUZZLE_LEFT 0x{images['muzzle'][0][0]<<4:02x}\n#define ART_MUZZLE_RIGHT 0x{images['muzzle'][0][1]:02x}\n"
    death = banner + array('death_sprites', [literal(images['explosion_'+str(i)]) for i in range(1,5)])
    font = banner + array('mini_font', [[sum(v << (2-x) for x,v in enumerate(row)) for row in images[key]] for key,*_ in SPECS[18:]])
    # Independent unencoded pixels for the host CPU oracle and renderer tests.
    oracle = banner + '#ifndef MAZE_ART_PIXELS_H\n#define MAZE_ART_PIXELS_H\n#include <stdint.h>\n'
    for key,*_ in SPECS:
        oracle += array('art_'+key, images[key])
    for key, *_ in SPECS[:4]:
        raw = [a*16+b for row in images[key] for a,b in zip(row[::2],row[1::2])]
        oracle += f'static const uint8_t {key}[20] = {{' + ','.join(f'0x{v:02x}' for v in raw) + '};\n'
    oracle += 'static const uint8_t (*const art_players[8])[8] = {' + ','.join('art_'+key for key,*_ in SPECS[4:12]) + '};\n'
    oracle += 'static const uint8_t (*const art_explosions[4])[12] = {' + ','.join('art_explosion_'+str(i) for i in range(1,5)) + '};\n'
    oracle += '#endif\n'
    return {'maze_sprites.h': maze, 'death_sprites.h': death, 'font_art.h': font, 'art_pixels.h': oracle}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--project', type=Path, default=PROJECT)
    args = parser.parse_args()
    try:
        generated = outputs(json.loads(args.project.read_text()))
    except (ValueError, OSError) as error:
        parser.exit(1, f'Art error: {error}\n')
    for name, text in generated.items():
        target = ROOT / 'src' / name
        if args.check:
            if not target.exists() or target.read_text() != text:
                parser.exit(1, f'{name} is stale; run make -C clients/lynx art\n')
        elif not target.exists() or target.read_text() != text:
            target.write_text(text)


if __name__ == '__main__':
    main()
