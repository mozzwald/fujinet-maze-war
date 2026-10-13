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
    parser.add_argument("--remote-jitter-checks", action="store_true", help="sample a remote sprite with irregularly spaced snapshot destinations")
    parser.add_argument("--death-checks", action="store_true")
    parser.add_argument("--round-checks", action="store_true")
    parser.add_argument("--sound-checks", action="store_true", help="capture Handy SDL audio using a test-only preload")
    parser.add_argument("--input-lag-ms", type=float, default=0)
    parser.add_argument("--snapshot-burst", type=int, default=1, help="deliver this many 10 Hz snapshots together to simulate receive jitter")
    parser.add_argument("--latency-checks", action="store_true", help="measure held movement and visible pixel cadence in an empty corridor")
    parser.add_argument("--hold-ms", type=float, default=1200, help="corridor hold duration for latency checks")
    parser.add_argument("--recovery-checks", action="store_true", help="drop held movement until its window fills and check retry recovery without another key press")
    parser.add_argument("--expect-stall", action="store_true", help="record an older ROM's missing recovery; requires --recovery-checks")
    args = parser.parse_args()
    if args.hold_ms <= 0:
        parser.error('--hold-ms must be positive')
    if not 1 <= args.snapshot_burst <= 4:
        parser.error('--snapshot-burst must be 1..4')
    if sum((args.movement_checks, args.remote_jitter_checks, args.death_checks, args.round_checks, args.latency_checks, args.recovery_checks)) > 1:
        parser.error('run movement, death, round, latency and recovery checks separately')
    if args.expect_stall and not args.recovery_checks:
        parser.error('--expect-stall requires --recovery-checks')
    rom = args.rom.resolve()
    pc = Path.home() / 'lynx-pc'
    output = Path(tempfile.mkdtemp(prefix='maze-suzy-game-'))
    # Measure an immutable cartridge even if another terminal rebuilds it.
    shutil.copyfile(rom, output / 'cartridge.lnx')
    rom = output / 'cartridge.lnx'
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
    dead_slots = {1,2,3} if args.latency_checks or args.recovery_checks else set()
    if args.remote_jitter_checks:
        dead_slots = {2,3}
    remote_positions = [[7,17],[11,17],[15,17]]
    simulate_moves = args.movement_checks or args.death_checks or args.latency_checks or args.recovery_checks
    latency_results = {}
    recovery_results = {}
    drop_moves = 0
    dropped_inputs = []
    last_admitted = None
    duplicate_inputs = 0
    death_results = []
    round_results = []
    applied_moves = []
    next_snapshot = time.monotonic()
    movement_results = []
    remote_results = {}
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

        snapshot_batch = bytearray()
        snapshot_batch_count = 0
        def send(payload):
            nonlocal snapshot_batch_count
            encoded = wire.encode(bytes(payload))
            if payload[0] == 0x40 and args.snapshot_burst > 1:
                snapshot_batch.extend(encoded)
                snapshot_batch_count += 1
                if snapshot_batch_count < args.snapshot_burst:
                    return
                peer.sendall(snapshot_batch)
                snapshot_batch.clear()
                snapshot_batch_count = 0
            else:
                peer.sendall(encoded)

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
                applied_moves.append(dict(seq=p[1], joy=p[3], before=before_cell, after=list(local), at=time.monotonic()))
            sequence = (sequence + 1) & 255
            coords = list(local)
            for slot,pos in enumerate(remote_positions,1):
                coords.extend([255,255] if slot in dead_slots else pos)
            send([0x40, sequence, 0x81]+coords+
                 [7,11,7,11,1,2,3,4,last_delta,round_id])

        def service(seconds, snapshots=False):
            nonlocal last_delta, next_snapshot, last_admitted, drop_moves, duplicate_inputs
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
                            drop = False
                            if drop_moves and p[3] == 7:
                                if any(item[1] == p[1] for item in dropped_inputs):
                                    drop_moves = 0 # Let the first retry recover the link.
                                else:
                                    dropped_inputs.append(list(p))
                                    drop = True
                            if drop:
                                pass
                            elif last_admitted is not None and not 0 < ((p[1]-last_admitted) & 255) < 128:
                                duplicate_inputs += 1
                            elif len(input_queue) < 6:
                                input_queue.append((p, time.monotonic() + args.input_lag_ms / 1000))
                                last_admitted = p[1]
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
        if args.remote_jitter_checks:
            remote_positions[0] = [4,12]
            send([0x52,58,1,4,12,3,round_id])
            service(.6,True)
            image = capture('remote-before')
            robe = image.getpixel((35,61))
            assert robe != image.getpixel((32,60)), 'remote robe sample was floor'
            schedule = [(0,5),(.075,6),(.205,7),(.305,8),(.51,9),(.61,10)]
            samples, destinations = [], []
            started = time.monotonic()
            while time.monotonic()-started < 1.3:
                elapsed = time.monotonic()-started
                while schedule and elapsed >= schedule[0][0]:
                    _, cell = schedule.pop(0)
                    remote_positions[0][0] = cell
                    destinations.append(dict(ms=round(elapsed*1000,1),cell=cell))
                    snapshot()
                service(.01,True)
                image = capture('remote-jitter')
                xs = [x for y in range(60,65) for x in range(8,152) if image.getpixel((x,y)) == robe]
                samples.append(dict(ms=round((time.monotonic()-started)*1000,1),left=min(xs) if xs else None))
            service(.4,True)
            image = capture('remote-settled')
            xs = [x for y in range(60,65) for x in range(8,152) if image.getpixel((x,y)) == robe]
            visible = [s['left'] for s in samples if s['left'] is not None]
            assert xs and min(xs)==81, 'remote did not settle at final destination'
            assert len(set(visible))>=4, 'remote intermediate positions missing'
            assert all(a<=b for a,b in zip(visible,visible[1:])), 'remote straight motion reversed'
            remote_results = dict(destinations=destinations,samples=samples,distinct_positions=len(set(visible)),
                                  largest_sample_gap_pixels=max((b-a for a,b in zip(visible,visible[1:])),default=0),stopped_left=min(xs))
        if args.recovery_checks:
            local[:] = [1,17]
            input_queue.clear()
            send([0x52,53,0,1,17,3,round_id])
            service(.8,True)
            drop_moves = 1
            first_event, first_applied = len(events), len(applied_moves)
            key('Right',True)
            service(3.4,True)
            steps_while_held = local[0]-1
            key('Right',False)
            service(1.5,True)
            stopped = list(local); service(.4,True)
            drop_moves = 0
            assert local == stopped, 'recovery moved again after release drain'
            assert 2 <= len(dropped_inputs) <= 4, 'did not fill bounded movement window'
            tail = dropped_inputs[-1][1]
            tail_packets = [e for e in events[first_event:] if e['delta'][1]==tail]
            tail_applied = [e for e in applied_moves[first_applied:] if e['seq']==tail]
            recovery_results = dict(dropped_inputs=list(dropped_inputs),steps_while_held=steps_while_held,
                                    tail_transmissions=len(tail_packets),tail_applications=len(tail_applied),
                                    duplicates_rejected=duplicate_inputs,stopped_at=stopped,
                                    expected_stall=args.expect_stall)
            if args.expect_stall:
                assert steps_while_held==0 and not tail_applied, 'older ROM unexpectedly recovered'
            else:
                assert steps_while_held>=3, 'held movement did not recover without a new press'
                assert len(tail_packets)>=2 and len(tail_applied)==1, 'retry lost or applied twice'
            capture('recovery-stopped')
        if args.latency_checks:
            local[:] = [1,17]
            input_queue.clear()
            send([0x52,53,0,1,17,3,round_id])
            service(.8,True)
            initial = capture('latency-before')
            robe = initial.getpixel((10,85))
            samples = []
            first_event = len(events)
            held_at = time.monotonic()
            key('Right',True)
            while time.monotonic()-held_at < args.hold_ms / 1000:
                service(.02,True)
                image = capture('latency-walking')
                xs = [x for y in range(85,90) for x in range(8,152) if image.getpixel((x,y)) == robe]
                samples.append(dict(ms=round((time.monotonic()-held_at)*1000,1),left=min(xs) if xs else None, server_cell=local[0]))
            key('Right',False)
            released_at = time.monotonic()
            server_at_release = local[0]
            release_samples = []
            while time.monotonic()-released_at < 1.5:
                service(.02,True)
                image = capture('latency-releasing')
                xs = [x for y in range(85,90) for x in range(8,152) if image.getpixel((x,y)) == robe]
                release_samples.append(dict(ms=round((time.monotonic()-released_at)*1000,1),left=min(xs) if xs else None, server_cell=local[0]))
            stopped=list(local); service(.4,True)
            assert local==stopped,'continued moving after release drain'
            moves=[e for e in events[first_event:] if e['at']<=released_at and e['delta'][3]==7]
            gaps=[round((b['at']-a['at'])*1000,1) for a,b in zip(moves[1:],moves[2:])]
            visible=[s['left'] for s in samples if s['left'] is not None]
            release_visible=[s for s in release_samples if s['left'] is not None]
            final_left=release_visible[-1]['left']
            last_moving=next((s['ms'] for s in reversed(release_visible) if s['left']!=final_left),0)
            first_neutral=next((e for e in events[first_event:] if e['at']>=released_at and e['delta'][3]==15),None)
            latency_results=dict(input_delay_ms=args.input_lag_ms,held_ms=round((released_at-held_at)*1000),
                                 sent_steps=len(moves),applied_steps=local[0]-1,
                                 largest_repeat_gap_ms=max(gaps,default=0),
                                 distinct_visual_positions=len(set(visible)),samples=samples,
                                 server_at_release=server_at_release, server_after_stop=local[0],
                                 visual_at_release_left=visible[-1], visual_stopped_left=final_left,
                                 visual_travel_after_release_cells=round((final_left-visible[-1])/8,3),
                                 last_unsettled_sample_ms=last_moving,
                                 release_samples=release_samples,
                                 post_release_moves=[e for e in events[first_event:] if e['at']>released_at and e['delta'][3]==7],
                                 neutral_delay_ms=round((first_neutral['at']-released_at)*1000,1) if first_neutral else None)
            # A byte already in transit at key-up can arrive just afterward.
            all_moves=[e for e in events[first_event:] if e['delta'][3]==7]
            assert len({e['delta'][1] for e in all_moves})==latency_results['applied_steps'],'prediction/server step count differs'
            assert first_neutral is not None,'release never sent a neutral input'
            assert not any(e['delta'][3]==7 and e['at']>first_neutral['at'] for e in events[first_event:]),'movement transmitted after neutral'
            capture('latency-stopped')
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
        if args.sound_checks: audio_windows.append(('bare-fire-idle',start_audio,time.monotonic()))
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
        result = dict(endpoint=endpoint, port=endpoint_port, input_lag_ms=args.input_lag_ms, snapshot_burst=args.snapshot_burst, remote_results=remote_results,
                      movement_results=movement_results, death_results=death_results, round_results=round_results, latency_results=latency_results, recovery_results=recovery_results, applied_moves=applied_moves, rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
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
                              remote_results={k:v for k,v in remote_results.items() if k != 'samples'}, snapshot_burst=args.snapshot_burst,
                              movement_results=movement_results, death_results=death_results, round_results=round_results,
                              latency_results={k:v for k,v in latency_results.items() if k not in ('samples','release_samples')}, rom_sha256=result['rom_sha256'],
                              recovery_results=recovery_results,
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
