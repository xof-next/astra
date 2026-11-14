#!/usr/bin/env python3
import os
import ssl
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

astra = str(Path(sys.argv[1]).resolve())
source = str(Path(sys.argv[2]).resolve())
openssl = sys.argv[3]

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass
    def do_GET(self):
        if self.path == "/downgrade":
            self.send_response(302)
            host = self.headers.get("Host", "127.0.0.1")
            self.send_header("Location", "http://" + host + "/plain")
            self.end_headers()
            return
        body = b"tls-ok"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

with tempfile.TemporaryDirectory(prefix="astra-tls-test-") as temp:
    root = Path(temp)
    ca_cert = str(root / "ca.pem")
    ca_key = str(root / "ca.key")
    server_cert = str(root / "server.pem")
    server_key = str(root / "server.key")
    server_csr = str(root / "server.csr")
    ca_config = root / "ca.cnf"
    server_config = root / "server.cnf"
    extensions_config = root / "server-ext.cnf"
    ca_config.write_text("""[req]
prompt = no
distinguished_name = dn
x509_extensions = v3_ca
[dn]
CN = Astra Local Test CA
[v3_ca]
basicConstraints = critical,CA:TRUE,pathlen:0
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
""", encoding="utf-8")
    server_config.write_text("""[req]
prompt = no
distinguished_name = dn
req_extensions = v3_req
[dn]
CN = 127.0.0.1
[v3_req]
subjectAltName = IP:127.0.0.1
""", encoding="utf-8")
    extensions_config.write_text("""[v3_server]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = IP:127.0.0.1
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
""", encoding="utf-8")
    subprocess.run([
        openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", ca_key, "-out", ca_cert, "-days", "2", "-sha256",
        "-config", str(ca_config),
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([
        openssl, "req", "-newkey", "rsa:2048", "-nodes",
        "-keyout", server_key, "-out", server_csr, "-sha256",
        "-config", str(server_config),
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([
        openssl, "x509", "-req", "-in", server_csr,
        "-CA", ca_cert, "-CAkey", ca_key, "-CAcreateserial",
        "-out", server_cert, "-days", "2", "-sha256",
        "-extfile", str(extensions_config), "-extensions", "v3_server",
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([
        openssl, "verify", "-CAfile", ca_cert, "-purpose", "sslserver", server_cert,
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(server_cert, server_key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    env = os.environ.copy()
    env["ASTRA_TLS_TEST_URL"] = f"https://127.0.0.1:{server.server_port}/"
    env["ASTRA_TLS_TEST_CA"] = ca_cert
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
print("local TLS certificate verification test passed")
