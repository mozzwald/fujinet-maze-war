#!/usr/bin/env python3
"""Exercise the production cartridge with a controlled ComLynx peer in Handy.

Uses private Xvfb/config/localhost port, no FujiNet firmware or credentials.
ROM may retain its live endpoint: the peer verifies but does not forward it.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import tempfile
import time

spec = importlib.util.spec_from_file_location('wire', Path(__file__).resolve().parents[1] / 'bench/run.py')
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


def main():
    from Xlib import X, display
    from Xlib.XK import string_to_keysym
    from Xlib.ext import xtest
    from PIL import Image
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=Path(__file__).resolve().parents[1] / 'build/maze-war-lynx.lnx')
    args = parser.parse_args()
    rom = args.rom.resolve()
    pc = Path.home() / 'lynx-pc'
    output = Path(tempfile.mkdtemp(prefix='maze-suzy-game-'))
    shutil.copy(pc / 'lynxboot.img', output / 'lynxboot.img')
    (output / 'config').mkdir()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(25)
    processes = []
    peer = d = None
    received = Counter()
    acked = set()
    pending = bytearray()
    sequence = 0
    round_id = 1
    last_delta = 0
    events = []
    try:
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(output / 'config'))
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0', '800x600x24', '-nolisten', 'tcp'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        processes.append(xvfb)
        assert select.select([xvfb.stdout], [], [], 10)[0], 'display startup'
        env['DISPLAY'] = ':' + xvfb.stdout.readline().strip()
        with (output / 'handy.log').open('w') as log:
            handy = subprocess.Popen([str(pc / 'handy_sdl'), str(rom), '-nosound', '-nofullscreen', '-comlynx', f'connect:127.0.0.1:{listener.getsockname()[1]}'], cwd=output, env=env, stdout=log, stderr=log)
        processes.append(handy)
        peer, _ = listener.accept()
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        peer.settimeout(10)
        command = bytearray()
        while len(command) < 3 or len(command) < 4 + command[2]:
            chunk = peer.recv(1)
            assert chunk, 'setup disconnected'
            command.extend(chunk)
        assert command[0] == 0x70 and command[3] == 0xf0
        endpoint = command[6:command.index(0,6)].decode('ascii')
        endpoint_port = int.from_bytes(command[4:6], 'little')
        assert (endpoint, endpoint_port) == ('fujinet.online', 5025), 'expected live test build'
        time.sleep(.02)
        peer.sendall(b'\x06\x06')
        peer.setblocking(False)

        def send(payload):
            peer.sendall(wire.encode(bytes(payload)))

        def snapshot():
            nonlocal sequence
            sequence = (sequence + 1) & 255
            send([0x40, sequence, 0x81, 3,17,7,17,11,17,15,17,
                  7,11,7,11,1,2,3,4,last_delta,round_id])

        def service(seconds, snapshots=False):
            nonlocal last_delta
            end = time.monotonic() + seconds
            next_snap = time.monotonic()
            while time.monotonic() < end:
                if snapshots and time.monotonic() >= next_snap:
                    snapshot()
                    next_snap += .1
                if not select.select([peer], [], [], .01)[0]:
                    assert handy.poll() is None, 'Handy exited'
                    continue
                data = peer.recv(4096)
                assert data, 'ComLynx disconnected'
                pending.extend(data)
                while 0 in pending:
                    at = pending.index(0)
                    encoded = bytes(pending[:at])
                    del pending[:at + 1]
                    if not encoded:
                        continue
                    p = wire.decode(encoded)
                    received[hex(p[0])] += 1
                    if p[0] == 0x45:
                        acked.add(int.from_bytes(p[2:4], 'little'))
                    elif p[0] == 0x41:
                        last_delta = p[1]
                        events.append(dict(delta=list(p), at=time.monotonic()))
                    elif p[0] == 0x43:
                        send(p)

        def reliable(rev, payload):
            for _ in range(4):
                send([0x53, 0, rev & 255, rev >> 8] + payload)
                service(.45)
                if rev in acked:
                    return
            raise AssertionError(f'reliable ACK missing: {rev}')

        def maze():
            bricks = bytearray(48)
            for y in range(19):
                for x in range(20):
                    if x in (0,19) or y in (0,18) or (x,y) == (10,10):
                        cell = y*20+x
                        bricks[cell//8] |= 1 << (cell%8)
            return list(bricks)

        def capture(name):
            path = output / (name+'.png')
            subprocess.run(['import', '-display', env['DISPLAY'], '-window', 'root', str(path)], check=True, timeout=10)
            return Image.open(path).convert('RGB').crop((160,147,640,432)).resize((160,95), Image.Resampling.NEAREST)

        send([0x47,1,1,0,10])
        service(.3)
        send([0x44,0,15])
        reliable(1,[0x55,1,10])
        reliable(2,[0x50,0,1]+maze()+[1])
        for i,name in enumerate(('LYNX    ','ATARI8  ','PLAYER3 ','PLAYER4 ')):
            send([0x43,0,i]+list(name.encode('ascii')))
            service(.15)
        service(2,True)
        before = capture('before')
        reliable(3,[0x51,0,10,10,1])
        service(1,True)
        destroyed = capture('destroyed')
        assert before.getpixel((80,50)) != destroyed.getpixel((80,50)), 'brick did not disappear'
        assert destroyed.getpixel((25,89)) != destroyed.getpixel((24,89)), 'last-row feet missing'
        assert destroyed.getpixel((24,90)) != destroyed.getpixel((27,90)), 'border texture missing'
        d = display.Display(env['DISPLAY'])
        def windows(w):
            yield w
            for child in w.query_tree().children:
                yield from windows(child)
        window = next(w for w in windows(d.screen().root) if w.get_wm_name() == 'Handy/SDL')
        window.set_input_focus(X.RevertToParent, X.CurrentTime)
        def key(name,down):
            xtest.fake_input(d,X.KeyPress if down else X.KeyRelease,d.keysym_to_keycode(string_to_keysym(name)))
            d.sync()
        key('z',True)
        service(.4,True)
        key('z',False)
        service(.4,True)
        assert any(e['delta'][3] & 0x10 for e in events), 'A/fire input not transmitted'
        key('Right',True)
        service(.3,True)
        key('Right',False)
        service(.8,True)
        assert any(e['delta'][3] & 15 == 7 for e in events), 'movement input not transmitted'
        # Return to one stable scene before checking diagnostics cache invalidation.
        service(1,True)
        stable = capture('stable')
        key('x',True)
        service(.8,True)
        capture('diagnostics')
        key('x',False)
        service(2,True)
        restored = capture('restored')
        assert stable.tobytes() == restored.tobytes(), 'diagnostics left stale maze pixels'
        round_id = 2
        reliable(4,[0x55,2,10])
        reliable(5,[0x50,0,1]+maze()+[2])
        service(2,True)
        restarted = capture('new-round')
        assert restarted.getpixel((80,50)) == before.getpixel((80,50)), 'new round did not restore brick'
        result = dict(endpoint=endpoint, port=endpoint_port, rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                      handy_sha256=hashlib.sha256((pc/'handy_sdl').read_bytes()).hexdigest(),
                      received=dict(received), acknowledged=sorted(acked),
                      checks=['brick removal','bottom feet','border texture','fire input','movement input','diagnostics restoration','new round'],
                      method='Production ROM; controlled direct ComLynx peer, 10 Hz snapshots; no firmware/WAN')
        (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(output=str(output),**result),indent=2))
    finally:
        if d:
            d.close()
        if peer:
            peer.close()
        listener.close()
        for p in reversed(processes):
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()


if __name__ == '__main__':
    main()
