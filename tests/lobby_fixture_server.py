#!/usr/bin/env python3
"""Small controlled format-1 Lobby endpoint for Atari/FujiNet browser tests."""

import argparse
import struct
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def fixed(text, maximum):
    raw = text.encode("ascii")[:maximum]
    return raw + bytes(maximum + 1 - len(raw))


def record(name, host, port, players, appkey):
    payload = (bytes((appkey,)) + fixed("Maze War", 16) + fixed(name, 32) +
               fixed(f"tcp://{host}:{port}", 64) +
               fixed("tnfs://fixture.test/maze-war-net.xex", 64) +
               fixed("us", 2) + bytes((1, players, 4, 0, 0)))
    assert len(payload) == 189
    return payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--game-host", default="127.0.0.1")
    parser.add_argument("--port-base", type=int, default=9100)
    parser.add_argument("--appkey", type=int, default=3)
    args = parser.parse_args()
    rooms = [record("FIXTURE NORTH", args.game_host, args.port_base, 1,
                    args.appkey),
             record("FIXTURE SOUTH", args.game_host, args.port_base + 1, 2,
                    args.appkey)]

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            query = urllib.parse.parse_qs(parsed.query)
            if (parsed.path != "/view" or query.get("bin") != ["1"] or
                    query.get("platform") != ["atari"] or
                    query.get("appkey") != [str(args.appkey)]):
                self.send_error(400)
                return
            page_size = min(max(int(query.get("pagesize", ["4"])[0]), 1), 4)
            page = min(max(int(query.get("page", ["0"])[0]), 0), 7)
            selected = rooms[page * page_size:(page + 1) * page_size]
            body = bytes((len(selected), 0, 0)) + b"".join(selected)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *values):
            print(fmt % values, flush=True)

    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
