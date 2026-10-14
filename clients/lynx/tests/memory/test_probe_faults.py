#!/usr/bin/env python3
"""Prove the host observer rejects stack/guard damage in private ROM copies."""
import argparse
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from build_handy_memory_probe import symbol


def main():
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    args = p.parse_args()
    rom = root/'build/maze-war-lynx.lnx'
    data = rom.read_bytes()
    text = (root/'build/maze-war-lynx.map').read_text()
    sym = lambda name: symbol(text, name)
    entry = sym('_main')
    bss_end = sym('__BSS_RUN__')+sym('__BSS_SIZE__')
    bottom = sym('__MAIN_START__')+sym('__MAIN_SIZE__')
    zp_end = sym('__APPZP_RUN__')+sym('__APPZP_SIZE__')
    sp = sym('c_sp')
    directory = 64+0xcb
    payload = (64+data[directory]*int.from_bytes(data[4:6], 'little')+
               int.from_bytes(data[directory+1:directory+3], 'little'))
    destination = int.from_bytes(data[directory+4:directory+6], 'little')
    word = lambda a: bytes([a & 255, a >> 8])
    loop = bytes([0x4c])+word(entry)
    cases = {
        'rom_map_mismatch': bytes([0xea]),
        'software_stack_out_of_bounds': bytes([0xa9,(bottom-1)&255,0x85,sp,
                                              0xa9,(bottom-1)>>8,0x85,sp+1])+loop,
        'hardware_stack_exhausted': bytes([0xa2,0xff,0x9a,0xa9,0,0x48,0x80,0xfd]),
        'padding_write': bytes([0xa9,0,0x8d])+word(bss_end)+loop,
        'zero_page_write': bytes([0xa9,0,0x85,zp_end])+loop,
        'immutable_write': bytes([0xa9,0,0x8d])+word(sym('__CODE_RUN__'))+loop,
    }
    output = Path(tempfile.mkdtemp(prefix='maze-memory-faults-'))
    shutil.copyfile(Path.home()/'lynx-pc/lynxboot.img', output/'lynxboot.img')
    processes = []
    results = {}
    try:
        xvfb = subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],
                                stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        processes.append(xvfb)
        assert select.select([xvfb.stdout],[],[],10)[0]
        env = dict(os.environ, DISPLAY=':'+xvfb.stdout.readline().strip(),
                   SDL_AUDIODRIVER='dummy', MAZE_MEMORY_ALLOW_PATCHED_ROM='1', XDG_CONFIG_HOME=str(output/'config'))
        for event, code in cases.items():
            case_env = dict(env)
            if event == 'rom_map_mismatch': case_env.pop('MAZE_MEMORY_ALLOW_PATCHED_ROM')
            private = bytearray(data)
            offset = payload+entry-destination
            private[offset:offset+len(code)] = code
            path = output/(event+'.lnx')
            path.write_bytes(private)
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(15)
                with (output/(event+'.log')).open('w') as log:
                    emulator = subprocess.Popen([str(args.emulator),str(path),'-nosound','-nofullscreen',
                        '-noautoskip','-frameskip','0','-throttle','-comlynx',
                        f'connect:127.0.0.1:{listener.getsockname()[1]}'],cwd=output,env=case_env,
                        stdout=log,stderr=log)
                    processes.append(emulator)
                    peer,_ = listener.accept()
                    with peer:
                        assert emulator.wait(timeout=20) == 86, event
            records = [json.loads(line.split('MAZE_MEMORY ',1)[1]) for line in
                       (output/(event+'.log')).read_text().splitlines() if line.startswith('MAZE_MEMORY ')]
            assert records[-1]['event'] == event, (event, records[-1])
            results[event] = records[-1]
        (output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(json.dumps({'output':str(output),'faults_detected':list(results)},indent=2))
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__ == '__main__':
    main()
