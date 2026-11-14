import concurrent.futures
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

binary = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(sys.argv[2]).resolve()
count = 1000

with tempfile.TemporaryFile(mode="w+t") as output:
    proc = subprocess.Popen([str(binary), str(source)], stdout=output, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.time() + 8
        started = False
        while time.time() < deadline:
            output.seek(0)
            if "Astra server listening" in output.read():
                started = True
                break
            if proc.poll() is not None:
                raise RuntimeError("server exited before listening: " + (proc.stderr.read() or ""))
            time.sleep(0.05)
        if not started:
            raise RuntimeError("server did not start")

        def request(index):
            with socket.create_connection(("127.0.0.1", 18766), timeout=5) as client:
                client.settimeout(5)
                client.sendall(f"GET /ping/{index} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode())
                chunks = []
                while True:
                    part = client.recv(8192)
                    if not part:
                        break
                    chunks.append(part)
            response = b"".join(chunks)
            if b"\r\n\r\n" not in response:
                raise RuntimeError(f"request {index}: malformed response")
            head, body = response.split(b"\r\n\r\n", 1)
            status = int(head.split(b"\r\n", 1)[0].split()[1])
            payload = json.loads(body.decode())
            if status != 200 or payload != {"id": str(index), "ok": True}:
                raise RuntimeError(f"request {index}: unexpected response {status} {payload!r}")
            return True

        with concurrent.futures.ThreadPoolExecutor(max_workers=64) as pool:
            results = list(pool.map(request, range(count)))
        if len(results) != count:
            raise RuntimeError("not all concurrent requests completed")
        proc.wait(timeout=10)
        if proc.returncode != 0:
            raise RuntimeError("Astra server returned non-zero status: " + (proc.stderr.read() or ""))
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
print(f"HTTP server concurrent integration: OK ({count} requests, 64 clients)")
