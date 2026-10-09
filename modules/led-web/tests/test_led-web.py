"""Led Web module tests. Run through a harness: `python -m xewe test --module led-web`.

Hardware preconditions: provisioned board (first boot done), WiFi connected (web-interface needs it).
led-strip and led-modes are compiled in as dependencies (the harness lock lists FastLED).

What these tests cannot cover: the tools have no HTTP client and the CLI cannot issue a GET, so the
routes are checked on the host by parsing the route table from LedWeb.cpp and the URLs the page's
JavaScript requests. The page itself (sliders, polling, mode switching) needs a browser on the same
network and is not tested here.
"""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ID = "led_web"
NAME = "Led Web"      # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]
SRC = MODULE_DIR / "src" / "LedWeb"
CPP = SRC / "LedWeb.cpp"

ROUTE_RX = re.compile(r'server->on\("([^"]+)",\s*(HTTP_GET|HTTP_POST)')

# the page's contract: (method, path) -> what serves it
EXPECTED_ROUTES = {
    ("GET", "/led"),
    ("GET", "/led/index.css"),
    ("GET", "/led/index.js"),
    ("GET", "/led/api/state"),
    ("GET", "/led/api/modes"),
    ("GET", "/led/api/events"),
    ("POST", "/led/api/brightness"),
    ("POST", "/led/api/power"),
    ("POST", "/led/api/mode"),
    ("POST", "/led/api/param"),
    ("POST", "/led/api/color"),
    ("POST", "/led/api/reset_params"),
}


def _routes() -> set[tuple[str, str]]:
    return {(m.removeprefix("HTTP_"), p) for p, m in ROUTE_RX.findall(CPP.read_text())}


def test_compiles(compiled):
    # build of the harness firmware (web-interface, led-strip, led-modes, led-web) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Page:\s+http://[\d.]+/led", timeout=5)


def test_url_command(serial):
    # unverified: written without a board (2026-10-09 night run); regex copied from the `url` handler
    serial.command(f"${ID} url", expect=r"Led Web: http://\d+\.\d+\.\d+\.\d+/led", timeout=5)


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = CPP.read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp


@pytest.mark.host
def test_no_first_boot_prompt():
    # LM5: no first-boot questions; Module::begin prompts when requires_init_setup or can_be_disabled
    cpp = CPP.read_text()
    assert re.search(r"/\* requires_init_setup \*/ false", cpp)
    assert re.search(r"/\* can_be_disabled\s+\*/ false", cpp)


@pytest.mark.host
def test_routes_compile_table():
    # every route is registered once, with the expected method
    found = ROUTE_RX.findall(CPP.read_text())
    assert len(found) == len(set(found)), "duplicate route registration"
    assert _routes() == EXPECTED_ROUTES


@pytest.mark.host
def test_page_requests_only_registered_routes():
    js = (SRC / "index_js.h").read_text()
    html = (SRC / "index_html.h").read_text()
    routes = _routes()
    get_paths = {p for m, p in routes if m == "GET"}
    post_paths = {p for m, p in routes if m == "POST"}

    # assets referenced by the page
    for ref in re.findall(r'(?:href|src)="([^"]+)"', html):
        assert ref in get_paths, f"page references unregistered asset {ref}"

    api = re.search(r"const API = '([^']+)';", js)[1]
    # GETs: fetch(API + 'x') and new EventSource(API + 'x?...')
    gets = re.findall(r"fetch\(API \+ '(\w+)'", js) + re.findall(r"new EventSource\(API \+ '(\w+)", js)
    assert "events" in gets, "page does not subscribe to the event stream"
    for name in gets:
        assert api + name in get_paths, f"page GETs unregistered {api + name}"
    # POSTs: post('x', ...) and the ROUTES map used by sendCommand
    posted = set(re.findall(r"post\('(\w+)'", js))
    posted |= set(re.findall(r"\b\w+: '(\w+)'", re.search(r"const ROUTES = \{([^}]*)\}", js)[1]))
    assert posted, "no POST routes found in the page"
    for name in posted:
        assert api + name in post_paths, f"page POSTs unregistered {api + name}"

    # 2.3.x endpoints must be gone from the code (comments may still name them)
    code = re.sub(r"//.*", "", js.split('R"rawliteral(', 1)[1])
    for old in ("'/set", "`/set", "'/state'", "'/modes'", "'/name'", "WebSocket"):
        assert old not in code, f"2.3.x reference {old} left in index_js.h"


