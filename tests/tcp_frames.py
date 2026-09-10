"""Receive one delimited server frame, preserving bytes across TCP reads.

Existing gameplay tests keep their own decoders and assertions. This helper
changes only the transport boundary they used to get from UDP.
"""
import time
import socket
import weakref

_buffers = weakref.WeakKeyDictionary()


def crc16_ccitt_false(payload):
    crc = 0xffff
    for byte in payload:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc


def encode_frame(payload):
    raw = bytearray(payload)
    crc = crc16_ccitt_false(raw)
    raw.extend((crc & 0xff, crc >> 8))
    out = bytearray([0])
    code_at, code = 0, 1
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


def send_frame(sock, payload):
    sock.sendall(encode_frame(payload))


def decode_frame(frame):
    if not frame or frame[-1] != 0:
        raise ValueError("frame missing delimiter")
    src = frame[:-1]
    out = bytearray()
    i = 0
    while i < len(src):
        code = src[i]
        i += 1
        if code == 0 or i + code - 1 > len(src):
            raise ValueError("bad COBS frame")
        out.extend(src[i:i + code - 1])
        i += code - 1
        if code != 0xFF and i < len(src):
            out.append(0)
    if len(out) < 3:
        raise ValueError("short decoded frame")
    crc = crc16_ccitt_false(out[:-2])
    if out[-2:] != bytes((crc & 0xff, crc >> 8)):
        raise ValueError("bad CRC")
    return bytes(out[:-2])


def recv_frame(sock, size=256):
    pending = _buffers.setdefault(sock, bytearray())
    timeout = sock.gettimeout()
    deadline = None if timeout is None else time.monotonic() + timeout
    try:
        while True:
            end = pending.find(0)
            if end >= 0:
                frame = bytes(pending[:end + 1])
                del pending[:end + 1]
                return frame
            if deadline is not None and timeout != 0:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise socket.timeout('incomplete frame')
                sock.settimeout(remaining)
            data = sock.recv(size)
            if not data:
                raise ConnectionError('server closed TCP stream')
            pending.extend(data)
            if len(pending) > 4096:
                raise ValueError('server frame exceeds limit')
    finally:
        sock.settimeout(timeout)
