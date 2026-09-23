#!/bin/sh

# A vacant seat's stored coordinates are historical. Prove a player can walk
# onto that cell, then rejoin the seat without overlapping, and prove existing
# clients receive an immediate final RESPAWN before the newcomer moves.

set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export PYTHONPATH="$ROOT_DIR/tests${PYTHONPATH:+:$PYTHONPATH}"
make -C "$ROOT_DIR" build/maze-war-server >/dev/null

python3 - "$ROOT_DIR" <<'PYEOF'
import os
import socket
import subprocess
import sys
import tempfile
import time

from tcp_frames import decode_frame, recv_frame, send_frame

root = sys.argv[1]


def fail(message):
    raise AssertionError(message)


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        self.sock.settimeout(0.04)
        self.pid = None
        self.seq = 1
        self.players = {}
        self.seat_mask = 0
        self.respawns = []
        # send_frame performs HELLO/WELCOME first; this neutral input neither
        # moves the actor nor makes the join-redraw assertion depend on motion.
        self.send_joy(0x0F)
        self.wait_ready()

    def handle(self, packet):
        if len(packet) >= 21 and packet[0] == 0x40:
            self.pid = (packet[2] >> 1) & 0x03
            for slot in range(4):
                self.players[slot] = (packet[3 + slot * 2],
                                      packet[4 + slot * 2])
        elif len(packet) == 3 and packet[0] == 0x44:
            self.seat_mask = packet[2] & 0x0F
        elif len(packet) == 7 and packet[0] == 0x52:
            self.respawns.append(tuple(packet[2:6]))

    def pump(self, duration=0.1):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                self.handle(decode_frame(recv_frame(self.sock, 256)))
            except socket.timeout:
                pass

    def wait_ready(self):
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            self.pump(0.1)
            if self.pid is not None and self.pid in self.players:
                return
        fail("client never received an authoritative snapshot")

    def send_joy(self, joy):
        send_frame(self.sock,
                   bytes((0x41, self.seq & 0xFF, self.pid or 0, joy)))
        self.seq += 1

    def step_to(self, expected, joy):
        self.send_joy(joy)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            self.pump(0.05)
            if self.players.get(self.pid) == expected:
                return
        fail(f"player {self.pid} did not move to {expected}; "
             f"last={self.players.get(self.pid)}")

    def leave(self):
        send_frame(self.sock, bytes((0x56, 0xA5)))
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            try:
                packet = decode_frame(recv_frame(self.sock, 256))
            except socket.timeout:
                continue
            except BrokenPipeError:
                # recv_frame auto-ACKs reliable traffic encountered ahead of
                # LEAVE_ACK. The server may already have finished its bounded
                # close, but the echoed leave ACK remains readable in TCP's
                # receive buffer.
                continue
            if packet == bytes((0x57, 0xA5)):
                self.sock.close()
                return
            self.handle(packet)
        fail("leaving client did not receive LEAVE_ACK")


probe = socket.socket()
probe.bind(("127.0.0.1", 0))
port = probe.getsockname()[1]
probe.close()

with tempfile.TemporaryDirectory() as tmp:
    bricks = os.path.join(tmp, "open-maze.txt")
    log_path = os.path.join(tmp, "server.log")
    with open(bricks, "w", encoding="ascii") as maze:
        for y in range(19):
            maze.write("#" * 20 if y in (0, 18)
                       else "#" + "." * 18 + "#")
            maze.write("\n")

    with open(log_path, "w+", encoding="utf-8") as log:
        server = subprocess.Popen(
            [os.path.join(root, "build/maze-war-server"),
             "--port", str(port), "--tick-hz", "30", "--zombies", "0",
             "--brick", bricks, "--debug"],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            # The full suite assembles and launches many binaries back to back;
            # leave enough startup margin that scheduler load is not mistaken
            # for a failed listener.
            time.sleep(0.5)
            observer = Client(port)
            leaver = Client(port)
            if (observer.pid, leaver.pid) != (0, 1):
                fail(f"expected slots 0 and 1, got {observer.pid}, {leaver.pid}")

            observer.pump(0.25)
            join_events = [r for r in observer.respawns
                           if r[0] == leaver.pid and r[3] & 0x02]
            if not join_events:
                fail("observer received no immediate final RESPAWN for join")
            stale_cell = leaver.players[leaver.pid]
            leaver.leave()

            deadline = time.monotonic() + 1.0
            while time.monotonic() < deadline:
                observer.send_joy(0x0F)
                observer.pump(0.05)
                if not (observer.seat_mask & (1 << 1)):
                    break
            if observer.seat_mask & (1 << 1):
                fail("departed seat never became vacant")

            # The open test maze lets the observer occupy the departed seat's
            # old cell. This turns the historical-coordinate bug deterministic.
            while observer.players[0][0] != stale_cell[0]:
                x, y = observer.players[0]
                dx = 1 if stale_cell[0] > x else -1
                observer.step_to((x + dx, y), 0x07 if dx > 0 else 0x0B)
            while observer.players[0][1] != stale_cell[1]:
                x, y = observer.players[0]
                dy = 1 if stale_cell[1] > y else -1
                observer.step_to((x, y + dy), 0x0D if dy > 0 else 0x0E)
            if observer.players[0] != stale_cell:
                fail("observer did not occupy the vacant seat's historical cell")

            before = len(observer.respawns)
            rejoin = Client(port)
            if rejoin.pid != 1:
                fail(f"rejoin expected slot 1, got {rejoin.pid}")
            observer.pump(0.3)
            immediate = [r for r in observer.respawns[before:]
                         if r[0] == rejoin.pid and r[3] & 0x02]
            if not immediate:
                fail("existing client received no immediate rejoin RESPAWN")
            spawn = (immediate[-1][1], immediate[-1][2])
            if spawn == observer.players[0]:
                fail(f"rejoining player spawned on observer at {spawn}")
            if rejoin.players[rejoin.pid] != spawn:
                fail("rejoin snapshot and immediate RESPAWN disagree")

            print("vacant-seat rejoin uses a distinct spawn and immediate RESPAWN")
        finally:
            server.terminate()
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=2)

        log.seek(0)
        server_log = log.read()
        if "TX join respawn pid=1" not in server_log:
            fail("server debug log has no join respawn marker")

atari = os.path.join(root, "clients/atari/maze-war.asm")
with open(atari, encoding="utf-8") as source:
    text = source.read()
filled_start = text.index("\nNVU_FILLED")
filled = text[filled_start:text.index("\nNVU_NX", filled_start)]
if "NET_REDRAW_MASK" not in filled:
    fail("Atari vacant-to-filled path does not force an actor redraw")

print("join spawn smoke passed")
PYEOF
