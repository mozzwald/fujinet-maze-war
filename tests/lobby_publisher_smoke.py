#!/usr/bin/env python3
"""Black-box contract checks for the opt-in asynchronous Lobby publisher."""

import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
from tcp_frames import decode_frame, recv_frame, send_frame  # noqa: E402


def free_port():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


class FakeLobby:
    def __init__(self, mode="normal", fail_first=0, delay=0):
        self.mode, self.fail_first, self.delay = mode, fail_first, delay
        self.requests = []
        self.lock = threading.Lock()
        fake = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                raw = self.rfile.read(length)
                try:
                    body = json.loads(raw)
                except json.JSONDecodeError:
                    body = {"invalid": raw.decode("ascii", "replace")}
                with fake.lock:
                    fake.requests.append(body)
                    call = len(fake.requests)
                if fake.delay:
                    time.sleep(fake.delay)
                if fake.mode == "malformed":
                    self.connection.sendall(b"not an HTTP response\n")
                    self.close_connection = True
                    return
                if call <= fake.fail_first:
                    self.send_response(500)
                    self.end_headers()
                    self.wfile.write(b'{"success":false}')
                    return
                self.send_response(201)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b'{"success":true}')

            def log_message(self, _format, *_args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       daemon=True)

    @property
    def base(self):
        return f"http://127.0.0.1:{self.server.server_port}"

    def start(self):
        self.thread.start()

    def stop(self):
        self.server.shutdown()
        self.thread.join(timeout=2)
        self.server.server_close()

    def snapshot(self):
        with self.lock:
            return list(self.requests)


def wait_for(predicate, seconds, message):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError(message)


def lobby_args(fake, port, **extra):
    args = [os.path.join(ROOT, "build/maze-war-server"), "--port-base", str(port),
            "--room-count", str(extra.get("rooms", 1)), "--zombies", "1",
            "--tick-hz", "10", "--lobby-enabled", "--lobby-base", fake.base,
            "--lobby-appkey", "42", "--lobby-client-url", "fujinet://maze-war",
            "--lobby-public-host", "198.51.100.25", "--lobby-refresh-ms",
            str(extra.get("refresh", 1000)), "--lobby-timeout-ms",
            str(extra.get("timeout", 150)), "--lobby-shutdown-ms",
            str(extra.get("shutdown", 300))]
    if extra.get("rooms", 1) == 2:
        args += ["--lobby-room-names", "North,South"]
    return args


def launch(args):
    return subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, start_new_session=True)


def stop(process):
    process.send_signal(signal.SIGTERM)
    try:
        output, _ = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        raise AssertionError("publisher server did not stop within bound")
    if process.returncode:
        raise AssertionError(f"publisher server returned {process.returncode}: {output}")
    return output


def test_default_off():
    fake = FakeLobby()
    fake.start()
    process = launch([os.path.join(ROOT, "build/maze-war-server"), "--port",
                      str(free_port()), "--zombies", "0"])
    time.sleep(0.25)
    stop(process)
    fake.stop()
    assert not fake.snapshot(), "default server made a Lobby request"


def test_register_update_refresh_shutdown():
    fake = FakeLobby()
    fake.start()
    port = free_port()
    process = launch(lobby_args(fake, port, rooms=2))
    try:
        wait_for(lambda: len(fake.snapshot()) >= 2, 2, "initial rooms were not published")
        initial = fake.snapshot()[:2]
        assert {body["server"] for body in initial} == {"North", "South"}
        assert {body["serverurl"] for body in initial} == {
            f"tcp://198.51.100.25:{port}", f"tcp://198.51.100.25:{port + 1}"}
        for body in initial:
            assert body["game"] == "Maze War" and body["appkey"] == 42
            assert body["maxplayers"] == 4 and body["curplayers"] == 0
            assert body["status"] == "online"

        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        sock.settimeout(0.5)
        send_frame(sock, bytes((0x41, 1, 0, 0x0f)))
        seen_welcome = False
        until = time.monotonic() + 1
        while time.monotonic() < until:
            packet = decode_frame(recv_frame(sock, 256))
            if packet and packet[0] == 0x47:
                seen_welcome = True
                break
        assert seen_welcome, "game server did not service HELLO"
        wait_for(lambda: any(body.get("server") == "North" and
                              body.get("curplayers") == 1 and
                              body.get("status") == "online"
                              for body in fake.snapshot()), 2,
                 "human occupancy update was not published")
        count = len(fake.snapshot())
        wait_for(lambda: len(fake.snapshot()) >= count + 2, 2,
                 "periodic refresh was not published for each room")
        sock.close()
        output = stop(process)
        assert "lobby room=" in output
        wait_for(lambda: len([body for body in fake.snapshot()
                              if body.get("status") == "offline"]) == 2,
                 2, "shutdown did not publish offline state for both rooms")
    finally:
        if process.poll() is None:
            stop(process)
        fake.stop()


def test_error_and_malformed_reply_retry():
    for mode, failures in (("normal", 1), ("malformed", 0)):
        fake = FakeLobby(mode=mode, fail_first=failures)
        fake.start()
        process = launch(lobby_args(fake, free_port()))
        try:
            wait_for(lambda: len(fake.snapshot()) >= 2, 3,
                     f"{mode} reply did not cause a bounded retry")
            output = stop(process)
            assert "publish failed" in output, f"{mode} failure was not logged"
        finally:
            if process.poll() is None:
                stop(process)
            fake.stop()


def test_stalled_publisher_does_not_block_game_or_shutdown():
    fake = FakeLobby(delay=1)
    fake.start()
    port = free_port()
    process = launch(lobby_args(fake, port, timeout=100, shutdown=100))
    try:
        time.sleep(0.05)
        started = time.monotonic()
        sock = socket.create_connection(("127.0.0.1", port), timeout=0.3)
        sock.settimeout(0.3)
        send_frame(sock, bytes((0x41, 1, 0, 0x0f)))
        while True:
            packet = decode_frame(recv_frame(sock, 256))
            if packet and packet[0] == 0x47:
                break
        assert time.monotonic() - started < 0.3, "stalled Lobby delayed 10 Hz game service"
        sock.close()
        started = time.monotonic()
        stop(process)
        assert time.monotonic() - started < 0.8, "stalled Lobby delayed shutdown"
    finally:
        if process.poll() is None:
            stop(process)
        fake.stop()


def main():
    subprocess.run(["make", "-C", ROOT, "build/maze-war-server"], check=True,
                   stdout=subprocess.DEVNULL)
    test_default_off()
    test_register_update_refresh_shutdown()
    test_error_and_malformed_reply_retry()
    test_stalled_publisher_does_not_block_game_or_shutdown()
    print("lobby publisher smoke passed")


if __name__ == "__main__":
    main()
