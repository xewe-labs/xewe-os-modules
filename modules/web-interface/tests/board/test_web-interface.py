"""Web Interface module tests. Run through a harness: `python -m xewe test --module web-interface`.

Hardware preconditions: provisioned board (first boot done, every first-boot prompt answered),
WiFi connected (Wifi is a requirement). The HTTP tests also need this machine to reach the board on
the network: they run only with XEWE_TEST_WEB_INTERFACE_HTTP=1 and read the address from
`$wifi status` and `$web_interface get port`. The catch-all test skips when an enabled Alexa module
is listed in `$system status` (a module that begins later and calls onNotFound replaces the
catch-all). The last two tests restart the board: `test_restart_keeps_serving` (with the HTTP
variable) and `test_reset_serves_on_the_next_boot`, which runs only with
XEWE_TEST_WEB_INTERFACE_RESET=1 (and the HTTP variable), resets the module's settings, checks that
the routes answer on the very next boot (and, when present, led-web's `/state`; the catch-all too
when Alexa is not enabled) and restores `port` and `root` afterwards. The module cannot be disabled
and its reset asks no question.
"""

import http.client
import os
import re

import pytest

from xewe.board.serialio import BOOT_READY, wait_for_banner

ID = "web_interface"
NAME = "Web Interface"  # module.properties name == C++ name argument
# the catch-all's headers, sent before every answer
CORS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "POST, GET, OPTIONS, DELETE, PUT",
    "Access-Control-Allow-Headers": "Content-Type",
}
# printed once the server listens, on every enabled boot
AVAILABLE = r"Web Interface now available at:"
# `$system status` table row of a module: name, enabled
ROW = r"^\|\s*{}\s*\|\s*(Yes|No)\s*\|"


def _needs_network():
    if os.environ.get("XEWE_TEST_WEB_INTERFACE_HTTP") != "1":
        pytest.skip(
            "needs network access to the board (XEWE_TEST_WEB_INTERFACE_HTTP=1)"
        )


def _ip(serial) -> str:
    serial.send("$wifi status")
    text = "\n".join(serial.collect(limit=10))
    m = re.search(r"^Local ip: ([\d.]+)$", text, re.MULTILINE)
    if m is None or m[1] == "0.0.0.0":
        pytest.skip("board has no IP address (WiFi not connected)")
    return m[1]


def _get(serial, key: str) -> str:
    return serial.command(f"${ID} get {key}", expect=rf"^{key}=(.*)$", timeout=5)[1]


def _request(ip: str, port: int, path: str, method: str = "GET"):
    """Status, headers and raw body; redirects are not followed."""
    conn = http.client.HTTPConnection(ip, port, timeout=5)
    try:
        conn.request(method, path)
        r = conn.getresponse()
        return r.status, dict(r.getheaders()), r.read()
    finally:
        conn.close()


def _assert_serves(ip: str, port: int) -> None:
    """`/` (the console, a redirect or a project page), `/console` and `/cmd` answer."""
    code, _, _ = _request(ip, port, "/")
    assert code in (200, 302), code
    code, headers, body = _request(ip, port, "/console")
    assert code == 200 and headers.get("Content-Type", "").startswith("text/html")
    assert b"<title>XeWe OS Web Interface</title>" in body
    # no `c` argument: nothing runs
    assert _request(ip, port, "/cmd")[0::2] == (400, b"Empty Command")


def _rows(serial) -> dict[str, str]:
    """`$system status` enabled column (Yes or No) of the Alexa and Led Web rows, when listed."""
    serial.send("$system status")
    text = "\n".join(serial.collect(limit=10))
    rows = {}
    for name in ("Alexa", "Led Web"):
        m = re.search(ROW.format(re.escape(name)), text, re.MULTILINE)
        if m:
            rows[name] = m[1]
    return rows


