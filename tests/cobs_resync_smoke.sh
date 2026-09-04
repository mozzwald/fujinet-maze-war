#!/bin/sh

# COBS framing must realign the parser after damage.
#
# This is the property the framing exists for. The Atari receives over SIO as a
# byte stream, and the old parser scanned for a type marker then took a fixed
# count of bytes, so one byte lost or gained shifted everything and payload
# bytes began to be read as markers -- for as long as it took a payload byte to
# look like one. A checksum makes a damaged packet fail closed but cannot
# realign. A zero delimiter can, because COBS guarantees no zero inside a frame.
#
# The decoder below is a literal transcription of NET_COBS_DECODE and
# NET_FRAME_CKSUM from clients/atari/maze-war.asm, kept in step with it by
# hand. It is checked against the server's real encoder by
# packet_checksum_smoke; what this test adds is the resync guarantee, which
# needs a deliberately damaged stream and so cannot be observed on a healthy
# link.

set -eu

python3 <<'PYEOF'
import random


# --- literal transcription of NET_COBS_DECODE (in-place, as the 6502 does) ---
def cobs_decode_asm(buf, frame_idx):
    rd = 0
    wr = 0
    while True:                        # NCD_GRP
        if rd >= frame_idx:            # CPY NET_FRAME_IDX / BCS NCD_OK
            return wr, False
        code = buf[rd]                 # LDA NET_FRAME_BUF,Y
        if code == 0:                  # BEQ NCD_BAD
            return 0, True
        rd += 1
        n = 1
        while True:                    # NCD_CPY
            if n >= code:              # CMP NET_COBS_CODE / BCS NCD_ZERO
                break
            if rd >= frame_idx:        # group overruns the delimiter
                return 0, True
            buf[wr] = buf[rd]
            rd += 1
            wr += 1
            n += 1
        if code == 0xFF:               # NCD_ZERO
            continue
        if rd >= frame_idx:
            return wr, False
        buf[wr] = 0
        wr += 1


# --- literal transcription of NET_FRAME_CKSUM ---
def cksum_asm(buf, length):
    ck_len = (length - 1) & 0xFF
    total = 0
    for y in range(ck_len):
        total = (total + buf[y]) & 0xFF
    return buf[ck_len] == total


def encode(raw):
    out = bytearray([0])
    code = 1
    ci = 0
    for b in raw:
        if b == 0:
            out[ci] = code
            ci = len(out)
            out.append(0)
            code = 1
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[ci] = code
                ci = len(out)
                out.append(0)
                code = 1
    out[ci] = code
    return bytes(out) + b"\x00"


def with_cksum(raw):
    t = 0
    for b in raw:
        t = (t + b) & 0xFF
    return bytes(raw) + bytes([t])


def parse(stream):
    """the Atari parser: accumulate to the delimiter, decode, verify"""
    good = 0
    rejected = 0
    buf = bytearray()
    for b in stream:
        if b == 0:
            if buf:
                w = bytearray(buf) + bytearray(8)
                n, err = cobs_decode_asm(w, len(buf))
                if err or n < 2 or not cksum_asm(w, n):
                    rejected += 1
                else:
                    good += 1
            buf = bytearray()
        else:
            buf.append(b)
    return good, rejected


def fail(m):
    raise SystemExit("FAIL: " + m)


# Snapshot-shaped frames with plenty of interior zeros, which is what makes
# COBS do real work: neutral joys are $0F and idle scores are 0.
random.seed(7)
frames = []
for i in range(40):
    body = bytes([0x40, i & 0xFF, 0x81]
                 + [random.choice([0, 1, 5, 17, 0, 0x0F]) for _ in range(16)]
                 + [i & 0xFF])
    frames.append(with_cksum(body))
stream = b"".join(encode(f) for f in frames)

clean, rej = parse(stream)
if clean != len(frames) or rej:
    fail(f"clean stream: {clean} accepted, {rej} rejected, expected {len(frames)}/0")

mid = len(stream) // 2
for label, damaged in (
    ("byte deleted", stream[:mid] + stream[mid + 1:]),
    ("byte inserted", stream[:mid] + b"\x7e" + stream[mid:]),
    ("byte flipped", stream[:mid] + bytes([stream[mid] ^ 0xFF]) + stream[mid + 1:]),
):
    good, _ = parse(damaged)
    lost = clean - good
    if lost > 1:
        fail(f"{label} cost {lost} frames; COBS must cost at most one")
    print(f"  {label:14s}: {lost} frame lost, parser realigned")

print(f"clean stream: {clean}/{len(frames)} frames accepted")
PYEOF

echo "cobs resync smoke passed"