@pytest.mark.host
def test_post_handlers_call_module_setters():
    # each write route ends in the public setter its CLI command uses (no duplicated logic)
    cpp = CPP.read_text()
    calls = {
        "handle_brightness": "led_strip.set_brightness(",
        "handle_power": "led_strip.set_state(",
        "handle_mode": "led_modes.set_mode(",
        "handle_param": "led_modes.set_param(",
        "handle_color": "led_modes.set_color(",
        "handle_reset_params": "led_modes.reset_params(",
    }
    for handler, setter in calls.items():
        body = re.search(rf"void LedWeb::{handler}\(\) \{{(.*?)\n\}}", cpp, re.S)
        assert body, f"{handler} missing"
        assert setter in body[1], f"{handler} does not call {setter}"
        # origin = this (the listener skips its own echo), then the other streams are told
        assert re.search(re.escape(setter) + r"[^;]*\bthis\)", body[1]), f"{handler}: setter without origin"
        assert "push_to_others();" in body[1], f"{handler} does not push to the other event streams"


@pytest.mark.host
def test_assets_are_raw_string_headers():
    for name, var in (("index_html.h", "LED_WEB_INDEX_HTML"), ("index_css.h", "LED_WEB_INDEX_CSS"),
                      ("index_js.h", "LED_WEB_INDEX_JS")):
        text = (SRC / name).read_text()
        assert f"static const char {var}[] PROGMEM = R\"rawliteral(" in text, name
        assert text.rstrip().endswith(')rawliteral";'), name
        body = text.split('R"rawliteral(', 1)[1]
        assert ')rawliteral"' not in body[:-len(')rawliteral";') - 1], f"{name}: delimiter inside the asset"


@pytest.mark.host
def test_readme_lists_every_route():
    readme = (MODULE_DIR / "README.md").read_text()
    for method, path in _routes():
        assert re.search(rf"\|\s*`{method}`\s*\|\s*`{re.escape(path)}`", readme), f"README misses {method} {path}"


@pytest.mark.host
def test_poll_interval_matches():
    js = (SRC / "index_js.h").read_text()
    cpp = CPP.read_text()
    ms = int(re.search(r"const POLL_MS = (\d+);", js)[1])
    s = int(re.search(r"POLL_INTERVAL_S = (\d+);", cpp)[1])
    assert ms == s * 1000


@pytest.mark.host
def test_sse_contract():
    # LH1: /led/api/events is a text/event-stream held by a copy of the server's client (not setSSE,
    # which would park WebServer::handleClient on that socket), at most LED_WEB_SSE_CLIENTS (2),
    # one `state` event per pending change, a keep-alive comment every 15 s, led-web is a listener
    cpp = CPP.read_text()
    h = (SRC / "LedWeb.h").read_text()
    assert re.search(r"#define LED_WEB_SSE_CLIENTS 2\b", h)
    assert "public LedListener" in h
    assert "Content-Type: text/event-stream" in cpp
    assert "setSSE" not in re.sub(r"//.*", "", cpp)
    assert re.search(r"NetworkClient client = server->client\(\);", cpp)
    assert int(re.search(r"SSE_KEEPALIVE_MS = (\d+);", cpp)[1]) == 15000
    assert '": keep-alive\\n\\n"' in cpp
    assert '"event: state\\ndata: " + state_json() + "\\n\\n"' in cpp
    assert re.search(r"send_json\(503,", cpp), "a third stream must be refused"
    assert "led_strip.add_listener(this)" in cpp
    assert re.search(r"if \(origin == static_cast<const void\*>\(this\)\) return;", cpp)


@pytest.mark.host
def test_page_falls_back_to_polling():
    js = (SRC / "index_js.h").read_text()
    assert "new EventSource(" in js and "addEventListener('state'" in js
    err = re.search(r"addEventListener\('error', \(\) => \{(.*?)\}\);", js, re.S)
    assert err and "startPolling()" in err[1]
    assert re.search(r"setInterval\(refreshState, POLL_MS\)", js)
    assert "body.set('client', CLIENT_ID)" in js and "events?client=' + CLIENT_ID" in js


@pytest.mark.host
def test_js_syntax_node():
    node = shutil.which("node")
    if node is None:
        pytest.skip("node not installed")
    js = (SRC / "index_js.h").read_text().split('R"rawliteral(', 1)[1].rsplit(')rawliteral"', 1)[0]
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(js)
    try:
        run = subprocess.run([node, "--check", f.name], capture_output=True, text=True)
    finally:
        Path(f.name).unlink()
    assert run.returncode == 0, run.stderr
