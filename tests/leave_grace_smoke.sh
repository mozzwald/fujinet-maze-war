#!/bin/sh

# Exercise the real TCP server's voluntary leave and unexpected-loss policy.
# A short command-line grace keeps the test quick; production defaults to 60s.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
make -C "$ROOT_DIR" build/maze-war-server >/dev/null

python3 - "$ROOT_DIR" <<'PYEOF'
import os
import socket
import subprocess
import sys
import tempfile
import time

root = sys.argv[1]


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(payload):
    raw = bytearray(payload)
    crc = crc16(raw)
    raw += bytes((crc & 0xFF, crc >> 8))
    out = bytearray((0,))
    code_at = 0
    code = 1
    for byte in raw:
        if byte == 0:
            out[code_at] = code
            code_at = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
    out[code_at] = code
    out.append(0)
    return bytes(out)


def decode(encoded):
    out = bytearray()
    pos = 0
    while pos < len(encoded):
        code = encoded[pos]
        pos += 1
        if code == 0 or pos + code - 1 > len(encoded):
            raise AssertionError("invalid COBS frame")
        out += encoded[pos:pos + code - 1]
        pos += code - 1
        if code != 255 and pos < len(encoded):
            out.append(0)
    if len(out) < 3 or crc16(out[:-2]) != out[-2] | (out[-1] << 8):
        raise AssertionError("invalid framed CRC")
    return bytes(out[:-2])


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        self.sock.settimeout(0.05)
        self.buf = bytearray()
        self.sock.sendall(frame(bytes((0x46, 1))))
        welcome = self.wait_type(0x47)
        self.round_id = welcome[2]

    def packets(self, timeout=0.05):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while 0 in self.buf:
                cut = self.buf.index(0)
                encoded = bytes(self.buf[:cut])
                del self.buf[:cut + 1]
                if encoded:
                    yield decode(encoded)
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                return
            self.buf += data

    def wait_type(self, packet_type, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for pkt in self.packets(min(0.1, deadline - time.monotonic())):
                if pkt and pkt[0] == packet_type:
                    return pkt
        raise AssertionError(f"packet ${packet_type:02X} not received")

    def leave(self, seq=0xA5, duplicate=False):
        wire = frame(bytes((0x56, seq)))
        self.sock.sendall(wire + (wire if duplicate else b""))
        ack = self.wait_type(0x57)
        assert ack == bytes((0x57, seq)), ack
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            try:
                if not self.sock.recv(4096):
                    break
            except socket.timeout:
                continue
        else:
            raise AssertionError("server did not close departing TCP socket")
        self.sock.close()

    def abort(self):
        self.sock.close()


probe = socket.socket()
probe.bind(("127.0.0.1", 0))
port = probe.getsockname()[1]
probe.close()

with tempfile.TemporaryDirectory() as tmp:
    log_path = os.path.join(tmp, "server.log")
    with open(log_path, "w+") as log:
        server = subprocess.Popen([
            os.path.join(root, "build/maze-war-server"),
            "--bind", "127.0.0.1", "--port", str(port), "--zombies", "1",
            "--no-human-grace-ms", "350", "--debug",
        ], cwd=root, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 2
            while True:
                try:
                    first = Client(port)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.02)

            initial_round = first.round_id
            first.leave(duplicate=True)

            # Final voluntary leave resets directly to dormant; a fresh HELLO
            # wakes a clean next round without entering grace.
            second = Client(port)
            assert second.round_id != initial_round
            preserved_round = second.round_id

            # Unexpected EOF starts grace. A reconnect inside it retains the
            # current round and cancels the pending expiry.
            second.abort()
            time.sleep(0.12)
            third = Client(port)
            assert third.round_id == preserved_round

            # A later unexpected final loss which outlives grace resets to a
            # clean dormant round before the next join.
            third.abort()
            time.sleep(0.65)
            fourth = Client(port)
            assert fourth.round_id != preserved_round
            fourth.leave(seq=0x5A)
        finally:
            server.terminate()
            server.wait(timeout=2)
        log.flush()
        log.seek(0)
        output = log.read()

    assert output.count("LEAVE_ROOM slot=0 seq=165") == 1, output
    assert "dormant reason=voluntary-final-leave" in output, output
    assert output.count("no-human grace started") == 2, output
    assert "no-human grace canceled by slot=0" in output, output
    assert "dormant reason=no-human-grace-expired" in output, output

source = open(os.path.join(root, "server/main.c")).read()
assert "DEFAULT_NO_HUMAN_GRACE_MS = 60000" in source
assert "PKT_LEAVE_ROOM = 0x56" in source and "PKT_LEAVE_ACK = 0x57" in source
print("leave ACK, immediate seat release, grace cancel, and expiry passed")
PYEOF

echo "leave/grace smoke passed"
