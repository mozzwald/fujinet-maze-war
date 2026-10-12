#!/usr/bin/env python3
"""Name persistence and HTTP->NetStream handoff in private Handy sessions.

Default: controlled command peer, production ROM, no external game connection.
--firmware: existing FujiNet PC binary, private SD, local HTTP and game fixtures;
uses a separate localhost ROM under /tmp and preserves the live hardware ROM.
"""
import argparse
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
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('wire',Path(__file__).resolve().parents[1]/'bench/run.py')
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


def record(host,port,label):
    r=bytearray(189); r[0]=3
    for start,size,s in [(1,17,'Maze War'),(18,33,label),(51,65,f'tcp://{host}:{port}'),(116,65,'tnfs://example/atari.xex'),(181,3,'us')]:
        b=s.encode(); assert len(b)<size; r[start:start+len(b)]=b
    r[184:187]=bytes([1,1,4])
    return r


def main():
    from Xlib import X,display
    from Xlib.XK import string_to_keysym
    from Xlib.ext import xtest
    from PIL import Image
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',action='store_true')
    p.add_argument('--new-name',action='store_true')
    p.add_argument('--fail-save',action='store_true')
    p.add_argument('--http404',action='store_true')
    p.add_argument('--sd-from',type=Path)
    p.add_argument('--edit-checks',action='store_true')
    p.add_argument('--live-browser',action='store_true')
    args=p.parse_args()
    root=Path(tempfile.mkdtemp(prefix='maze-lynx-startup-'))
    pc=Path.home()/'lynx-pc'; repo=Path(__file__).resolve().parents[3]
    rom=repo/'clients/lynx/build/maze-war-lynx.lnx'
    shutil.copy(pc/'lynxboot.img',root/'lynxboot.img'); (root/'config').mkdir()
    stop=threading.Event(); errors=[]; commands=[]; frames=[]; urls=[]
    state={'key':b'' if args.new_name else b'ACE LYNX','mode':0,'offset':0,'raw':False}
    peers=[]; processes=[]; threads=[]; d=None
    listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(2); listener.settimeout(1)
    game_port=listener.getsockname()[1]
    host='127.0.0.1' if args.firmware else 'fujinet.online'
    port=game_port if args.firmware else 5025
    body=bytes([4,0,0])+b''.join(record(host,port+i,'Maze War Humans Only' if not i else f'Maze War Room {i+1}') for i in range(4))

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            urls.append(self.path)
            self.send_response(404 if args.http404 else 200)
            data=b'{"message":"no rooms"}' if args.http404 else body
            self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
        def log_message(self,*a): pass
    http=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=http.serve_forever,daemon=True); thread.start(); threads.append(thread)

    def recv_exact(s,n):
        out=bytearray(); deadline=time.monotonic()+20
        while len(out)<n:
            if stop.is_set() or time.monotonic()>deadline: raise TimeoutError('receive')
            try: b=s.recv(n-len(out))
            except socket.timeout: continue
            if not b: raise EOFError('peer closed')
            out.extend(b)
        return bytes(out)

    def response(s,b):
        ck=0
        for c in b: ck^=c
        s.sendall(len(b).to_bytes(2,'big')+b+bytes([ck]))
        assert recv_exact(s,1)==b'\x06','response ACK'

    def command_peer(s):
        while not stop.is_set():
            h=recv_exact(s,3); n=int.from_bytes(h[1:],'big'); b=recv_exact(s,n); ck=recv_exact(s,1)[0]
            assert wire_xor(b)==ck
            commands.append({'device':h[0],'payload':list(b)})
            s.sendall(b'\x06'); c=b[0]
            if h[0]==0x70:
                if c==0xdc:
                    assert b[1:5] in (b'\x22\x30\x03\x00',b'\x01\x00\x01\x00')
                    state['mode']=b[5]; s.sendall(b'\x06')
                elif c==0xdd: response(s,state['key'])
                elif c==0xdb: s.sendall(b'\x06')
                elif c==0xde:
                    assert state['mode']==1
                    if not args.fail_save: state['key']=b[1:]
                    s.sendall(b'\x15' if args.fail_save else b'\x06')
                elif c==0xf0:
                    assert int.from_bytes(b[1:3],'little')==port
                    assert b[3:-2].decode()==host and b[-2:]==b'\0\x01'
                    state['raw']=True; s.sendall(b'\x06'); return
                else: raise AssertionError(f'Fuji command {c:x}')
            else:
                assert h[0]==0x71
                if c==ord('O'):
                    assert b[1:3]==b'\x04\0'; urls.append(b[3:].rstrip(b'\0').decode()); state['offset']=0; s.sendall(b'\x06')
                elif c==ord('S'):
                    remaining=len(body)-state['offset']
                    response(s,remaining.to_bytes(2,'little')+bytes([int(bool(remaining)),170 if args.http404 else 1 if remaining else 136]))
                elif c==ord('R'):
                    data=body[state['offset']:state['offset']+256]; state['offset']+=len(data); response(s,data)
                elif c==ord('C'): s.sendall(b'\x06')
                else: raise AssertionError('network command')

    def game_peer(s):
        buf=bytearray()
        while not stop.is_set():
            try: b=s.recv(512)
            except socket.timeout: continue
            if not b: return
            buf.extend(b)
            while 0 in buf:
                split=buf.index(0); chunk=bytes(buf[:split]); del buf[:split+1]
                if not chunk: continue
                msg=wire.decode(chunk)
                frames.append(list(msg))
                if msg==bytes([0x46,1]): s.sendall(wire.encode(bytes([0x47,1,1,0,10])))

    def serve():
        try:
            while not stop.is_set():
                try: s,_=listener.accept()
                except socket.timeout: continue
                peers.append(s); s.settimeout(.5); s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                if not args.firmware: command_peer(s)
                game_peer(s)
        except Exception as e:
            if not stop.is_set(): errors.append(repr(e))

    def wait_for(predicate,seconds=15):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            assert not errors,errors
            if predicate(): return
            time.sleep(.05)
        raise AssertionError('checkpoint timeout')

    def capture(name):
        win=d.screen().root; g=win.get_geometry()
        raw=win.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
        Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX').save(root/(name+'.png'))

    def key(name):
        code=d.keysym_to_keycode(string_to_keysym(name))
        xtest.fake_input(d,X.KeyPress,code); d.sync(); time.sleep(.12)
        xtest.fake_input(d,X.KeyRelease,code); d.sync(); time.sleep(.15)

    try:
        if args.firmware:
            if args.sd_from:
                shutil.copytree(args.sd_from,root/'SD')
                state['key']=(root/'SD/FujiNet/30220300.key').read_bytes()
            else: (root/'SD/FujiNet').mkdir(parents=True)
            if not args.new_name and not args.sd_from: (root/'SD/FujiNet/30220300.key').write_bytes(state['key'])
            shutil.copytree(pc/'fujinet/data',root/'data')
            (root/'fnconfig.ini').write_text('[General]\nconfigenabled=0\n[BOIP]\nenabled=1\nhost=127.0.0.1\nport=0\n')
            temp=socket.socket(); temp.bind(('127.0.0.1',0)); boip=temp.getsockname()[1]; temp.close()
            cfg=(root/'fnconfig.ini').read_text().replace('port=0',f'port={boip}')
            (root/'fnconfig.ini').write_text(cfg)
            if not args.live_browser:
                rom=root/'startup-local.lnx'
                subprocess.run(['make','-C',str(repo/'clients/lynx'),'all',f'TARGET={rom}',f'MAP={root}/startup-local.map',
                                'CL65=/home/mozzwald/build/cc65/bin/cl65','CC65_ROOT=/home/mozzwald/build/cc65',
                                'SERVER_HOST=127.0.0.1',f'SERVER_PORT={game_port}',f'ROOM_PORT_BASE={game_port}',
                                f'LOBBY_BASE=http://127.0.0.1:{http.server_port}'],check=True,stdout=subprocess.DEVNULL)
            log=(root/'firmware.log').open('w')
            processes.append(subprocess.Popen([str(pc/'fujinet/fujinet'),'-c',str(root/'fnconfig.ini'),'-s',str(root/'SD')],cwd=root,stdout=log,stderr=log)); log.close()
            time.sleep(2)
        else: boip=game_port
        t=threading.Thread(target=serve,daemon=True); t.start(); threads.append(t)
        env=dict(os.environ,SDL_AUDIODRIVER='dummy',XDG_CONFIG_HOME=str(root/'config'))
        xvfb=subprocess.Popen(['Xvfb','-displayfd','1','-screen','0','800x600x24','-nolisten','tcp'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
        processes.append(xvfb); assert select.select([xvfb.stdout],[],[],10)[0]
        env['DISPLAY']=':'+xvfb.stdout.readline().strip()
        log=(root/'handy.log').open('w')
        processes.append(subprocess.Popen([str(pc/'handy_sdl'),str(rom),'-nosound','-nofullscreen','-noautoskip','-frameskip','0','-throttle','-comlynxtrace','-comlynx',f'connect:127.0.0.1:{boip}'],cwd=root,env=env,stdout=log,stderr=log)); log.close()
        time.sleep(5); d=display.Display(env['DISPLAY'])
        def windows(w):
            yield w
            for child in w.query_tree().children: yield from windows(child)
        win=next(w for w in windows(d.screen().root) if w.get_wm_name()=='Handy/SDL')
        win.set_input_focus(X.RevertToParent,X.CurrentTime); d.sync()
        capture('initial')
        if args.live_browser:
            assert args.firmware and not args.new_name
            def fetched():
                log=(root/'firmware.log').read_text(errors='replace')
                return 'resultCode=200' in log and 'NetworkProtocolHTTP::umount()' in log
            wait_for(fetched,30); time.sleep(4); capture('live-lobby')
            log=(root/'firmware.log').read_text(errors='replace')
            assert 'https://lobby.fujinet.online/view?' in log
            assert 'ENABLE NETSTREAM' not in log and not frames
            result={'firmware':True,'live_browser':True,
                    'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),
                    'firmware_sha256':hashlib.sha256((pc/'fujinet/fujinet').read_bytes()).hexdigest(),
                    'checks':['saved name loaded','real production HTTPS HTTP 200','HTTP channel closed','no NetStream before selection']}
            (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
            print(json.dumps(result,indent=2)); return
        if args.new_name:
            assert not urls,'Lobby shown before name entry'
            key('Up'); key('z'); key('z') # choose A, append, Done
            expected=b'A       '
        else: expected=state['key'].ljust(8,b' ')
        wait_for(lambda: bool(urls)); time.sleep(2); capture('lobby')
        assert not frames,'raw traffic before selection'
        if args.edit_checks:
            assert expected==b'ACE LYNX'
            key('F2'); key('Up'); key('z') # cannot append a ninth character
            capture('name-at-limit')
            key('F2') # cancel retains original
            key('F2'); key('x'); key('z') # delete X, Done
            expected=b'ACE LYN '
            capture('name-edited')
        key('x' if args.http404 else 'z')
        wait_for(lambda: any(p[0]==0x43 for p in frames),20)
        names=[bytes(p[3:]) for p in frames if p[0]==0x43]
        assert expected in names,(expected,names)
        if args.new_name and not args.fail_save:
            persisted=(root/'SD/FujiNet/30220300.key').read_bytes() if args.firmware else state['key']
            assert persisted==b'A',persisted
        assert not errors,errors
        if args.edit_checks:
            persisted=(root/'SD/FujiNet/30220300.key').read_bytes() if args.firmware else state['key']
            assert persisted==b'ACE LYN',persisted
        capture('joined')
        result={'firmware':args.firmware,'new_name':args.new_name,'failed_save':args.fail_save,'http404':args.http404,'edit_checks':args.edit_checks,'reload':bool(args.sd_from),
                'name':expected.decode(),'commands':commands,'http_requests':urls,
                'rom_sha256':hashlib.sha256(rom.read_bytes()).hexdigest(),
                'handy_sha256':hashlib.sha256((pc/'handy_sdl').read_bytes()).hexdigest(),
                'checks':['name resolved before HTTP','Lobby waits for selection','selected endpoint','HELLO/WELCOME','NAME survives WELCOME']}
        if args.firmware: result['firmware_sha256']=hashlib.sha256((pc/'fujinet/fujinet').read_bytes()).hexdigest()
        (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k!='commands'},indent=2))
    finally:
        stop.set()
        if d: d.close()
        for s in peers: s.close()
        listener.close(); http.shutdown(); http.server_close()
        for process in reversed(processes):
            process.terminate()
            try: process.wait(timeout=3)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        print('Handy startup artifacts:',root)


def wire_xor(b):
    ck=0
    for c in b: ck^=c
    return ck

if __name__=='__main__': main()
