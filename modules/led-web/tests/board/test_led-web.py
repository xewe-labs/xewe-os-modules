# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
"""Led Web module tests. Run through a harness: `python -m xewe test --module led-web`.

Hardware preconditions: provisioned board (first boot done, the "enable Led Web module?" prompt
answered yes, the led wizard completed), WiFi connected. The HTTP and WebSocket tests also need
this machine to reach the board on the network: they run only with XEWE_TEST_LED_WEB_HTTP=1 and
read the board's address from `$led_web status`. Each test restores what it changes. The last
test disables the module (answering the core's confirmation), restarts, and enables it again.
"""

import base64
import hashlib
import os
import re
import socket
import time
import urllib.error
import urllib.request

import pytest

from xewe.board.serialio import BOOT_READY, wait_for_banner

ID = "led_web"
NAME = "Led Web"  # module.properties name == C++ name argument
# the input line of a firmware prompt; answer only after it
INPUT = r"^(?:\(y/n\) )?> $"
# `$led_web status`: the page address, with :<port> when the web interface's port is not 80
PAGE = r"^Page: http://([\d.]+(?::\d+)?)/$"

# the CORS headers on every answer of the page's routes (xewe-led-os 2.3.0 values)
CORS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "POST, GET, OPTIONS, DELETE, PUT",
    "Access-Control-Allow-Headers": "Content-Type",
}
# SHA-256 of the served page script (the raw literal in src/LedWeb/pages/index_js.h)
INDEX_JS_SHA256 = "04caa56b40b92a04ebe15aa5b5fb25a46bf4462456dac6236520d210881ac2c9"


def _needs_network():
    if os.environ.get("XEWE_TEST_LED_WEB_HTTP") != "1":
        pytest.skip("needs network access to the board (XEWE_TEST_LED_WEB_HTTP=1)")


def _host(serial) -> str:
    """The board's `<ip>[:<port>]` from `$led_web status`."""
    m = serial.command(f"${ID} status", expect=PAGE, timeout=5)
    if m[1].split(":")[0] == "0.0.0.0":
        pytest.skip("board has no IP address")
    return m[1]


def _fetch(host: str, path: str) -> tuple[int, dict[str, str], bytes]:
    """Status, headers and raw body of GET http://<host><path>."""
    try:
        with urllib.request.urlopen(f"http://{host}{path}", timeout=5) as r:
            return r.status, dict(r.headers.items()), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers.items()), e.read()


def _get(host: str, path: str) -> tuple[int, str, str]:
    code, headers, body = _fetch(host, path)
    return code, headers.get("Content-Type", ""), body.decode()


