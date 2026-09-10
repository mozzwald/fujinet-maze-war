#!/usr/bin/env python3
"""Measure real-server movement cadence without an Atari or the legacy UDP rig.

Starts and stops its own isolated loopback server. Output is JSON, including
raw snapshot rows. This measures host TCP delivery, not Atari rendering or SIO.
Run after `make build/maze-war-server`.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from tcp_frames import decode_frame, send_frame


def run(fps, seconds, frames):
    with tempfile.TemporaryDirectory(prefix="maze-cadence-") as tmp:
        maze = Path(tmp) / "open.txt"
        maze.write_text("\n".join(["#" * 20] + ["#" + "." * 18 + "#"] * 17 + ["#" * 20]) + "\n")
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        with open(Path(tmp) / "server.log", "w+") as log:
            server = subprocess.Popen([
                str(ROOT / "build/maze-war-server"), "--bind", "127.0.0.1",
                "--port", str(port), "--zombies", "0", "--brick", str(maze),
            ], stdout=log, stderr=log, cwd=ROOT)
            try:
                deadline = time.monotonic() + 3
                while True:
                    if server.poll() is not None:
                        log.seek(0)
                        raise RuntimeError(log.read())
                    try:
                        sock = socket.create_connection(("127.0.0.1", port), timeout=.1)
                        break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(.02)
                with sock:
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    sock.settimeout(.5)
                    start = time.monotonic()
                    next_send = start
                    seq, joy, x, pid = 0, 7, 10, None
                    pending = bytearray()
                    rows, sends = [], []
                    while time.monotonic() - start < seconds:
                        now = time.monotonic()
                        if now >= next_send:
                            if x >= 15:
                                joy = 11
                            elif x <= 4:
                                joy = 7
                            if pid is not None:
                                send_frame(sock, bytes([0x41, seq & 255, pid, joy]))
                                sends.append((round((now - start) * 1000, 3), seq & 255))
                                seq += 1
                            next_send += frames / fps
                        if not select.select([sock], [], [], max(0, min(.01, next_send - time.monotonic())))[0]:
                            continue
                        chunk = sock.recv(4096)
                        if not chunk:
                            raise RuntimeError("server disconnected")
                        pending.extend(chunk)
                        while 0 in pending:
                            end = pending.index(0) + 1
                            packet = decode_frame(bytes(pending[:end]))
                            del pending[:end]
                            if packet[0] != 0x40:
                                continue
                            pid = (packet[2] >> 1) & 3
                            x, y = packet[3 + 2 * pid:5 + 2 * pid]
                            rows.append(dict(ms=round((time.monotonic() - start) * 1000, 3),
                                             x=x, y=y, joy=packet[11 + pid],
                                             ack=packet[19] if packet[2] & 128 else None))
                    if len(rows) < seconds * 8:
                        raise RuntimeError("too few snapshots for a useful baseline")
                    applied = [r for i, r in enumerate(rows) if r["ack"] is not None
                               and (i == 0 or r["ack"] != rows[i - 1]["ack"])]
                    gaps = [b["ms"] - a["ms"] for a, b in zip(applied, applied[1:])]
                    snapshot_gaps = [b["ms"] - a["ms"] for a, b in zip(rows, rows[1:])]
                    return dict(fps=fps, frames=frames, seconds=seconds,
                                inputs=len(sends), snapshots=len(rows),
                                applied=len(applied),
                                application_intervals_ms=dict(Counter(round(g / 100) * 100 for g in gaps)),
                                snapshot_interval_ms=dict(min=min(snapshot_gaps), max=max(snapshot_gaps)),
                                neutral_snapshots=sum(r["joy"] == 15 for r in rows[2:]),
                                sends=sends, rows=rows)
            finally:
                if server.poll() is None:
                    server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fps", type=int, choices=(50, 60), default=60)
    parser.add_argument("--frames", type=int, default=None,
                        help="display frames between DELTAs; defaults to 6 at 60Hz, 5 at 50Hz")
    parser.add_argument("--seconds", type=float, default=10)
    args = parser.parse_args()
    if args.seconds < 3:
        parser.error("--seconds must be at least 3")
    frames = args.frames if args.frames is not None else (6 if args.fps == 60 else 5)
    if frames <= 0:
        parser.error("--frames must be positive")
    print(json.dumps(run(args.fps, args.seconds, frames), indent=2))
