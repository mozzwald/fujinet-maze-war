#!/usr/bin/env python3
"""Run the benchmark ROM in the installed Handy with a paced ComLynx peer.

The peer only acknowledges NetStream setup and injects harmless, CRC-valid
shot-clear frames. No firmware, external server, credentials, or settings are
modified. RX metrics exercise the client's actual cc65 serial driver/parser.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import select
import shutil
import socket
import statistics
import struct
import subprocess
import tempfile
import time


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def encode(payload):
    data = payload + struct.pack('<H', crc16(payload))
    out = bytearray([0])
    code_at, code = 0, 1
    for byte in data:
        if not byte:
            out[code_at] = code
            code_at, code = len(out), 1
            out.append(0)
        else:
            out.append(byte)
            code += 1
    out[code_at] = code
    out.append(0)
    return bytes(out)


def decode(wire):
    out = bytearray()
    i = 0
    while i < len(wire):
        code = wire[i]
        if not code or i + code > len(wire) + 1:
            raise ValueError('invalid COBS')
        out.extend(wire[i + 1:i + code])
        i += code
        if code != 255 and i < len(wire):
            out.append(0)
    if len(out) < 3 or crc16(out[:-2]) != int.from_bytes(out[-2:], 'little'):
        raise ValueError('invalid CRC')
    return bytes(out[:-2])


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


MODES = ['CPU current', 'CPU changed cells', 'Suzy batch 4',
         'Suzy batch 8', 'Suzy batch 16']
SCENARIOS = ['initial two pages', 'one player + one shot',
             'four players + three shots', '12 brick removals, both pages',
             'shot clear, unchanged maze']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--traffic-hz', type=float, default=50)
    parser.add_argument('--handy', type=Path, default=Path.home() / 'lynx-pc/handy_sdl')
    parser.add_argument('--rom', type=Path, default=Path(__file__).resolve().parent.parent /
                        'build/render-bench.lnx')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    args.rom = args.rom.resolve()
    args.handy = args.handy.resolve()
    if args.traffic_hz < 0:
        parser.error('traffic rate must be nonnegative')
    output = (args.output or Path(tempfile.mkdtemp(prefix='maze-suzy-bench-'))).resolve()
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy(Path.home() / 'lynx-pc/lynxboot.img', output / 'lynxboot.img')
    (output / 'config').mkdir(exist_ok=True)
    results = []
    peer = None
    xvfb = handy = None
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(20)
    port = listener.getsockname()[1]
    env = dict(os.environ, SDL_AUDIODRIVER='dummy',
               XDG_CONFIG_HOME=str(output / 'config'))
    metadata = dict(traffic_hz=args.traffic_hz,
                    offered_bytes_per_second=args.traffic_hz * 11,
                    rom=str(args.rom), rom_sha256=digest(args.rom),
                    handy=str(args.handy), handy_sha256=digest(args.handy),
                    method='Mikey timers 1/3/5, 24-bit microseconds; 60 Hz display')
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    try:
        with open(output / 'xvfb.log', 'w') as xlog, open(output / 'handy.log', 'w') as hlog:
            xvfb = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0',
                                     '800x600x24', '-nolisten', 'tcp'],
                                    stdout=subprocess.PIPE, stderr=xlog, text=True)
            if not select.select([xvfb.stdout], [], [], 10)[0]:
                raise RuntimeError('Xvfb did not allocate a display')
            env['DISPLAY'] = ':' + xvfb.stdout.readline().strip()
            handy = subprocess.Popen([str(args.handy), str(args.rom), '-nosound',
                                      '-nofullscreen', '-comlynx',
                                      f'connect:127.0.0.1:{port}'],
                                     cwd=output, env=env, stdout=hlog, stderr=hlog)
            peer, _ = listener.accept()
            peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            peer.settimeout(10)
            command = bytearray()
            while len(command) < 3 or len(command) < 4 + command[2]:
                byte = peer.recv(1)
                if not byte:
                    raise RuntimeError('ComLynx disconnected during setup')
                command.extend(byte)
            if command[0] != 0x70 or command[3] != 0xf0:
                raise RuntimeError(f'unexpected setup: {command.hex()}')
            time.sleep(0.02)
            peer.sendall(b'\x06\x06')
            time.sleep(0.03)
            peer.sendall(encode(bytes([0x47, 1, 1, 0, 10])))
            peer.setblocking(False)
            wire = bytearray()
            active = None
            next_injection = float('inf')
            seq = 0
            deadline = time.monotonic() + 300
            finished = False
            with open(output / 'events.jsonl', 'w', buffering=1) as events:
                while not finished and time.monotonic() < deadline:
                    now = time.monotonic()
                    if args.traffic_hz and active and now >= next_injection:
                        peer.sendall(encode(bytes([0x42, seq, 3, 0, 0, 0, 1])))
                        seq = (seq + 1) & 255
                        active['offered_frames'] += 1
                        next_injection = max(next_injection + 1 / args.traffic_hz,
                                             now + 0.0001)
                    timeout = min(0.02, max(0, next_injection - time.monotonic()))
                    if not select.select([peer], [], [], timeout)[0]:
                        if handy.poll() is not None:
                            raise RuntimeError(f'Handy exited {handy.returncode}')
                        continue
                    chunk = peer.recv(4096)
                    if not chunk:
                        raise RuntimeError('ComLynx disconnected')
                    wire.extend(chunk)
                    while 0 in wire:
                        at = wire.index(0)
                        encoded = bytes(wire[:at])
                        del wire[:at + 1]
                        if not encoded:
                            continue
                        payload = decode(encoded)
                        events.write(json.dumps(dict(host_time=time.monotonic(),
                                                     payload=payload.hex())) + '\n')
                        if payload[0] == 0x7c:
                            active = dict(mode=payload[1], scenario=payload[2],
                                          batch=payload[3], count=payload[4],
                                          offered_frames=0, host_start=time.monotonic(),
                                          render_us=[], frame_us=[])
                            results.append(active)
                            next_injection = time.monotonic() + 0.005
                        elif payload[0] == 0x7d:
                            result = results[-1]
                            assert (payload[1], payload[2]) == (result['mode'], result['scenario'])
                            result['elapsed_us'], result['max_poll_gap_us'], result['max_batch_us'] = \
                                struct.unpack_from('<III', payload, 4)
                            fields = ['rx_valid', 'rx_bad', 'serial_errors', 'rx_ring_full',
                                      'missing_sequence_frames', 'poll_calls']
                            result.update(zip(fields, struct.unpack_from('<HHHHHH', payload, 16)))
                            result['host_elapsed'] = time.monotonic() - result['host_start']
                            active = None
                            next_injection = float('inf')
                        elif payload[0] == 0x7e:
                            result = results[-1]
                            assert payload[3] == len(result['render_us'])
                            for i in range(payload[4]):
                                render, frame = struct.unpack_from('<II', payload, 5 + i * 8)
                                result['render_us'].append(render)
                                result['frame_us'].append(frame)
                        elif payload[0] == 0x7b:
                            subprocess.run(['import', '-display', env['DISPLAY'], '-window', 'root',
                                            str(output / f'mode-{payload[1]}.png')],
                                           env=env, check=True, timeout=10)
                        elif payload[0] == 0x7f:
                            finished = True
            if not finished:
                raise RuntimeError('benchmark timed out')
    finally:
        if peer:
            peer.close()
        listener.close()
        for process in (handy, xvfb):
            if process:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if len(results) != 25 or any(len(r['render_us']) != r['count'] for r in results):
        raise RuntimeError('incomplete timing reports')
    lines = ['| Renderer | Scenario | Full cycle mean (ms) | '
             'Render median / p95 / max (ms) | Render+swap median (ms) | '
             'Longest poll gap (ms) | RX valid / bad / missing |',
             '|---|---|---|---|---|---|---|']
    for result in results:
        values = sorted(result['render_us'])
        median = statistics.median(values) / 1000
        p95 = values[math.ceil(len(values) * .95) - 1] / 1000
        lines.append(f"| {MODES[result['mode']]} | {SCENARIOS[result['scenario']]} | "
                     f"{result['elapsed_us']/result['count']/1000:.3f} | "
                     f"{median:.3f} / {p95:.3f} / {max(values)/1000:.3f} | "
                     f"{statistics.median(result['frame_us'])/1000:.3f} | "
                     f"{result['max_poll_gap_us']/1000:.3f} | "
                     f"{result['rx_valid']} / {result['rx_bad']} / "
                     f"{result['missing_sequence_frames']} |")
    (output / 'table.md').write_text('\n'.join(lines) + '\n')
    from PIL import Image, ImageChops
    baseline = Image.open(output / 'mode-0.png').convert('RGB').crop((160, 147, 640, 432))
    comparisons = {}
    for mode in range(1, 5):
        image = Image.open(output / f'mode-{mode}.png').convert('RGB').crop((160, 147, 640, 432))
        diff = ImageChops.difference(baseline, image)
        comparisons[MODES[mode]] = dict(bounds=diff.getbbox(),
            differing_pixels=sum(pixel != (0, 0, 0) for pixel in diff.getdata()))
        if diff.getbbox():
            diff.save(output / f'diff-{mode}.png')
    (output / 'pixel-comparison.json').write_text(json.dumps(comparisons, indent=2) + '\n')
    print(output, flush=True)


if __name__ == '__main__':
    main()
