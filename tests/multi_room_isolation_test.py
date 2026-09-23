#!/usr/bin/env python3
"""Exercise two room listeners and prove their mutable state stays isolated."""

import os
import socket
import subprocess
import sys
import tempfile
import time

from tcp_frames import decode_frame, recv_frame, send_frame


def consecutive_ports():
    for _ in range(100):
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        if port >= 65535:
            continue
        sockets = []
        try:
            for candidate in (port, port + 1):
                item = socket.socket()
                item.bind(("127.0.0.1", candidate))
                sockets.append(item)
        except OSError:
            continue
        finally:
            for item in sockets:
                item.close()
        return port
    raise RuntimeError("could not reserve consecutive test ports")


def connect(port, deadline):
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.2)
            sock.settimeout(0.6)
            return sock
        except OSError:
            time.sleep(0.02)
    raise RuntimeError(f"server did not listen on {port}")


def payload(sock):
    return decode_frame(recv_frame(sock))


def wait_type(sock, packet_type, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            packet = payload(sock)
        except socket.timeout:
            continue
        if packet and packet[0] == packet_type:
            return packet
    raise AssertionError(f"packet {packet_type:02x} not received")


def assert_no_type(sock, packet_type, duration):
    deadline = time.monotonic() + duration
    old_timeout = sock.gettimeout()
    sock.settimeout(0.1)
    try:
        while time.monotonic() < deadline:
            try:
                packet = payload(sock)
            except socket.timeout:
                continue
            assert packet[0] != packet_type, (
                f"room received leaked packet {packet_type:02x}: {packet.hex()}"
            )
    finally:
        sock.settimeout(old_timeout)


def bit_is_set(packet, x, y):
    index = y * 20 + x
    return bool(packet[3 + index // 8] & (1 << (index % 8)))


def main(root):
    port = consecutive_ports()
    maze = ["#" * 20]
    maze.extend("#" + "." * 18 + "#" for _ in range(1, 18))
    maze.append("#" * 20)
    # Put exactly one mutable interior brick at (5,5).
    row = list(maze[5])
    row[5] = "#"
    maze[5] = "".join(row)
    maze_path = None
    clients = []
    server = None
    try:
        with tempfile.NamedTemporaryFile("w", delete=False) as handle:
            maze_path = handle.name
            handle.write("\n".join(maze) + "\n")
        server_binary = os.environ.get(
            "MAZE_WAR_SERVER", os.path.join(root, "build/maze-war-server")
        )
        server = subprocess.Popen(
            [
                server_binary,
                "--bind", "127.0.0.1",
                "--port-base", str(port),
                "--room-count", "2",
                "--room-zombies", "0,3",
                "--tick-hz", "20",
                "--brick", maze_path,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        deadline = time.monotonic() + 3
        room0 = connect(port, deadline)
        room1 = connect(port + 1, deadline)
        clients.extend((room0, room1))

        full0 = wait_type(room0, 0x50)
        full1 = wait_type(room1, 0x50)
        assert bit_is_set(full0, 5, 5) and bit_is_set(full1, 5, 5)
        snap0 = wait_type(room0, 0x40)
        snap1 = wait_type(room1, 0x40)
        assert ((snap0[2] >> 3) & 0x0F) == 0
        assert ((snap1[2] >> 3) & 0x0F) == 0x0E

        send_frame(room0, bytes((0x43, 1, 0)) + b"ALPHA   ")
        name0 = wait_type(room0, 0x43)
        assert name0[3:11] == b"ALPHA   "
        assert_no_type(room1, 0x43, 0.25)
        send_frame(room1, bytes((0x43, 1, 0)) + b"BRAVO   ")
        name1 = wait_type(room1, 0x43)
        assert name1[3:11] == b"BRAVO   "

        send_frame(room0, bytes((0x51, 2, 5, 5)))
        delta0 = wait_type(room0, 0x51)
        assert delta0[2:4] == bytes((5, 5))
        assert_no_type(room1, 0x51, 0.35)
        repaired0 = wait_type(room0, 0x50, timeout=4.0)
        repaired1 = wait_type(room1, 0x50, timeout=4.0)
        assert not bit_is_set(repaired0, 5, 5)
        assert bit_is_set(repaired1, 5, 5)

        # A room-0 sender that never reads must not delay room 1's tick stream.
        stalled = connect(port, time.monotonic() + 2)
        clients.append(stalled)
        baseline_seq = wait_type(room1, 0x40)[1]
        for seq in range(40):
            try:
                send_frame(stalled, bytes((0x43, seq, 0)) + b"FLOOD   ")
            except (BrokenPipeError, ConnectionResetError):
                # Queue saturation now deliberately disconnects only this
                # stalled peer instead of silently dropping reliable events.
                break
        stamps = []
        snapshot_seqs = []
        deadline = time.monotonic() + 2
        while len(stamps) < 6 and time.monotonic() < deadline:
            packet = payload(room1)
            if packet[0] == 0x40:
                stamps.append(time.monotonic())
                snapshot_seqs.append(packet[1])
        assert len(stamps) == 6
        assert max(b - a for a, b in zip(stamps, stamps[1:])) < 0.4
        assert 0 < ((snapshot_seqs[-1] - baseline_seq) & 0xFF) < 24

        # Disconnect/reconnect exercises slot and descriptor reuse. The new
        # room-0 peer must receive a clean slot-0 snapshot, while room 1 lives.
        room0.close()
        clients.remove(room0)
        time.sleep(0.12)
        replacement = connect(port, time.monotonic() + 2)
        clients.append(replacement)
        replacement_snap = wait_type(replacement, 0x40)
        assert ((replacement_snap[2] >> 1) & 0x03) == 0
        assert wait_type(room1, 0x40)[0] == 0x40
    finally:
        for client in clients:
            try:
                client.close()
            except OSError:
                pass
        if server is not None:
            server.terminate()
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if server.returncode not in (0, -15):
                output = server.stdout.read() if server.stdout else ""
                raise AssertionError(f"server exited {server.returncode}\n{output}")
        if maze_path is not None:
            os.unlink(maze_path)


if __name__ == "__main__":
    main(os.path.abspath(sys.argv[1]))
    print("multi-room isolation smoke passed")
