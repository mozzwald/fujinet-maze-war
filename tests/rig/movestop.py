"""A remote player that alternates walking and standing still. Auto-restarts
its own socket on error so a transient hiccup doesn't kill the whole bot."""
import socket, sys, time

def cobs_decode(pkt):
    if not pkt or pkt[-1] != 0: return b""
    frame = pkt[:-1]; out = bytearray(); rd = 0
    while rd < len(frame):
        code = frame[rd]; rd += 1
        if code == 0: return b""
        for _ in range(code - 1):
            if rd >= len(frame): return b""
            out.append(frame[rd]); rd += 1
        if code != 0xFF and rd < len(frame): out.append(0)
    return bytes(out)

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9500
R, D, L, U, N = 0x07, 0x0D, 0x0B, 0x0E, 0x0F

def run():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("127.0.0.1", PORT)); s.settimeout(0.02)
    seq = 0; pid = None

    def send(joy):
        nonlocal seq
        s.send(bytes([0x41, seq, pid or 0, joy])); seq = (seq + 1) & 0xFF

    def pump(secs):
        nonlocal pid
        end = time.time() + secs
        while time.time() < end:
            try: p = cobs_decode(s.recv(256))
            except socket.timeout: continue
            if len(p) >= 20 and p[0] == 0x40: pid = (p[2] >> 1) & 0x03

    send(N); pump(1.0)
    print("bot pid", pid, flush=True)
    phase = 0
    while True:
        t0 = time.time()
        print("WALK", flush=True)
        while time.time() - t0 < 4.0:
            send(R if (phase % 2 == 0) else L)
            pump(0.09)
        t0 = time.time()
        print("STILL", flush=True)
        while time.time() - t0 < 4.0:
            send(N)
            pump(0.09)
        phase += 1

while True:
    try:
        run()
    except Exception as e:
        print("CRASH, restarting:", repr(e), flush=True)
        time.sleep(0.5)
