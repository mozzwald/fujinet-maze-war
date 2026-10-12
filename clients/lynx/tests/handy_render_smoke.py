#!/usr/bin/env python3
"""Exercise the production cartridge with a controlled ComLynx peer in Handy.

Uses private Xvfb/config/localhost port, no FujiNet firmware or credentials.
ROM may retain its live endpoint: the peer verifies but does not forward it.
"""
import argparse
from collections import Counter, deque
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import struct
import tempfile
import time
import wave

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
    parser.add_argument("--movement-checks", action="store_true")
    parser.add_argument("--death-checks", action="store_true")
    parser.add_argument("--round-checks", action="store_true")
    parser.add_argument("--sound-checks", action="store_true", help="capture Handy SDL audio using a test-only preload")
    parser.add_argument("--input-lag-ms", type=float, default=0)
    args = parser.parse_args()
    if sum((args.movement_checks, args.death_checks, args.round_checks)) > 1:
        parser.error('run movement, death and round checks separately')
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
    input_queue = deque()
    local = [3,17]
    dead_slots = set()
    remote_positions = [[7,17],[11,17],[15,17]]
    simulate_moves = args.movement_checks or args.death_checks
    death_results = []
    round_results = []
    applied_moves = []
    next_snapshot = time.monotonic()
    movement_results = []
    events = []
    key_events = []
    audio_windows = []
    try:
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(output / 'config'))
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', '1', '-screen', '0', '800x600x24', '-nolisten', 'tcp'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        processes.append(xvfb)
        assert select.select([xvfb.stdout], [], [], 10)[0], 'display startup'
        env['DISPLAY'] = ':' + xvfb.stdout.readline().strip()
        if args.sound_checks:
            capture_lib = output / 'audio-capture.so'
            flags = subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True).split()
            subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',*flags,
                            str(Path(__file__).with_name('handy_audio_capture.c')),'-ldl','-o',str(capture_lib)],check=True)
            env['LD_PRELOAD'] = str(capture_lib)
            env['HANDY_AUDIO_CAPTURE'] = str(output / 'audio.bin')
        with (output / 'handy.log').open('w') as log:
            handy = subprocess.Popen([str(pc / 'handy_sdl'), str(rom), '-sound' if args.sound_checks else '-nosound', '-nofullscreen', '-noautoskip', '-frameskip', '0', '-throttle', '-comlynx', f'connect:127.0.0.1:{listener.getsockname()[1]}'], cwd=output, env=env, stdout=log, stderr=log)
        processes.append(handy)
        peer, _ = listener.accept()
        peer.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        peer.settimeout(10)
        from handy_command_bootstrap import bootstrap
        command = bootstrap(peer, env)
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
            nonlocal last_delta
            if simulate_moves and input_queue and time.monotonic() >= input_queue[0][1]:
                p, _ = input_queue.popleft()
                last_delta = p[1]
                before_cell = list(local)
                stick = p[3] & 15
                dx = 1 if stick == 7 else -1 if stick == 11 else 0
                dy = 1 if stick == 13 else -1 if stick == 14 else 0
                x, y = local[0] + dx, local[1] + dy
                if not (p[3] & 0x10) and 0 <= x < 20 and 0 <= y < 19:
                    cell = y*20+x
                    occupied = any([x,y] == pos and slot not in dead_slots
                                   for slot,pos in enumerate(remote_positions,1))
                    if not occupied and not (maze()[cell//8] & (1 << (cell%8))):
                        local[:] = [x,y]
                applied_moves.append(dict(seq=p[1], joy=p[3], before=before_cell, after=list(local)))
            sequence = (sequence + 1) & 255
            coords = list(local)
            for slot,pos in enumerate(remote_positions,1):
                coords.extend([255,255] if slot in dead_slots else pos)
            send([0x40, sequence, 0x81]+coords+
                 [7,11,7,11,1,2,3,4,last_delta,round_id])

        def service(seconds, snapshots=False):
            nonlocal last_delta, next_snapshot
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if snapshots and time.monotonic() >= next_snapshot:
                    snapshot()
                    next_snapshot = time.monotonic() + .1
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
                        if simulate_moves:
                            input_queue.append((p, time.monotonic() + args.input_lag_ms / 1000))
                        else:
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
                    if x in (0,19) or y in (0,18) or (x,y) == (10,10) or (
                            args.movement_checks and y == 16 and x != 6):
                        cell = y*20+x
                        bricks[cell//8] |= 1 << (cell%8)
            return list(bricks)

        def capture(name):
            path = output / (name+'.png')
            if d is None:
                subprocess.run(['import', '-display', env['DISPLAY'], '-window', 'root', str(path)], check=True, timeout=10)
            else:
                root_window = d.screen().root
                geometry = root_window.get_geometry()
                raw = root_window.get_image(0,0,geometry.width,geometry.height,X.ZPixmap,0xffffffff)
                Image.frombytes('RGB',(geometry.width,geometry.height),raw.data,'raw','BGRX').save(path)
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
        start_audio = time.monotonic()
        reliable(3,[0x51,0,10,10,1])
        service(1,True)
        if args.sound_checks: audio_windows.append(('brick',start_audio,time.monotonic()))
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
            key_events.append(dict(key=name, down=down, at=time.monotonic()))
            xtest.fake_input(d,X.KeyPress if down else X.KeyRelease,d.keysym_to_keycode(string_to_keysym(name)))
            d.sync()
        if args.death_checks:
            local[:] = [6,17]
            send([0x52,54,0,6,17,3,round_id])
            service(.4,True)
            floor = capture('death-before').getpixel((59,84))
            dead_slots.add(1)
            send([0x52,55,1,0,0,1,round_id])
            for _ in range(10):
                service(.04,True)
                burst = capture('death-burst')
                if burst.getpixel((59,84)) != floor:
                    break
            assert burst.getpixel((59,84)) != floor, 'explosion/smoke missing'
            key('Right',True)
            service(.1,True)
            key('Right',False)
            service(.2,True)
            passing = capture('death-walk-through')
            assert local == [7,17], f'dead cell blocked movement: {local}'
            assert passing.getpixel((59,84)) != floor, 'cloud expired before traversal'
            # Interpolation and facing change the occupied pixels. Look for
            # the green robe inside the dead player's cell, over the cloud.
            robe = {burst.getpixel((x,y)) for x in range(48,56) for y in range(85,90)
                    if burst.getpixel((x,y))[1] > max(burst.getpixel((x,y))[::2])}
            assert robe and any(passing.getpixel((x,y)) in robe
                                for x in range(56,64) for y in range(85,90)), 'living player hidden by cloud'
            service(.7,True)
            expired = capture('death-cleared')
            assert expired.getpixel((59,84)) == floor, 'smoke did not clear'
            send([0x52,55,1,0,0,1,round_id])
            service(.15,True)
            assert capture('death-echo').getpixel((59,84)) == floor, 'duplicate restarted smoke'
            remote_positions[0] = [10,17]
            dead_slots.remove(1)
            send([0x52,56,1,10,17,3,round_id])
            service(.3,True)
            spawned = capture('death-respawn')
            assert spawned.crop((80,85,88,90)).tobytes() != expired.crop((80,85,88,90)).tobytes(), 'respawned wizard missing'
            death_results = ['explosion/smoke visible', 'walked through active cloud',
                             'living player drawn over cloud', 'cloud cleanup',
                             'echo did not restart animation', 'final respawn visible']
            local[:] = [3,17]
            send([0x52,57,0,3,17,3,round_id])
            service(.4,True)
        if args.movement_checks:
            # A real queued-step model applies one DELTA per 10 Hz tick and
            # acknowledges only that applied sequence, with optional delay.
            for duration in (.06, .12, .16):
                origin = list(local)
                key('Right',True)
                service(duration,True)
                key('Right',False)
                service(1.3,True)
                assert local == [origin[0]+1,origin[1]], f'tap {duration}s: {origin} -> {local}'
                stopped = list(local)
                service(.4,True)
                assert local == stopped, 'continued walking after release'
                movement_results.append(dict(tap_ms=round(duration*1000), before=origin, after=list(local)))
            assert local == [6,17], f'intersection: {local}'
            key('Up',True)
            service(.1,True)
            key('Up',False)
            service(1.3,True)
            assert local == [6,16], f'column entrance: {local}'
            # Begin Up, then roll onto Right while Up remains physically held.
            key('Up',True)
            service(.1,True)
            key('Right',True)
            service(.1,True)
            key('Up',False)
            key('Right',False)
            service(1.5,True)
            assert local == [7,15], f'overlapping corner turn: {local}'
            movement_results.append(dict(turn='Up then Right while Up held', after=list(local)))
            capture('movement-corner')
        start_audio = time.monotonic()
        key('z',True)
        service(.4,True)
        key('z',False)
        service(.4,True)
        if args.sound_checks: audio_windows.append(('local-fire',start_audio,time.monotonic()))
        assert any(e['delta'][3] & 0x10 for e in events), 'A/fire input not transmitted'
        start_audio = time.monotonic()
        key('Right',True)
        service(.3,True)
        key('Right',False)
        service(.8,True)
        if args.sound_checks: audio_windows.append(('movement',start_audio,time.monotonic()))
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
        if args.sound_checks:
            # Allow the callback's 3 KB cushion and all envelopes to drain.
            service(1,True)
            start_audio = time.monotonic()
            service(.6,True)
            audio_windows.append(('idle',start_audio,time.monotonic()))
            for slot in (0,1):
                start_audio = time.monotonic()
                send([0x42,70+slot,slot,5+slot,17,1,round_id])
                service(.6,True)
                audio_windows.append((f'shot-{slot}',start_audio,time.monotonic()))
                start_audio = time.monotonic()
                send([0x42,72+slot,slot,5+slot,17,0,round_id])
                service(.6,True)
                audio_windows.append((f'impact-{slot}',start_audio,time.monotonic()))
            start_audio=time.monotonic()
            dead_slots.add(1)
            send([0x52,75,1,0,0,1,round_id])
            service(1,True)
            audio_windows.append(('death',start_audio,time.monotonic()))
            start_audio=time.monotonic()
            dead_slots.remove(1)
            send([0x52,76,1,7,17,3,round_id])
            service(.8,True)
            audio_windows.append(('respawn',start_audio,time.monotonic()))
        next_revision = 4
        if args.round_checks:
            names = b'LYNX    ATARI8  PLAYER3 PLAYER4 '
            match = [0x54,round_id,0,15,8,10,10,7,4,2,8]+list(names)
            send([0x53,0,4,0]+match)
            for _ in range(30):
                service(.04)
                smoke = capture('round-losers-smoke')
                if smoke.crop((54,83,66,90)).tobytes() != stable.crop((54,83,66,90)).tobytes():
                    break
            assert smoke.crop((54,83,66,90)).tobytes() != stable.crop((54,83,66,90)).tobytes(), 'loser smoke missing'
            service(.65)
            assert 4 in acked, 'MATCH_END not acknowledged during animation'
            poses = []
            for i in range(4):
                poses.append(capture(f'round-dance-{i}').crop((24,85,32,90)).tobytes())
                service(.14)
            assert len(set(poses)) >= 2, 'winner dance did not animate'
            danced = capture('round-dance')
            floor = danced.getpixel((8,85))
            assert all(danced.getpixel((x,y)) == floor for x in range(56,64) for y in range(85,90)), 'loser remained after smoke'
            # A new reliable revision carrying the same frozen result must
            # ACK without restarting either animation or changing the scores.
            reliable(5,match)
            service(5.5)
            scores = capture('round-scores')
            assert scores.getpixel((0,0)) == floor, 'results screen not displayed'
            assert any(scores.getpixel((x,y)) != floor for x in range(24,108) for y in range(45,49)), 'score row missing'
            key('x',True); service(.6); key('x',False); service(1)
            assert capture('round-scores-restored').tobytes() == scores.tobytes(), 'diagnostics lost frozen results'
            if args.sound_checks:
                start_audio=time.monotonic(); service(.6)
                audio_windows.append(('results-idle',start_audio,time.monotonic()))
            round_results = ['losers smoke', 'animated winner', 'losers disappear',
                             'ACK during animation', 'duplicate result does not restart',
                             'score screen', 'results restored after diagnostics']
            next_revision = 6
        round_id = 2
        reliable(next_revision,[0x55,2,10])
        reliable(next_revision+1,[0x50,0,1]+maze()+[2])
        service(2,True)
        restarted = capture('new-round')
        assert restarted.getpixel((80,50)) == before.getpixel((80,50)), 'new round did not restore brick'
        result = dict(endpoint=endpoint, port=endpoint_port, input_lag_ms=args.input_lag_ms,
                      movement_results=movement_results, death_results=death_results, round_results=round_results, applied_moves=applied_moves, rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                      handy_sha256=hashlib.sha256((pc/'handy_sdl').read_bytes()).hexdigest(),
                      received=dict(received), acknowledged=sorted(acked),
                      checks=['brick removal','bottom feet','border texture','fire input','movement input','diagnostics restoration','new round'],
                      method='Production ROM; controlled direct ComLynx peer, 10 Hz snapshots; no firmware/WAN')
        if args.sound_checks:
            data=(output/'audio.bin').read_bytes()
            freq,fmt,channels=struct.unpack_from('=III',data)
            assert (freq,fmt,channels)==(22050,8,1), 'unexpected capture format'
            records=[]; offset=12
            while offset+12<=len(data):
                ns,count=struct.unpack_from('=QI',data,offset); offset+=12
                if offset+count>len(data): break
                records.append((ns/1e9,data[offset:offset+count])); offset+=count
            checks=[]
            for name,begin,end in audio_windows:
                samples=b''.join(pcm for stamp,pcm in records if begin<=stamp<=end)
                peak=max((abs(v-128) for v in samples),default=0)
                assert samples, f'no audio callbacks during {name}'
                assert (peak==0 if name.endswith('idle') else peak>0), f'{name} audio peak {peak}'
                checks.append(dict(event=name,peak=peak,samples=len(samples)))
            with wave.open(str(output/'sound-preview.wav'),'wb') as wav:
                wav.setparams((1,1,freq,0,'NONE','not compressed'))
                wav.writeframes(b''.join(pcm for _,pcm in records))
            result['audio_checks']=checks
        (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(output=str(output), endpoint=endpoint, port=endpoint_port,
                              movement_results=movement_results, death_results=death_results, round_results=round_results, rom_sha256=result['rom_sha256'],
                              acknowledged=sorted(acked)),indent=2))
    finally:
        (output/'input-events.json').write_text(json.dumps(dict(received=events, applied=applied_moves, keys=key_events),indent=2)+'\n')
        print('Handy artifacts:', output)
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