class _WebSocket:
    """Minimal client for the board's text frames (server frames are never masked)."""

    def __init__(self, host: str, port: int = 81):
        self.sock = socket.create_connection((host, port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            (
                f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
                f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n"
            ).encode()
        )
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.sock.recv(1024)
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        assert b" 101 " in head.split(b"\r\n")[0], head

    def _read(self, n: int, deadline: float) -> bytes:
        while len(self.buf) < n:
            self.sock.settimeout(max(0.05, deadline - time.monotonic()))
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("WebSocket closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def expect(self, pattern: str, timeout: float = 5) -> re.Match:
        """Reads text frames until one matches `pattern` (a full match)."""
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            try:
                b0, b1 = self._read(2, deadline)
                n = b1 & 0x7F
                if n == 126:
                    n = int.from_bytes(self._read(2, deadline), "big")
                elif n == 127:
                    n = int.from_bytes(self._read(8, deadline), "big")
                payload = self._read(n, deadline)
            except socket.timeout:
                break
            if b0 & 0x0F == 0x1:
                text = payload.decode()
                seen.append(text)
                m = re.fullmatch(pattern, text)
                if m:
                    return m
        raise AssertionError(f"no frame matching {pattern!r}; got {seen}")

    def next_frames(self, count: int, timeout: float = 5) -> list[str]:
        """The next `count` text frames, heartbeats left out."""
        deadline = time.monotonic() + timeout
        out = []
        while len(out) < count:
            m = self.expect(
                r"[^H].*|H.+", timeout=max(0.1, deadline - time.monotonic())
            )
            out.append(m[0])
        return out

    def close(self):
        self.sock.close()


def _wait_reboot(serial) -> None:
    serial.expect(r"Rebooting", timeout=10)
    wait_for_banner(serial, BOOT_READY, 90, reset=False)
    serial.collect(silence=2.0, limit=40)


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(
        f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5
    )


def test_status_reports_page_and_websocket(serial):
    serial.command(f"${ID} status", expect=PAGE, timeout=5)
    serial.expect(r"^WebSocket Port: 81$", timeout=5)
    serial.expect(r"^WebSocket Clients: \d+$", timeout=5)


def test_http_routes(serial):
    _needs_network()
    host = _host(serial)
    code, ctype, body = _get(host, "/")
    assert code == 200 and ctype.startswith("text/html")
    assert "<title>XeWe LED</title>" in body
    for path in ("/index.css", "/static/index.css"):
        assert _get(host, path)[:2] == (200, "text/css")
    for path in ("/index.js", "/static/index.js"):
        assert _get(host, path)[:2] == (200, "application/javascript")
    code, ctype, state = _get(host, "/state")
    assert code == 200 and re.fullmatch(r"F[0-9A-F]{6},\d+,[01],\d+", state), state
    code, ctype, body = _get(host, "/modes")
    assert code == 200 and ctype.startswith("application/json")
    assert body.startswith('[{"id":0,')
    assert _get(host, "/name")[0] == 200
    assert _get(host, "/set") == (200, "text/plain", "OK")
    # a malformed colour is rejected and changes nothing
    assert _get(host, "/set?color=zz0000") == (400, "text/plain", "Invalid color")
    assert _get(host, "/state")[2] == state


def test_http_cors_and_served_bytes(serial):
    _needs_network()
    host = _host(serial)
    for path in ("/", "/state"):
        code, headers, _ = _fetch(host, path)
        assert code == 200, path
        for name, value in CORS.items():
            assert headers.get(name) == value, (path, name, headers.get(name))
    code, _, body = _fetch(host, "/index.js")
    assert code == 200 and hashlib.sha256(body).hexdigest() == INDEX_JS_SHA256
    # unknown paths are the web interface server's (no catch-all of this module)
    assert _get(host, "/nope")[0] == 404


def test_set_is_echoed_over_websocket(serial):
    _needs_network()
    host = _host(serial)
    _, _, state = _get(host, "/state")
    color, brightness, _, mode = state[1:].split(",")
    brightness = int(brightness)
    other = 77 if brightness != 77 else 78
    ws = _WebSocket(host.split(":")[0])
    second = None
    try:
        ws.expect(r"F[0-9A-F]{6},\d+,[01],\d+")  # the current values on connect
        assert _get(host, f"/set?brightness={other}") == (200, "text/plain", "OK")
        ws.expect(rf"B{other}")  # the page that sent it gets the echo
        # a CLI change reaches the page too
        serial.send(f"$led set_brightness {brightness}")
        ws.expect(rf"B{brightness}")
        ws.expect(r"H", timeout=3)
        if mode == "0":
            # a mode change: the mode's colour first, then the mode
            assert _get(host, "/set?mode_id=0") == (200, "text/plain", "OK")
            frames = ws.next_frames(2)
            assert re.fullmatch(r"C[0-9A-F]{6}", frames[0]) and frames[1] == "M0", (
                frames
            )
        # a second page connecting sends the current values to every page
        second = _WebSocket(host.split(":")[0])
        second.expect(r"F[0-9A-F]{6},\d+,[01],\d+")
        ws.expect(r"F[0-9A-F]{6},\d+,[01],\d+")
    finally:
        ws.close()
        if second is not None:
            second.close()
        _get(host, f"/set?color={color}")
        _get(host, f"/set?brightness={brightness}")


def test_disable_enable_across_restart(serial):
    # core Module::disable asks "[WARNING] Disabling <name> / Will reset it / OK?" (get_yn, 2 x 15 s,
    # default no): answer y. The namespace is reset and the board restarts.
    serial.command(f"${ID} disable", expect=r"OK\?", timeout=5)
    serial.expect(INPUT, timeout=5)
    serial.send("y")
    serial.expect(rf"{NAME} module disabled", timeout=10)
    _wait_reboot(serial)
    try:
        serial.command(f"${ID} status", expect=rf"{NAME} module disabled", timeout=5)
    finally:
        serial.command(f"${ID} enable", expect=rf"{NAME} module enabled", timeout=10)
        _wait_reboot(serial)
    serial.command(f"${ID} status", expect=rf"{NAME} module enabled", timeout=5)
