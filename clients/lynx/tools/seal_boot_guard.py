#!/usr/bin/env python3
"""Seal/verify the immutable CODE+RODATA startup checksum in a linked ROM."""
import argparse
from pathlib import Path
import re


def layout(rom, linker_map):
    data = bytearray(rom.read_bytes())
    text = linker_map.read_text()
    def symbol(name):
        match = re.search(r'\b' + re.escape(name) + r'\s+([0-9A-F]{6})\s+\w+', text)
        if not match:
            raise ValueError(f'missing linker symbol {name}')
        return int(match[1], 16)
    if data[:4] != b'LYNX':
        raise ValueError('expected LNX cartridge')
    # cc65 default directory: first entry follows the signed boot loader.
    directory = 64 + 0xCB
    block = data[directory]
    block_offset = int.from_bytes(data[directory+1:directory+3], 'little')
    destination = int.from_bytes(data[directory+4:directory+6], 'little')
    payload = 64 + block * int.from_bytes(data[4:6], 'little') + block_offset
    start = symbol('__CODE_RUN__')
    end = symbol('__RODATA_RUN__') + symbol('__RODATA_SIZE__')
    expected = symbol('_boot_expected')
    if not destination <= start < end <= expected < symbol('__BSS_RUN__'):
        raise ValueError('checksum/initialized-data layout is invalid')
    first, last, field = (payload + x - destination for x in (start, end, expected))
    if field + 2 > len(data):
        raise ValueError('truncated cartridge')
    return data, first, last, field


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('linker_map', type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    data, first, last, field = layout(args.rom, args.linker_map)
    checksum = (sum(data[first:last]) & 0xFFFF).to_bytes(2, 'little')
    if args.verify:
        if data[field:field+2] != checksum:
            raise SystemExit('startup checksum mismatch')
    else:
        data[field:field+2] = checksum
        args.rom.write_bytes(data)
    print(f'Startup integrity: {last-first} bytes, checksum {int.from_bytes(checksum, "little"):04X}')


if __name__ == '__main__':
    main()
