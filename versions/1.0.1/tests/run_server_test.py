import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

binary = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(sys.argv[2]).resolve()
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

        def request(raw):
            with socket.create_connection(("127.0.0.1", 18765), timeout=3) as sock:
                sock.sendall(raw)
                chunks=[]
                while True:
                    part=sock.recv(8192)
                    if not part: break
                    chunks.append(part)
            data=b"".join(chunks)
            head, body=data.split(b"\r\n\r\n",1)
            lines=head.decode("latin1").split("\r\n")
            headers={}
            for line in lines[1:]:
                if ":" in line:
                    k,v=line.split(":",1); headers[k.lower()]=v.strip()
            return int(lines[0].split()[1]),headers,body

        with socket.create_connection(("127.0.0.1", 18765), timeout=3) as slow_client:
            slow_client.settimeout(3)
            slow_client.sendall(b"GET /slow HTTP/1.1\r\nHost:")
            slow_response = b""
            while True:
                part = slow_client.recv(1024)
                if not part:
                    break
                slow_response += part
            assert b" 408 " in slow_response.split(b"\r\n", 1)[0], slow_response

        code, headers, body = request(b"GET /hello/Kyxzz?x=1 HTTP/1.1\r\nHost: localhost\r\n\r\n")
        payload=json.loads(body.decode())
        assert code == 200 and payload["name"] == "Kyxzz" and payload["method"] == "GET"
        assert headers.get("server") == "Astra", headers

        code, headers, body = request(b"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\ntest")
        assert code == 201 and body == b"test" and headers.get("server") == "Astra"

        code, headers, body = request(b"PUT /replace/42 HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n")
        assert code == 202 and body == b"42" and headers.get("server") == "Astra"

        large_body = b"x" * 4096
        code, headers, body = request(b"POST /large HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4096\r\n\r\n" + large_body)
        assert code == 500 and body == b"Internal Server Error" and headers.get("server") == "Astra"
        proc.wait(timeout=5)
        if proc.returncode != 0:
            raise RuntimeError("Astra server returned non-zero status: " + (proc.stderr.read() or ""))
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=2)
            except subprocess.TimeoutExpired: proc.kill()
print("HTTP server integration: routes, bounded response, and absolute request deadline OK")
