"""Receive one delimited server frame, preserving bytes across TCP reads.

Existing gameplay tests keep their own decoders and assertions. This helper
changes only the transport boundary they used to get from UDP.
"""
import time
import socket
import weakref

_buffers = weakref.WeakKeyDictionary()
_queued_frames = weakref.WeakKeyDictionary()
_handshaken = weakref.WeakKeyDictionary()
_round_ids = weakref.WeakKeyDictionary()
_auto_ack = weakref.WeakKeyDictionary()
_reliable_revs = weakref.WeakKeyDictionary()

PKT_HELLO, PKT_WELCOME = 0x46, 0x47
PROTOCOL_VERSION = 1


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


def _recv_wire_frame(sock, size=256):
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


def _ensure_handshake(sock):
    if _handshaken.get(sock):
        return
    sock.sendall(encode_frame(bytes((PKT_HELLO, PROTOCOL_VERSION))))
    queued = _queued_frames.setdefault(sock, [])
    while True:
        frame = _recv_wire_frame(sock)
        payload = decode_frame(frame)
        queued.append(frame)
        if len(payload) == 5 and payload[0] == PKT_WELCOME:
            if payload[1] != PROTOCOL_VERSION:
                raise ConnectionError('incompatible server protocol')
            _round_ids[sock] = payload[2]
            _handshaken[sock] = True
            return


def send_frame(sock, payload):
    _ensure_handshake(sock)
    payload = bytes(payload)
    # Keep pre-08-03 gameplay fixtures concise while making their wire image
    # obey the epoch contract. Session-scoped packets are unchanged.
    if payload and ((payload[0] == 0x41 and len(payload) == 4) or
                    (payload[0] == 0x51 and len(payload) == 4) or
                    (payload[0] == 0x52 and len(payload) == 6)):
        payload += bytes((_round_ids[sock],))
    sock.sendall(encode_frame(payload))


def set_auto_ack(sock, enabled):
    """Let reliability-specific tests observe wrappers themselves."""
    _auto_ack[sock] = bool(enabled)


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
    _ensure_handshake(sock)
    queued = _queued_frames.setdefault(sock, [])
    while True:
        frame = queued.pop(0) if queued else _recv_wire_frame(sock, size)
        payload = decode_frame(frame)
        if len(payload) == 5 and payload[0] == PKT_WELCOME:
            _round_ids[sock] = payload[2]
        elif len(payload) >= 7 and payload[0] == 0x53:
            inner = payload[4:]
            control = inner[0] in (0x50, 0x55)
            if control or _auto_ack.get(sock, True):
                rev = payload[2] | payload[3] << 8
                sock.sendall(encode_frame(bytes((0x45, 0, rev & 0xff,
                                                  rev >> 8))))
                _reliable_revs[sock] = rev
                if inner[0] == 0x55:
                    _round_ids[sock] = inner[1]
                    continue
                # Present auto-acknowledged events as their inner packet so
                # gameplay fixtures keep testing the same semantic event.
                return encode_frame(inner)
        return frame
