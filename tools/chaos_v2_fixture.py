#!/usr/bin/env python3
"""Bounded normal temporary-audio fixture; starts only when explicitly run."""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import signal
import time

EXPECTED = "781f1a9def10816a70b07c3de77198e92d9a23b547519908b7da78cd0906980c"
DEFAULT_WAV = Path.home() / "voxone-hil" / "tts-abc-release-fixtures-1" / "tts-abc-release-4s-mono.wav"


class FixtureServer(HTTPServer):
    allow_reuse_address = True

    def __init__(self, address, wav, log_path, limit, seconds):
        super().__init__(address, Handler)
        self.wav = wav
        self.log = log_path.open("x", encoding="utf-8", buffering=1)
        self.limit = limit
        self.seconds = seconds
        self.count = 0
        self.started = time.monotonic()
        self.timeout = 0.5
        self.stopping = False

    def record(self, **row):
        self.log.write(json.dumps({"at_ns": time.time_ns(), **row}) + "\n")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        self.server.count += 1
        allowed = self.client_address[0] == "192.168.1.56" and self.path == "/chaos-v2/normal.wav"
        allowed = allowed and self.server.count <= self.server.limit
        self.server.record(kind="request", client=self.client_address[0], path=self.path, allowed=allowed)
        if not allowed:
            self.send_error(403)
            self.close_connection = True
            return
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(self.server.wav)))
        self.send_header("Connection", "close")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(self.server.wav)
        self.wfile.flush()
        self.server.record(kind="complete", body_bytes=len(self.server.wav))
        self.close_connection = True


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--bind", default="192.168.1.15")
    p.add_argument("--port", type=int, default=8099)
    p.add_argument("--wav", type=Path, default=DEFAULT_WAV)
    p.add_argument("--log", type=Path, required=True)
    p.add_argument("--max-requests", type=int, default=2)
    p.add_argument("--max-seconds", type=int, default=60)
    args = p.parse_args()
    if not 1 <= args.max_requests <= 4 or not 20 <= args.max_seconds <= 180:
        p.error("unsafe fixture limits")
    wav = args.wav.read_bytes()
    if hashlib.sha256(wav).hexdigest() != EXPECTED:
        p.error("fixture SHA mismatch")
    server = FixtureServer((args.bind, args.port), wav, args.log,
                           args.max_requests, args.max_seconds)

    def stop(*_args):
        server.stopping = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    server.record(kind="start", sha256=EXPECTED, max_requests=args.max_requests)
    try:
        while not server.stopping and server.count < server.limit and time.monotonic() - server.started < server.seconds:
            server.handle_request()
    finally:
        server.record(kind="stop", requests=server.count)
        server.server_close()
        server.log.close()


if __name__ == "__main__":
    main()
