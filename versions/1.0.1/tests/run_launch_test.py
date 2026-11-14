#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

astra = str(Path(sys.argv[1]).resolve())
source = str(Path(sys.argv[2]).resolve())

barrier = threading.Barrier(2)

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass
    def _send(self, status, payload=None):
        body = b"" if payload is None else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)
    def do_GET(self):
        if self.path.startswith("/concurrent"):
            try:
                barrier.wait(timeout=3)
                self._send(200, {"concurrent": True})
            except threading.BrokenBarrierError:
                self._send(503, {"concurrent": False})
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/get")
            self.end_headers()
        elif self.path == "/missing":
            self._send(404, {"error": "not found"})
        elif self.path == "/large":
            body = b"0123456789abcdef"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self._send(200, {"method": "GET", "path": self.path, "testHeader": self.headers.get("X-Astra-Test", "")})
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        self._send(201, {"method": "POST", "received": body})
    def do_PUT(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        self._send(200, {"method": "PUT", "received": body})
    def do_PATCH(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode()
        self._send(200, {"method": "PATCH", "received": body})
    def do_DELETE(self):
        self._send(202, {"method": "DELETE"})
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Allow", "GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS")
        self.end_headers()
    def do_HEAD(self):
        self._send(200)

server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
env = os.environ.copy()
env["ASTRA_HTTP_TEST_URL"] = f"http://127.0.0.1:{server.server_port}"
try:
    result = subprocess.run([astra, "run", source], env=env, text=True, capture_output=True, timeout=20)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)
print("local HTTP integration test passed")
