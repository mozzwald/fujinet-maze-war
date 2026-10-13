#!/usr/bin/env python3
"""Reject unsafe Suzy control-block placement and video/stack overlap."""
import argparse
from pathlib import Path
import re


def check(rom, linker_map):
    text = linker_map.read_text()

    def symbol(name):
        match = re.search(r'\b' + re.escape(name) + r'\s+([0-9A-F]{6})\s+\w+', text)
        if not match:
            raise ValueError(f'missing linker symbol {name}')
        return int(match[1], 16)

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def one_page(address, size, label):
        require(address >> 8 == (address + size - 1) >> 8,
                f'{label} crosses a page at ${address:04X}')

    require(rom.read_bytes()[:4] == b'LYNX', 'expected LNX cartridge')
    require('lynx-160-102-16-tgi.o' not in text, 'stock TGI driver linked unexpectedly')
    data_start = symbol('__DATA_RUN__')
    data_end = data_start + symbol('__DATA_SIZE__')
    for name, size, palette_offset in (
        ('cls', 16, 15), ('pixel', 16, 15), ('line', 20, 19),
        ('bar', 16, 15), ('text', 16, 15),
    ):
        address = symbol(f'maze_tgi_{name}_sprite')
        require(data_start <= address and address + size <= data_end,
                f'TGI {name} control block outside DATA')
        one_page(address, size, f'TGI {name} control block')
        require((address + palette_offset) & 255 != 0xFA,
                f'TGI {name} palette hits Suzy $xxFA erratum')

    zp_start, zp_size = symbol('__APPZP_RUN__'), symbol('__APPZP_SIZE__')
    require(zp_start >= 0x2b and zp_start + zp_size <= 0x100,
            'application zero-page scratch overlaps runtime or exceeds page zero')
    bss_start = symbol('__BSS_RUN__')
    bss_end = bss_start + symbol('__BSS_SIZE__')
    for name, size in (('maze_tgi_text_bitmap', 177), ('_maze_scbs', 184),
                       ('_maze_text_scb', 23)):
        address = symbol(name)
        require(bss_start <= address and address + size <= bss_end,
                f'{name} outside BSS')
        one_page(address, size, name)
    scbs = symbol('_maze_scbs')
    compact = symbol('_maze_text_scb')
    require(scbs & 255 == 0 and compact == scbs + 184,
            'game control blocks must share one aligned page')
    require(symbol('maze_tgi_text_bitmap') & 255 == 0,
            'TGI text bitmap must be page aligned')
    for address in [scbs + 23 * index for index in range(8)] + [compact]:
        require((address + 15) & 255 != 0xFA,
                'game palette hits Suzy $xxFA erratum')
    require(symbol('__STACKSIZE__') == 0x800, '2 KB software stack must be retained')
    stack_bottom = 0xC038 - symbol('__STACKSIZE__')
    require(bss_end <= stack_bottom, 'BSS overlaps reserved stack/video RAM')
    print(f'Graphics layout: SCBs ${scbs:04X}, text ${symbol("maze_tgi_text_bitmap"):04X}; '
          f'{stack_bottom - bss_end} bytes before full 2 KB stack; '
          f'APPZP ${zp_start:02X}..${zp_start+zp_size-1:02X}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('linker_map', type=Path)
    args = parser.parse_args()
    try:
        check(args.rom, args.linker_map)
    except ValueError as error:
        raise SystemExit(f'Graphics layout rejected: {error}') from error


if __name__ == '__main__':
    main()