def _assert_catch_all(ip: str, port: int) -> None:
    """An unmatched GET gets the CORS 404; a preflight to any path gets the CORS 204."""
    code, headers, body = _request(ip, port, "/nope")
    assert (code, body) == (404, b"404: Not Found")
    assert headers.get("Content-Type", "").startswith("text/plain")
    for name, value in CORS.items():
        assert headers.get(name) == value, name
    # a preflight to any path, routed or not: the routes answer GET only
    for path in ("/nope", "/", "/console", "/cmd"):
        code, headers, body = _request(ip, port, path, method="OPTIONS")
        assert (code, body) == (204, b""), path
        assert headers.get("Content-Type", "").startswith("text/html"), path
        for name, value in CORS.items():
            assert headers.get(name) == value, (path, name)


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


def test_status_reports_server(serial):
    # the module has no commands of its own; its status adds uptime and memory lines
    serial.command(f"${ID} status", expect=r"Uptime: \d+d \d\d:\d\d:\d\d", timeout=5)
    serial.expect(r"Memory usage: [\d.]+% \(\d+ / \d+ bytes\)", timeout=5)


def test_settings_table(serial):
    # settings table: port (restart) + root rows
    serial.command("$web_interface get port", expect=r"port=\d+", timeout=5)
    serial.command(
        "$web_interface schema",
        expect=r'\{"end":"web_interface","count":2\}',
        timeout=5,
    )


def test_http_routes(serial):
    _needs_network()
    _assert_serves(_ip(serial), int(_get(serial, "port")))


def test_http_catch_all(serial):
    _needs_network()
    if _rows(serial).get("Alexa") == "Yes":
        pytest.skip("Alexa is enabled: its not-found handler replaces the catch-all")
    _assert_catch_all(_ip(serial), int(_get(serial, "port")))


def test_restart_keeps_serving(serial):
    # restarts the board; the server listens again on the next boot
    _needs_network()
    port = int(_get(serial, "port"))
    serial.send("$system restart")
    serial.expect(r"Rebooting", timeout=10)
    wait_for_banner(serial, AVAILABLE, 90, reset=False)
    serial.expect(BOOT_READY, timeout=60)
    serial.collect(silence=2.0, limit=40)
    _assert_serves(_ip(serial), port)


def test_reset_serves_on_the_next_boot(serial):
    # LAST in this file: `$web_interface reset` wipes the module's settings and restarts; the next
    # boot is the module's first-boot setup boot, and the server answers on it without another
    # restart: the routes, the catch-all (Alexa not enabled) and led-web's `/state` (when present).
    # Then `port` and `root` are restored.
    _needs_network()
    if os.environ.get("XEWE_TEST_WEB_INTERFACE_RESET") != "1":
        pytest.skip(
            "restarts and resets the module: set XEWE_TEST_WEB_INTERFACE_RESET=1 to run it"
        )
    port, root = _get(serial, "port"), _get(serial, "root")
    serial.command(f"${ID} reset", expect=rf"{NAME} module reset", timeout=10)
    serial.expect(r"Rebooting", timeout=10)
    wait_for_banner(serial, AVAILABLE, 90, reset=False)
    serial.expect(BOOT_READY, timeout=60)
    serial.collect(silence=2.0, limit=40)
    try:
        assert _get(serial, "port") == "80"
        ip = _ip(serial)
        _assert_serves(ip, 80)
        rows = _rows(serial)
        if rows.get("Alexa") != "Yes":
            _assert_catch_all(ip, 80)
        if rows.get("Led Web") == "Yes":
            # a led-web route on this server; regex from led-web's board test (test_led-web.py)
            code, _, body = _request(ip, 80, "/state")
            assert code == 200, code
            assert re.fullmatch(rb"F[0-9A-F]{6},\d+,[01],\d+", body), body
    finally:
        if root:
            serial.command(
                f"${ID} set root {root}", expect=rf"root={re.escape(root)}", timeout=5
            )
        if port != "80":
            serial.command(f"${ID} set port {port}", expect=rf"port={port}", timeout=5)
            serial.send("$system restart")
            _wait_reboot(serial)
