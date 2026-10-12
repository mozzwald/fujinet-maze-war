"""Command-mode prelude for the existing gameplay smoke harness."""
import time
from handy_startup_smoke import record, wire_xor


def bootstrap(peer, env):
    from Xlib import X, display
    from Xlib.XK import string_to_keysym
    from Xlib.ext import xtest
    body=bytes([4,0,0])+b''.join(record('fujinet.online',5025+i,f'Maze War Room {i+1}') for i in range(4))
    offset=0
    def read(n):
        out=bytearray()
        while len(out)<n:
            b=peer.recv(n-len(out)); assert b,'command-mode disconnect'; out.extend(b)
        return bytes(out)
    def response(data):
        peer.sendall(len(data).to_bytes(2,'big')+data+bytes([wire_xor(data)]))
        assert read(1)==b'\x06'
    while True:
        h=read(3); b=read(int.from_bytes(h[1:],'big')); assert read(1)==bytes([wire_xor(b)])
        if b[0]==0xf0: return bytearray(h+b+bytes([wire_xor(b)]))
        peer.sendall(b'\x06')
        c=b[0]
        if c==0xdd: response(b'LYNX')
        elif c in (0xdc,0xdb): peer.sendall(b'\x06')
        elif c==ord('O'): offset=0; peer.sendall(b'\x06')
        elif c==ord('S'):
            n=len(body)-offset
            response(n.to_bytes(2,'little')+bytes([int(bool(n)),1 if n else 136]))
        elif c==ord('R'):
            data=body[offset:offset+256]; offset+=len(data); response(data)
        elif c==ord('C'):
            peer.sendall(b'\x06'); time.sleep(1)
            d=display.Display(env['DISPLAY'])
            def windows(w):
                yield w
                for child in w.query_tree().children: yield from windows(child)
            win=next(w for w in windows(d.screen().root) if w.get_wm_name()=='Handy/SDL')
            win.set_input_focus(X.RevertToParent,X.CurrentTime)
            code=d.keysym_to_keycode(string_to_keysym('z'))
            xtest.fake_input(d,X.KeyPress,code); d.sync(); time.sleep(.15)
            xtest.fake_input(d,X.KeyRelease,code); d.sync(); d.close()
        else: raise AssertionError(f'unexpected command {c:x}')
