import http.server
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time

binary = pathlib.Path(sys.argv[1]).resolve()


def write_config(directory, text):
    path = directory / "astra.conf"
    path.write_text(text, encoding="utf-8")
    return path


def request(port, body=b""):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
        client.settimeout(3)
        payload = b"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n" + body
        client.sendall(payload)
        chunks = []
        while True:
            try:
                part = client.recv(4096)
            except ConnectionResetError:
                break
            if not part:
                break
            chunks.append(part)
    response = b"".join(chunks)
    return int(response.split(b"\r\n", 1)[0].split()[1]), response.split(b"\r\n\r\n", 1)[1]


with tempfile.TemporaryDirectory(prefix="astra-config-server-") as temp:
    directory = pathlib.Path(temp)
    source = directory / "server.astra"
    source.write_text('''import "std:server" as http;
const app = http.create();
app.get("/ping", (req, res) => res.send("ok"));
app.post("/echo", (req, res) => res.send(req.body));
app.listen(18767, { host: "127.0.0.1" });
''', encoding="utf-8")
    write_config(directory, '''[server]
host = "127.0.0.1"
workers = 2
queue_capacity = 32
backlog = 64
max_connections = 16
max_connections_per_ip = 8
max_request_buffer = 8b
max_response_buffer = 1kb
max_header_buffer = 8kb
request_timeout = 2s
max_tracked_ips = 256

[rate_limit]
enabled = true
window = 10s
max_requests_total = 8
max_requests_per_ip = 5

[client]
max_request_buffer = 16b
max_response_buffer = 128b
max_header_buffer = 8kb
timeout = 2s
connect_timeout = 1s
max_redirects = 2
verify_tls = true
''')
    with tempfile.TemporaryFile(mode="w+t") as output:
        proc = subprocess.Popen([str(binary), str(source)], cwd=directory, stdout=output, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.time() + 8
            while time.time() < deadline:
                output.seek(0)
                if "Astra server listening" in output.read():
                    break
                if proc.poll() is not None:
                    raise RuntimeError("server exited before listening: " + (proc.stderr.read() or ""))
                time.sleep(0.05)
            else:
                raise RuntimeError("server did not start with astra.conf")

            large_status, _ = request(18767, b"0123456789abcdef")
            assert large_status == 413, large_status
            first_status, _ = request(18767)
            second_status, _ = request(18767)
            third_status, _ = request(18767)
            fourth_status, _ = request(18767)
            fifth_status, _ = request(18767)
            assert [first_status, second_status, third_status, fourth_status, fifth_status] == [200, 200, 200, 200, 429]
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()

with tempfile.TemporaryDirectory(prefix="astra-config-client-") as temp:
    directory = pathlib.Path(temp)
    config = write_config(directory, '''[client]
max_request_buffer = 16b
max_response_buffer = 128b
max_header_buffer = 8kb
timeout = 2s
connect_timeout = 1s
max_redirects = 2
verify_tls = true
''')

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = b"x" * 1024
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            self.rfile.read(length)
            body = b"ok"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_):
            return

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    port = server.server_address[1]
    source = directory / "client.astra"
    source.write_text(f'''import "std:testing" as testing;
pub async fn main() {{
    let responseBlocked = false;
    try {{ await launch.get("http://127.0.0.1:{port}/large"); }} catch (error) {{ responseBlocked = true; }}
    testing.assert(responseBlocked);
    let requestBlocked = false;
    try {{ await launch.post("http://127.0.0.1:{port}/post", "12345678901234567890"); }} catch (error) {{ requestBlocked = true; }}
    testing.assert(requestBlocked);
    const overrideResponse = await launch.post("http://127.0.0.1:{port}/post", "12345678901234567890", {{ maxRequestBytes: 64 }});
    testing.assert(overrideResponse.status == 200);
}}
''', encoding="utf-8")
    try:
        result = subprocess.run([str(binary), str(source)], cwd=directory, env={**__import__("os").environ, "ASTRA_CONFIG": str(config)}, capture_output=True, text=True, timeout=12)
        if result.returncode != 0:
            raise RuntimeError("launch runtime configuration test failed: " + result.stdout + result.stderr)
    finally:
        server.shutdown()
        server.server_close()

with tempfile.TemporaryDirectory(prefix="astra-config-invalid-") as temp:
    directory = pathlib.Path(temp)
    write_config(directory, "[server]\nunknown_setting = 1\n")
    result = subprocess.run([str(binary), "config", "check"], cwd=directory, capture_output=True, text=True)
    if result.returncode == 0 or "unknown setting" not in result.stderr:
        raise RuntimeError("unknown config key was not rejected")
    write_config(directory, "[server]\nworkers = invalid\n")
    result = subprocess.run([str(binary), "config", "check"], cwd=directory, capture_output=True, text=True)
    if result.returncode == 0 or "valid integer" not in result.stderr:
        raise RuntimeError("invalid config type was not rejected")
    write_config(directory, "[server]\nmax_request_buffer = 1tb\n")
    result = subprocess.run([str(binary), "config", "check"], cwd=directory, capture_output=True, text=True)
    if result.returncode == 0 or "must use B, KB, MB, or GB units" not in result.stderr:
        raise RuntimeError("invalid config unit was not rejected")

print("runtime configuration integration: server limits, rate limits, launch buffers, overrides, and invalid keys OK")
