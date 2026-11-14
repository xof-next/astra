import pathlib
import socket
import subprocess
import sys
import tempfile
import threading

binary = pathlib.Path(sys.argv[1]).resolve()
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    listener.settimeout(5)

    def serve():
        try:
            conn, _ = listener.accept()
            with conn:
                data = conn.recv(64)
                if data == b"ping":
                    conn.sendall(b"pong")
        except OSError:
            pass

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory(prefix="astra-tcp-") as temp:
        source = pathlib.Path(temp) / "tcp.astra"
        source.write_text(
            'import "std:tcp" as tcp;\n'
            f'pub async fn main() {{\n    let client = await tcp.connect("127.0.0.1", {port}, {{ timeoutMs: 2000 }});\n'
            '    assert(client.isOpen());\n'
            '    assert(await client.write("ping") == 4);\n'
            '    let response = await client.read(32);\n'
            '    assert(response == "pong");\n'
            '    assert(client.close());\n'
            '    let stillOpen = client.isOpen();\n    assert(stillOpen == false);\n'
            '    console.log("TCP client integration passed");\n}\n',
            encoding="utf-8",
        )
        result = subprocess.run([str(binary), str(source)], capture_output=True, text=True, timeout=8)
        if result.returncode != 0 or "TCP client integration passed" not in result.stdout:
            raise SystemExit(f"TCP integration failed:\n{result.stdout}\n{result.stderr}")
    thread.join(timeout=5)
print("TCP client integration: OK")
