#!/usr/bin/env python3
"""Integration tests for Tachyon's network stack.

Starts local HTTP, HTTPS and proxy servers on free ports, then runs the fetch
tool against them and checks what it prints:

  fetch_tests.py --certs DIR -- [wine] path/to/fetch

Everything after "--" is the fetch command (under Wine for Windows builds).
With --serve instead, it just runs the servers and prints their addresses,
for trying the browser by hand. Python standard library only.
"""

import argparse
import gzip
import http.server
import os
import socket
import socketserver
import ssl
import subprocess
import sys
import threading
import time
import zlib

TEXT = b"Tachyon: the modern web, sent back to 1995.\n" * 50


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "TachyonTestServer/1"

    def log_message(self, *args):
        pass

    def reply(self, status, body, headers=()):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path
        if path.startswith("http://"):  # absolute form, from a proxy
            path = "/" + path.split("/", 3)[3]
        if path == "/hello":
            self.reply(200, b"Hello from the test server\n", [("Content-Type", "text/html; charset=utf-8")])
        elif path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (b"chunk one, ", b"chunk two, ", b"the end\n"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        elif path == "/gzip":
            self.reply(200, gzip.compress(TEXT), [("Content-Encoding", "gzip")])
        elif path == "/deflate":
            self.reply(200, zlib.compress(TEXT), [("Content-Encoding", "deflate")])
        elif path == "/close":
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"delimited by closing the connection\n")
            self.close_connection = True
        elif path.startswith("/redirect/"):
            n = int(path.rsplit("/", 1)[1])
            self.reply(302, b"", [("Location", "/redirect/%d" % (n - 1) if n > 0 else "/hello")])
        elif path == "/loop":
            self.reply(302, b"", [("Location", "/loop")])
        elif path.startswith("/big/"):
            n = int(path.rsplit("/", 1)[1])
            self.reply(200, (b"0123456789abcdef" * (n // 16 + 1))[:n])
        elif path.startswith("/status/"):
            self.reply(int(path.rsplit("/", 1)[1]), b"status page\n")
        elif path == "/slow":
            time.sleep(15)
            self.reply(200, b"finally\n")
        elif path == "/headers":
            self.reply(200, "".join("%s: %s\n" % kv for kv in self.headers.items()).encode())
        else:
            self.reply(404, b"not found\n")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


class ProxyHandler(socketserver.BaseRequestHandler):
    """A tiny HTTP proxy: CONNECT tunnels, and absolute-form GET forwarding."""

    def handle(self):
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.request.recv(4096)
            if not chunk:
                return
            data += chunk
        head, rest = data.split(b"\r\n\r\n", 1)
        method, target, _ = head.split(b"\r\n")[0].split(b" ")
        if method == b"CONNECT":
            host, port = target.rsplit(b":", 1)
            upstream = socket.create_connection((host.decode(), int(port)))
            self.request.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
        else:
            hostport = target.split(b"/")[2]
            host, port = hostport.rsplit(b":", 1) if b":" in hostport else (hostport, b"80")
            upstream = socket.create_connection((host.decode(), int(port)))
            upstream.sendall(data)
        self.relay(self.request, upstream)

    @staticmethod
    def relay(a, b):
        def pump(src, dst):
            try:
                while True:
                    chunk = src.recv(65536)
                    if not chunk:
                        break
                    dst.sendall(chunk)
            except OSError:
                pass
            finally:
                try:
                    dst.shutdown(socket.SHUT_WR)
                except OSError:
                    pass
        t = threading.Thread(target=pump, args=(b, a), daemon=True)
        t.start()
        pump(a, b)
        t.join(30)


class ProxyServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True


def start(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1]


def https_server(certs, name, max_tls12=False):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(certs, name + ".pem"), os.path.join(certs, name + ".key"))
    if max_tls12:
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    server = Server(("127.0.0.1", 0), Handler)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    return start(server)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--certs", required=True)
    ap.add_argument("--serve", action="store_true", help="only run the servers")
    ap.add_argument("fetch", nargs="*")
    args = ap.parse_args()
    fetch = [a for a in args.fetch if a]
    ca = os.path.join(args.certs, "ca.pem")

    http_port = start(Server(("127.0.0.1", 0), Handler))
    rsa = https_server(args.certs, "rsa")
    ecdsa12 = https_server(args.certs, "ecdsa", max_tls12=True)
    expired = https_server(args.certs, "expired")
    future = https_server(args.certs, "future")
    wronghost = https_server(args.certs, "wronghost")
    selfsigned = https_server(args.certs, "selfsigned")
    proxy = start(ProxyServer(("127.0.0.1", 0), ProxyHandler))
    closed = free_port()

    def url(port, path, scheme="https"):
        return "%s://localhost:%d%s" % (scheme, port, path)

    if args.serve:
        print("http   %s" % url(http_port, "/", "http"))
        print("https  %s  (trust %s)" % (url(rsa, "/"), ca))
        print("proxy  127.0.0.1:%d" % proxy, flush=True)
        while True:
            time.sleep(3600)

    # (name, fetch arguments, expect success, strings stdout must contain,
    #  strings stderr must contain)
    cases = [
        ("plain http", [url(http_port, "/hello", "http")], True, ["Hello from the test server"], ["200 OK"]),
        ("https, RSA, TLS 1.3", ["--cacert", ca, url(rsa, "/hello")], True,
         ["Hello from the test server"], ["TLSv1.3"]),
        ("https, ECDSA, TLS 1.2", ["--cacert", ca, url(ecdsa12, "/hello")], True,
         ["Hello from the test server"], ["TLSv1.2", "ECDHE-ECDSA"]),
        ("headers sent", ["--cacert", ca, url(rsa, "/headers")], True,
         ["User-Agent: Mozilla/5.0 (Windows 95) Tachyon/", "Accept-Encoding: gzip, deflate"], []),
        ("chunked body", ["--cacert", ca, url(rsa, "/chunked")], True, ["chunk one, chunk two, the end"], []),
        ("gzip body", ["--cacert", ca, url(rsa, "/gzip")], True, [TEXT.decode()], ["%d bytes" % len(TEXT)]),
        ("deflate body", ["--cacert", ca, url(rsa, "/deflate")], True, [TEXT.decode()], []),
        ("body until close", [url(http_port, "/close", "http")], True, ["delimited by closing"], []),
        ("redirect chain", ["--cacert", ca, url(rsa, "/redirect/3")], True,
         ["Hello from the test server"], ["4 redirects", "/hello"]),
        ("redirect loop", ["--cacert", ca, url(rsa, "/loop")], False, [], ["too many redirects"]),
        ("big body", ["-q", "--cacert", ca, url(rsa, "/big/3000000")], True, [], ["3000000 bytes"]),
        ("404 is a response", ["--cacert", ca, url(rsa, "/status/404")], True, ["status page"], ["404"]),
        ("headers with -i", ["-i", url(http_port, "/hello", "http")], True,
         ["HTTP/1.1 200 OK", "Content-Type: text/html; charset=utf-8"], []),
        ("untrusted without the test CA", [url(rsa, "/hello")], False, [], ["isn't signed by an authority"]),
        ("self-signed", ["--cacert", ca, url(selfsigned, "/hello")], False, [], ["isn't signed by an authority"]),
        ("expired certificate", ["--cacert", ca, url(expired, "/hello")], False, [], ["expired"]),
        ("certificate not yet valid", ["--cacert", ca, url(future, "/hello")], False, [], ["isn't valid yet"]),
        ("wrong host", ["--cacert", ca, url(wronghost, "/hello")], False, [], ["different site"]),
        ("connection refused", [url(closed, "/hello", "http")], False, [], ["Couldn't connect"]),
        ("unknown host", ["http://nonexistent.invalid/"], False, [], ["server not found"]),
        ("https through a proxy", ["--proxy", "127.0.0.1:%d" % proxy, "--cacert", ca, url(rsa, "/hello")], True,
         ["Hello from the test server"], ["TLSv1.3"]),
        ("http through a proxy", ["--proxy", "127.0.0.1:%d" % proxy, url(http_port, "/hello", "http")], True,
         ["Hello from the test server"], []),
        ("unsupported scheme", ["ftp://localhost/"], False, [], ["can't load ftp"]),
    ]

    failures = 0
    for name, extra, ok, want_out, want_err in cases:
        try:
            p = subprocess.run(fetch + extra, capture_output=True, timeout=120)
        except subprocess.TimeoutExpired:
            print("FAIL %s: timed out" % name)
            failures += 1
            continue
        out = p.stdout.decode("utf-8", "replace")
        err = p.stderr.decode("utf-8", "replace")
        problems = []
        if (p.returncode == 0) != ok:
            problems.append("exit status %d" % p.returncode)
        problems += ["stdout lacks %r" % w[:60] for w in want_out if w not in out]
        problems += ["stderr lacks %r" % w for w in want_err if w not in err]
        if problems:
            failures += 1
            print("FAIL %s: %s\n     stderr: %s" % (name, "; ".join(problems), err.strip()[-300:]))
        else:
            print("ok   %s  [%s]" % (name, err.strip().splitlines()[-1][:110] if err.strip() else ""))
    print("%d of %d integration tests passed" % (len(cases) - failures, len(cases)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
