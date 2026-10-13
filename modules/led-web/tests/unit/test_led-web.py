# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
"""Led Web module unit tests: source checks, the C++ handler and frame tests (g++), the route table
against the LED page's HTTP surface, the served page bytes and the page script syntax. Developer
machine only, no board and no firmware build.
Run through a harness: `python -m xewe test --module led-web --unit-only`.

The C++ test needs XeWeCore 3.1.0's src/ (the hex helpers in Utils/Color.h; XEWE_CORE_SOURCE, else
the harness's build/libraries/XeWeCore)
and the led module's src/Led (the modules checkout, else the harness's build/modules/src/Led).
The comparison with the original firmware reads xewe-led-os at commit 91bd344 (v2.3.0) with
`git show`: XEWE_LED_OS_REPO, else xewe-led-os next to the modules checkout; without it only the
recorded SHA-256 values are checked.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

try:
    from xewe.testing import module_dir

    MODULE_DIR = module_dir(__file__)
except ImportError:  # standalone run
    MODULE_DIR = Path(__file__).resolve().parents[2]

ID = "led_web"
NAME = "Led Web"
SRC = MODULE_DIR / "src" / "LedWeb"
UNIT = MODULE_DIR / "tests" / "unit"

V230 = "91bd344"
V230_DIR = "src/Modules/Software/SmartHome/WebInterface"

# The page assets: file here, file in v2.3.0, SHA-256 of the served bytes (the raw literal).
ASSETS = {
    "index_html.h": (
        "templates/index_html.h",
        1854,
        "124f2b6fe9132b7d2d6d93fa946c4ab65aad5a7ad31eaca53e33e194f748db67",
    ),
    "index_css.h": (
        "static/index_css.h",
        3575,
        "3e11080f335fa43815565faffdc773756bebcef105f79929a1f0e44d1ed56a76",
    ),
    "index_js.h": (
        "static/index_js.h",
        18087,
        "04caa56b40b92a04ebe15aa5b5fb25a46bf4462456dac6236520d210881ac2c9",
    ),
}

# The LED page's HTTP surface of v2.3.0 without the schedule page:
# (path, method, query parameters in the order the handler checks them, content type)
JINJA_CSS = "/%7B%7B%20url_for('static',%20filename='index.css')%20%7D%7D"
JINJA_JS = "/%7B%7B%20url_for('static',%20filename='index.js')%20%7D%7D"
EXPECTED_ROUTES = [
    ("/", "GET", "", "Page"),
    ("/set", "GET", "color,brightness,state,mode_id,reset_params,param+val", "Set"),
    ("/state", "GET", "", "State"),
    ("/modes", "GET", "", "Modes"),
    ("/name", "GET", "", "Name"),
    ("/index.css", "GET", "", "Css"),
    ("/index.js", "GET", "", "Js"),
    ("/static/index.css", "GET", "", "Css"),
    ("/static/index.js", "GET", "", "Js"),
    (JINJA_CSS, "GET", "", "Css"),
    (JINJA_JS, "GET", "", "Js"),
]
# the content type of each kind (LedWebApi.h content_type, 2.3.0 send/send_P)
CONTENT_TYPES = {
    "Page": "text/html",
    "Css": "text/css",
    "Js": "application/javascript",
    "Set": "text/plain",
    "State": "text/plain",
    "Modes": "application/json",
    "Name": "text/plain",
}
SET_ARGS_ORDER = [
    "color",
    "brightness",
    "state",
    "mode_id",
    "reset_params",
    "param",
    "val",
]

LITERAL_RE = re.compile(rb'R"rawliteral\((.*)\)rawliteral"', re.S)

# ---------------------------------------------------------------------------------------------
# Locating sources
# ---------------------------------------------------------------------------------------------


def _harness_dirs() -> list[Path]:
    """The xewe projects this test runs in (or the dev project next to the modules checkout)."""
    out = [
        p
        for p in [Path(__file__).resolve(), *Path(__file__).resolve().parents]
        if (p / "build" / "libraries").is_dir()
    ]
    sibling = MODULE_DIR.parents[2] / "xewe-led-dev"
    if (sibling / "build" / "libraries").is_dir():
        out.append(sibling)
    return out


def _find_core_src() -> Path | None:
    env = os.environ.get("XEWE_CORE_SOURCE")
    candidates = [Path(env) / "src", Path(env)] if env else []
    candidates.append(MODULE_DIR.parents[2] / "xewe-os-core" / "src")
    candidates += [
        h / "build" / "libraries" / "XeWeCore" / "src" for h in _harness_dirs()
    ]
    for c in candidates:
        color = c / "XeWeCore" / "Utils" / "Color.h"
        if color.is_file() and "parse_hex_color" in color.read_text():
            return c
    return None


def _find_led_src() -> Path | None:
    candidates = [MODULE_DIR.parent / "led" / "src" / "Led"]
    candidates += [h / "build" / "modules" / "src" / "Led" for h in _harness_dirs()]
    for c in candidates:
        if (c / "LedListener.h").is_file():
            return c
    return None


def _v230_repo() -> Path | None:
    env = os.environ.get("XEWE_LED_OS_REPO")
    repo = Path(env) if env else MODULE_DIR.parents[2] / "xewe-led-os"
    if shutil.which("git") is None or not (repo / ".git").exists():
        return None
    ok = (
        subprocess.run(
            ["git", "-C", str(repo), "cat-file", "-e", f"{V230}^{{commit}}"],
            capture_output=True,
        ).returncode
        == 0
    )
    return repo if ok else None


def _git_show(repo: Path, path: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(repo), "show", f"{V230}:{path}"],
        capture_output=True,
        check=True,
    ).stdout


def _literal(data: bytes) -> bytes:
    m = LITERAL_RE.search(data)
    assert m, 'no R"rawliteral(...)rawliteral" in the asset'
    return m.group(1)


def _routes() -> list[tuple[str, str, str, str]]:
    text = (SRC / "LedWebApi.h").read_text()
    table = text[
        text.index("ROUTES[] = {") : text.index("};", text.index("ROUTES[] = {"))
    ]
    return re.findall(r'\{"([^"]*)", "([A-Z]+)", "([^"]*)", Content::(\w+)\}', table)


# ---------------------------------------------------------------------------------------------
# Module shape
# ---------------------------------------------------------------------------------------------


@pytest.mark.unit
def test_properties_match_source():
    props = dict(
        l.split("=", 1)
        for l in (MODULE_DIR / "module.properties").read_text().splitlines()
        if "=" in l
    )
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["name"] == NAME
    assert props["declare"] == "LedWeb led_web(os, led, web_interface);"
    assert re.split(r"[,\s]+", props["depends_modules"].strip()) == [
        "led",
        "web-interface",
    ]
    assert props["depends_libraries"] == "WebSockets"


@pytest.mark.unit
def test_websockets_pinned_to_a_commit():
    catalogue = (MODULE_DIR.parents[1] / "libraries.toml").read_text()
    m = re.search(
        r'^\[WebSockets\]\nrepo = "([^"]+)"\nref = "([^"]+)"$', catalogue, re.M
    )
    assert m, "WebSockets missing from libraries.toml"
    assert m[1] == "https://github.com/xewe-labs/xewe-led-library-websockets"
    assert re.fullmatch(r"[0-9a-f]{40}", m[2]), "the fork is pinned to a commit SHA"


@pytest.mark.unit
def test_every_source_file_is_gpl():
    files = [
        p
        for p in MODULE_DIR.rglob("*")
        if p.suffix in {".h", ".cpp", ".py"} and "__pycache__" not in p.parts
    ]
    assert files
    for p in files:
        head = p.read_text().splitlines()[:3]
        assert "SPDX-License-Identifier: GPL-3.0-only" in "\n".join(head), p


@pytest.mark.unit
def test_comments_describe_code_only():
    # no process words in the sources: decision or finding ids, port markers, review status,
    # history of where the code came from
    for p in SRC.rglob("*"):
        if p.suffix in {".h", ".cpp"} and p.parent.name != "pages":
            text = p.read_text()
            for pattern in (
                r"DECISION",
                r"PORT-(FIX|QUIRK)",
                r"\bD-\d+\b",
                r"CONTRACT",
                r"\bTODO\b",
                r"\boriginal\b",
                r"\bprototype\b",
                r"\bexported from\b",
                r"\bFlask\b",
                r"\b2\.3\.\d\b",
            ):
                assert not re.search(pattern, text, re.I), f"{p.name}: {pattern}"
    # the hex parser comes from Color.h, the host-includable home of the colour helpers
    api = (SRC / "LedWebApi.h").read_text()
    assert not re.search(r"#include [<\"][^>\"]*Utils/String\.h[>\"]", api)


@pytest.mark.unit
def test_config_matches_the_page_script():
    config = (SRC / "Config.h").read_text()
    port = int(re.search(r"#define XEWE_MODULE_LED_WEB_WS_PORT (\d+)", config)[1])
    beat = int(re.search(r"#define XEWE_MODULE_LED_WEB_HEARTBEAT_MS (\d+)", config)[1])
    js = _literal((SRC / "pages" / "index_js.h").read_bytes()).decode()
    assert f"ws://${{location.hostname}}:{port}/" in js
    timeout = int(re.search(r"HEARTBEAT_TIMEOUT_MS = (\d+);", js)[1])
    assert (port, beat) == (81, 1000)
    assert 2 * beat <= timeout  # one lost heartbeat does not show Offline


# ---------------------------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------------------------


@pytest.mark.unit
def test_route_table_matches_the_led_page_surface():
    assert _routes() == EXPECTED_ROUTES


@pytest.mark.unit
def test_set_checks_arguments_in_order():
    api = (SRC / "LedWebApi.h").read_text()
    body = api[api.index("Response handle_set(") : api.index("// GET /state")]
    assert re.findall(r'req\.has\("(\w+)"\)', body) == SET_ARGS_ORDER
    assert "xewe::color::parse_hex_color" in body


@pytest.mark.unit
def test_routes_are_registered_on_the_web_interface_server():
    cpp = (SRC / "LedWeb.cpp").read_text()
    assert "web_interface.get_server()" in cpp
    assert re.search(
        r"for \(const led_web_api::Route& route : led_web_api::ROUTES\)", cpp
    )
    assert re.search(r"web_interface\.set_root\(", cpp)
    assert re.search(r"server\.on\(route\.path, HTTP_GET,", cpp)
    assert "onNotFound" not in cpp  # the web interface owns the catch-all
    assert "led.listeners.add(this)" in cpp
    assert re.search(r"handle_set\(ServerRequest\{server\}, led, this\)", cpp)
    serve = cpp[
        cpp.index("void LedWeb::serve(") : cpp.index("void LedWeb::send_response(")
    ]
    assert serve.index("apply_cors();") < serve.index("switch (route.content)")


@pytest.mark.unit
def test_websocket_lifecycle_matches_v230():
    # the server glue forwards to PageSessions (LedWebApi.h, host-tested in test_led_web_api.cpp):
    # disconnect -> count down, connect -> count up and F to every page, heartbeat while a page is
    # connected, reset closes every page before the core reset
    cpp = (SRC / "LedWeb.cpp").read_text()

    def body(signature: str) -> str:
        start = cpp.index(signature)
        return cpp[start : cpp.index("\n}\n", start)]

    event = body("void LedWeb::on_web_socket_event(")
    assert re.search(r"if \(is_disabled\(\)\)\s+return;", event)
    assert re.search(
        r"case WStype_DISCONNECTED:\s+page_disconnected\(\);\s+break;", event
    )
    assert re.search(r"case WStype_CONNECTED:\s+page_connected\(led\);\s+break;", event)
    loop = body("void LedWeb::loop(")
    assert re.search(
        r"web_socket\.loop\(\);\s+if \(heartbeat_due\(millis\(\), "
        r"XEWE_MODULE_LED_WEB_HEARTBEAT_MS\)\) \{\s+"
        r"broadcast\(led_web_api::FRAME_HEARTBEAT, 1\);\s+heartbeat_sent\(millis\(\)\);",
        loop,
    )
    reset = body("void LedWeb::reset(")
    assert reset.index("close_pages();") < reset.index("Module::reset(")
    disconnect = body("void LedWeb::disconnect_all(")
    assert re.search(r"if \(started\)\s+web_socket\.disconnect\(\);", disconnect)
    send = body("void LedWeb::send_frame(")
    assert "broadcast(frame.data(), frame.size());" in send
    broadcast = body("void LedWeb::broadcast(")
    assert re.search(r"if \(is_disabled\(\) \|\| !started\)\s+return;", broadcast)
    assert re.search(
        r"if \(length > 0\)\s+web_socket\.broadcastTXT\(payload, length\);", broadcast
    )
    status = body("std::string LedWeb::status(")
    assert re.search(r'web_settings\.find\("port"\)', status)
    assert re.search(
        r"led_web_api::page_url\(\s*WiFi\.localIP\(\)\.toString\(\)\.c_str\(\),\s*"
        r"port_row \? web_settings\.value\(\*port_row\) : std::string\(\)\)",
        status,
    )


@pytest.mark.unit
def test_routes_equal_v230_registrations():
    repo = _v230_repo()
    if repo is None:
        pytest.skip("xewe-led-os with commit 91bd344 not found (XEWE_LED_OS_REPO)")
    cpp = _git_show(repo, f"{V230_DIR}/WebInterface.cpp").decode()
    registered = re.findall(r'httpServer\.on\("([^"]+)", HTTP_(\w+)', cpp)
    led_page = [
        (p, m) for p, m in registered if "schedule" not in p and "style.css" not in p
    ]
    assert sorted(led_page) == sorted((p, m) for p, m, _, _ in EXPECTED_ROUTES)
    handler = cpp[
        cpp.index("void WebInterface::handleSetRequest()") : cpp.index(
            "void WebInterface::handleGetStateRequest()"
        )
    ]
    assert re.findall(r'hasArg\("(\w+)"\)', handler) == SET_ARGS_ORDER
    assert 'httpServer.send(200, "text/plain", "OK");' in handler
    state = cpp[cpp.index("void WebInterface::handleGetStateRequest()") :]
    assert '"F%02X%02X%02X,%u,%u,%u"' in state

    # per route: the content type it answers with and the query arguments it reads
    def handler_body(path: str) -> str:
        at = cpp.index(f'httpServer.on("{path}", HTTP_GET, ')
        rest = cpp[at:]
        bound = re.match(
            r'httpServer\.on\("[^"]+", HTTP_GET, std::bind\(&WebInterface::(\w+)', rest
        )
        if bound is None:  # a lambda: its body up to the closing "});"
            return rest[: rest.index("});")]
        start = cpp.index(f"void WebInterface::{bound[1]}()")
        return cpp[start : cpp.index("\n}\n", start)]

    for path, _, params, kind in EXPECTED_ROUTES:
        body = handler_body(path)
        sent = re.search(r'httpServer\.send(?:_P)?\(200, "([^"]+)"', body)
        assert sent and sent[1] == CONTENT_TYPES[kind], (path, sent and sent[1])
        assert "applyCORS();" in body, path
        args = ",".join(re.findall(r'hasArg\("(\w+)"\)', body))
        assert args == params.replace("+", ","), (path, args)


# ---------------------------------------------------------------------------------------------
# C++ handlers and frames
# ---------------------------------------------------------------------------------------------


@pytest.mark.unit
def test_api_unit_gpp(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    core = _find_core_src()
    led = _find_led_src()
    if core is None or led is None:
        pytest.skip(
            "XeWeCore >=3.1.0 src/ (hex helpers in Utils/Color.h) or the led module's src/Led not found"
        )
    exe = tmp_path / "test_led_web_api"
    src = UNIT / "test_led_web_api.cpp"
    subprocess.run(
        [
            gpp,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(core),
            "-I",
            str(led),
            "-o",
            str(exe),
            str(src),
        ],
        check=True,
    )
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert int(re.search(r"PASSED: (\d+) check", run.stdout)[1]) >= 200


@pytest.mark.unit
def test_api_header_stays_pure():
    allowed = {
        "array",
        "cstddef",
        "cstdint",
        "cstdio",
        "string",
        "string_view",
        "XeWeCore/Utils/Color.h",
        "../Led/LedListener.h",
    }
    text = (SRC / "LedWebApi.h").read_text()
    assert set(re.findall(r'#include [<"]([^>"]+)[>"]', text)) <= allowed


# ---------------------------------------------------------------------------------------------
# Page assets
# ---------------------------------------------------------------------------------------------


@pytest.mark.unit
def test_assets_serve_the_v230_bytes():
    for name, (_, size, sha) in ASSETS.items():
        served = _literal((SRC / "pages" / name).read_bytes())
        assert len(served) == size, name
        assert hashlib.sha256(served).hexdigest() == sha, name


@pytest.mark.unit
def test_assets_equal_v230_files():
    repo = _v230_repo()
    if repo is None:
        pytest.skip("xewe-led-os with commit 91bd344 not found (XEWE_LED_OS_REPO)")
    for name, (old, _, sha) in ASSETS.items():
        before = _git_show(repo, f"{V230_DIR}/{old}")
        after = (SRC / "pages" / name).read_bytes()
        assert hashlib.sha256(_literal(before)).hexdigest() == sha, name
        assert _literal(before) == _literal(after), name
        # outside the literal the header files differ in the path line and in the double blank
        # line before the declaration, which the formatter collapses to one
        b, a = before.split(b"\n"), after.split(b"\n")
        path = f"// xewe-os-modules/modules/led-web/src/LedWeb/pages/{name}".encode()
        assert b[6:8] == [b"", b""], name
        assert a == b[:2] + [path] + b[3:7] + b[8:], name


@pytest.mark.unit
def test_page_script_parses(tmp_path):
    node = shutil.which("node")
    if node is None:
        pytest.skip("node not installed")
    js = tmp_path / "index.js"
    js.write_bytes(_literal((SRC / "pages" / "index_js.h").read_bytes()))
    run = subprocess.run(
        [node, "--check", str(js)], capture_output=True, text=True, timeout=60
    )
    assert run.returncode == 0, run.stderr


@pytest.mark.unit
def test_page_links_resolve_to_routes():
    html = _literal((SRC / "pages" / "index_html.h").read_bytes()).decode()
    paths = {p for p, _, _, _ in EXPECTED_ROUTES}
    assert 'href="/index.css"' in html and "/index.css" in paths
    assert (
        'src="static/index.js"' in html and "/static/index.js" in paths
    )  # relative to /
    js = _literal((SRC / "pages" / "index_js.h").read_bytes()).decode()
    for path in set(re.findall(r"fetch\(['`](/\w+)", js)):
        assert path in paths, path
