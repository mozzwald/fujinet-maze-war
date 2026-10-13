#!/usr/bin/env python3
"""Run the actual local server + installed Handy through a private raw bridge.

The bridge replaces only FujiNet command/NetStream forwarding, not game logic.
It does not model electrical ComLynx arbitration or contact the live service.
"""
from collections import Counter
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import tempfile
import time
from Xlib import X, display
from Xlib.XK import string_to_keysym
from Xlib.ext import xtest
from PIL import Image
from handy_command_bootstrap import bootstrap
from handy_render_smoke import diagnostic_values, wire

repo = Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--drop-history',action='store_true',help='drop one moving history frame on the upstream bridge')
args=parser.parse_args()
pc = Path.home() / 'lynx-pc'
output = Path(tempfile.mkdtemp(prefix='maze-handy-server-'))
rom = output/'cartridge.lnx'
shutil.copyfile(repo/'clients/lynx/build/maze-war-lynx.lnx', rom)
shutil.copyfile(pc/'lynxboot.img', output/'lynxboot.img')
processes, sockets = [], []
counts = {'up': Counter(), 'down': Counter()}
buffers = {'up': bytearray(), 'down': bytearray()}
observations = []
dropped_history=[]
try:
    probe = socket.socket(); probe.bind(('127.0.0.1',0))
    server_port = probe.getsockname()[1]; probe.close()
    with (output/'server.log').open('w') as log:
        server = subprocess.Popen([str(repo/'build/maze-war-server'),'--bind','127.0.0.1',
                                   '--port-base',str(server_port),'--room-count','1',
                                   '--room-zombies','2','--kill-limit','10','--debug'],
                                  cwd=repo,stdout=log,stderr=log)
    processes.append(server)
    listener=socket.socket(); sockets.append(listener)
    listener.bind(('127.0.0.1',0)); listener.listen(1); listener.settimeout(20)
    xvfb=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],
                          stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
    processes.append(xvfb)
    assert select.select([xvfb.stdout],[],[],10)[0]
    env=dict(os.environ,DISPLAY=':'+xvfb.stdout.readline().strip(),
             SDL_AUDIODRIVER='dummy',XDG_CONFIG_HOME=str(output/'config'))
    with (output/'handy.log').open('w') as log:
        handy=subprocess.Popen([str(pc/'handy_sdl'),str(rom),'-nosound','-nofullscreen',
                                '-noautoskip','-frameskip','0','-throttle','-comlynx',
                                f'connect:127.0.0.1:{listener.getsockname()[1]}'],
                               cwd=output,env=env,stdout=log,stderr=log)
    processes.append(handy)
    peer,_=listener.accept(); sockets.append(peer); peer.settimeout(10)
    command=bootstrap(peer,env)
    assert command[3]==0xf0 and int.from_bytes(command[4:6],'little')==5025
    assert command[6:command.index(0,6)].decode()=='fujinet.online'
    backend=socket.create_connection(('127.0.0.1',server_port)); sockets.append(backend)
    for sock in (backend,peer):
        sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        sock.setblocking(False)
    time.sleep(.02); peer.sendall(b'\x06\x06')

    def service(seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            assert handy.poll() is None and server.poll() is None
            ready,_,_=select.select([peer,backend],[],[],.01)
            for source in ready:
                data=source.recv(8192); assert data, 'bridge disconnected'
                direction='up' if source is peer else 'down'
                buffer=buffers[direction]; buffer.extend(data)
                while 0 in buffer:
                    index=buffer.index(0); encoded=bytes(buffer[:index]); del buffer[:index+1]
                    if not encoded: continue
                    payload=wire.decode(encoded); counts[direction][hex(payload[0])]+=1
                    drop=(args.drop_history and not dropped_history and direction=='up' and
                          payload[0]==0x58 and any((joy&15)!=15 and not joy&16 for joy in payload[4::2]))
                    if drop: dropped_history.extend(payload)
                    else: (backend if source is peer else peer).sendall(encoded+b'\0')
                    if payload[0]==0x53:
                        counts[direction]['reliable_'+hex(payload[4])]+=1
                    observations.append({'direction':direction,'at':time.monotonic(),'payload':list(payload),'dropped':drop})
    service(3)
    d=display.Display(env['DISPLAY'])
    def windows(window):
        yield window
        for child in window.query_tree().children: yield from windows(child)
    window=next(w for w in windows(d.screen().root) if w.get_wm_name()=='Handy/SDL')
    window.set_input_focus(X.RevertToParent,X.CurrentTime); d.sync()
    def key(name,down):
        xtest.fake_input(d,X.KeyPress if down else X.KeyRelease,
                         d.keysym_to_keycode(string_to_keysym(name))); d.sync()
    def capture(name):
        raw=window.get_image(0,0,480,306,X.ZPixmap,0xffffffff)
        image=Image.frombytes('RGB',(480,306),raw.data,'raw','BGRX')
        image.save(output/(name+'.png'))
        return image.resize((160,102),Image.Resampling.NEAREST)
    capture('joined')
    # Move, stop, turn, and directional fire against real two-zombie logic.
    for direction in ('Right','Up','Left','Down'):
        key(direction,True); service(.5); key(direction,False); service(.3)
        key('z',True); key(direction,True); service(.6)
        key(direction,False); key('z',False); service(.3)
    service(2); capture('playing')
    key('x',True); service(.8); diagnostics=diagnostic_values(capture('diagnostics'))
    assert diagnostics['input_history']==1, 'ROM did not enable negotiated history'
    key('x',False); service(.5); capture('restored')
    assert counts['up']['0x46'] and counts['down']['0x47']
    assert counts['up']['0x58'] >= 8 and counts['down']['0x40'] >= 50
    assert counts['up']['0x59'] and counts['down']['0x59'], 'history negotiation missing'
    assert counts['up']['0x45'] and counts['down']['0x42']
    assert any(o['direction']=='up' and o['payload'][0]==0x58 and
               any(joy&16 and joy&15!=15 for joy in o['payload'][4::2]) for o in observations)
    assert any(o['direction']=='down' and o['payload'][0]==0x40 and
               o['payload'][2]&128 for o in observations), 'no applied-input ACK'
    assert counts['down']['0x43'] > counts['down']['reliable_0x43']
    if args.drop_history:
        assert dropped_history, 'no moving history frame dropped'
        lost=list(zip(dropped_history[3::2],dropped_history[4::2]))
        assert any(o['direction']=='up' and not o['dropped'] and o['payload'][0]==0x58 and
                   all(pair in list(zip(o['payload'][3::2],o['payload'][4::2])) for pair in lost)
                   for o in observations), 'next history did not repair the lost prefix'
    result={'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),
            'server_sha256':hashlib.sha256((repo/'build/maze-war-server').read_bytes()).hexdigest(),
            'counts':counts,'diagnostics':diagnostics,'dropped_history':dropped_history,
            'checks':['actual server two-zombie session','handshake/name/map/reliable ACKs',
                      'movement/stop/turn/directional fire','applied input ACKs',
                      'periodic names direct only','diagnostics/maze return','negotiated input history',
                      *(['dropped moving frame repaired in later history'] if args.drop_history else [])]}
    (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    (output/'frames.json').write_text(json.dumps(observations,indent=2)+'\n')
    print(json.dumps(result,indent=2)); d.close()
finally:
    for sock in sockets: sock.close()
    for process in reversed(processes):
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
    print('Handy/server artifacts:',output)
