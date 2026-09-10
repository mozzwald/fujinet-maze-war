"""Live transport checks; gameplay assertions remain in the existing suite."""
import socket
import struct
import subprocess
import sys
import tempfile
import time
from tcp_frames import crc16_ccitt_false, encode_frame, recv_frame, send_frame

root = sys.argv[1]

def decode(frame):
    src = frame[:-1]
    out = bytearray()
    i = 0
    while i < len(src):
        code = src[i]
        i += 1
        assert code and i + code - 1 <= len(src)
        out.extend(src[i:i + code - 1])
        i += code - 1
        if code != 255 and i < len(src): out.append(0)
    assert len(out) >= 3
    crc = crc16_ccitt_false(out[:-2])
    assert out[-2:] == bytes((crc & 0xff, crc >> 8))
    return bytes(out[:-2])

with socket.socket() as reservation:
    reservation.bind(('127.0.0.1', 0))
    port = reservation.getsockname()[1]
args = [root + '/build/maze-war-server', '--bind', '127.0.0.1', '--port', str(port), '--zombies', '3', '--debug']
peers = []
log = tempfile.TemporaryFile(mode='w+')
server = subprocess.Popen(args, cwd=root, stdout=log, stderr=log)

def connect():
    s = socket.create_connection(('127.0.0.1', port), timeout=1)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(.5)
    peers.append(s)
    return s

def wait_packet(s, predicate, timeout=2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try: p = decode(recv_frame(s))
        except socket.timeout: continue
        if predicate(p): return p
    raise AssertionError('expected packet not received')

def snapshot(s, pid, ack=None):
    return wait_packet(s, lambda p: p[0] == 0x40 and (p[2] >> 1) & 3 == pid and
                       (ack is None or (p[2] & 0x80 and p[19] == ack)))

try:
    # Wait for the listener without creating and consuming a probe seat.
    deadline = time.monotonic() + 3
    while True:
        log.flush(); log.seek(0)
        if 'listening on TCP' in log.read(): break
        assert server.poll() is None and time.monotonic() < deadline
        time.sleep(.02)
    a = connect()
    assert wait_packet(a, lambda p: p[0] == 0x50)
    for byte in encode_frame(bytes([0x41, 1, 0, 0x0f])):
        a.sendall(bytes([byte]))
        time.sleep(.01)
    snapshot(a, 0, 1)
    b = connect()
    send_frame(b, bytes([0x41, 1, 1, 0x0f]))
    snapshot(b, 1, 1)
    a.sendall(encode_frame(bytes([0x41, 2, 0, 0x0f])) +
              encode_frame(bytes([0x41, 3, 0, 0x0f])))
    snapshot(a, 0, 3)
    # Isolated writes must be applied on a prompt game tick (NODELAY path).
    start = time.monotonic()
    send_frame(a, bytes([0x41, 4, 0, 0x0f]))
    snapshot(a, 0, 4)
    assert time.monotonic() - start < .5
    duplicate = subprocess.run(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=2)
    assert duplicate.returncode != 0 and b'bind' in duplicate.stdout
    # Drain B then FIN it, proving immediate seat release and zombie backfill.
    b.shutdown(socket.SHUT_WR)
    while b.recv(1024): pass
    b.close()
    wait_packet(a, lambda p: p[0] == 0x44 and p[2] == 1)
    p = snapshot(a, 0)
    assert (p[2] >> 3) & 15 == 14
    b = connect(); send_frame(b, bytes([0x41, 1, 1, 0x0f])); snapshot(b, 1, 1)
    # RST must neither kill the server with SIGPIPE nor strand the seat.
    b.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
    b.close()
    wait_packet(a, lambda p: p[0] == 0x44 and p[2] == 1)
    # Fill the remaining seats; a fifth peer is rejected.
    for pid in range(1, 4):
        c = connect(); send_frame(c, bytes([0x41, 1, pid, 0x0f])); snapshot(c, pid, 1)
    extra = connect()
    assert extra.recv(1) == b''
    extra.close()
    for c in peers: c.close()
    peers.clear()
    time.sleep(.1)
    # A peer that never completes even one packet has a short handshake limit.
    idle = connect(); idle.sendall(b'\x41')
    start = time.monotonic()
    while time.monotonic() - start < 4.5:
        try:
            if idle.recv(1024) == b'': break
        except socket.timeout: pass
    else: raise AssertionError('incomplete handshake was not reaped')
    server.terminate(); server.wait(timeout=2)
    # SO_REUSEADDR permits immediate restart on the same TCP port.
    server = subprocess.Popen(args, cwd=root, stdout=log, stderr=log)
    time.sleep(.1)
    assert server.poll() is None
    c = connect(); send_frame(c, bytes([0x41, 1, 0, 0x0f])); snapshot(c, 0, 1)
    print('TCP split/coalesced input, cadence, FIN/RST, seats, handshake and restart passed')
finally:
    for s in peers: s.close()
    server.terminate(); server.wait(timeout=3)
    log.close()
