"""Web Interface module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module web-interface --unit-only`.

The catch-all's answers are compiled from src/WebInterface/WebInterfaceApi.h with g++
(tests/unit/test_web_interface_api.cpp); the hook placement is checked in the source text. The
comparison with the xewe-led-os 2.3.0 web interface reads commit 91bd344 with `git show`:
XEWE_LED_OS_REPO, else xewe-led-os next to the modules checkout; without it that comparison skips
with a warning and the values pinned here are checked against this module only.
"""

import hashlib
import os
import re
import shutil
import subprocess
import warnings
from pathlib import Path

import pytest

from xewe.testing import module_dir

ID = "web_interface"
NAME = "Web Interface"  # module.properties name == C++ name argument
MODULE_DIR = module_dir(
    __file__
)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/
SRC = MODULE_DIR / "src" / "WebInterface"
UNIT = MODULE_DIR / "tests" / "unit"

V230 = "91bd344"
V230_WI = "src/Modules/Software/SmartHome/WebInterface/WebInterface.cpp"

# the catch-all's headers, sent before every answer (xewe-led-os 2.3.0 applyCORS)
CORS = [
    ("Access-Control-Allow-Origin", "*"),
    ("Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE, PUT"),
    ("Access-Control-Allow-Headers", "Content-Type"),
]
# SHA-256 and size of the console page (the raw literal of INDEX_HTML)
INDEX_HTML = (3614, "a6b4f47b2652c12af18d4e8c8067b6d32f0b1b1e225a1ec1016c83da541f9043")


def _cpp() -> str:
    return (SRC / "WebInterface.cpp").read_text()


def _body(text: str, signature: str) -> str:
    start = text.index(signature)
    return text[start : text.index("\n}\n", start)]


def _v230_repo() -> Path | None:
    if shutil.which("git") is None:
        return None
    env = os.environ.get("XEWE_LED_OS_REPO")
    for repo in [*([Path(env)] if env else []), MODULE_DIR.parents[2] / "xewe-led-os"]:
        if (repo / ".git").exists() and (
            subprocess.run(
                ["git", "-C", str(repo), "cat-file", "-e", f"{V230}^{{commit}}"],
                capture_output=True,
                check=False,
            ).returncode
            == 0
        ):
            return repo
    return None


@pytest.mark.unit
def test_properties_match_source():
    props = dict(
        l.split("=", 1)
        for l in (MODULE_DIR / "module.properties").read_text().splitlines()
        if "=" in l
    )
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp


@pytest.mark.unit
def test_routes_and_catch_all_in_the_required_routine():
    # the required routine runs on every enabled boot, before init or regular
    cpp = _cpp()
    required = _body(cpp, "void WebInterface::begin_routines_required()")
    for path, handler in (
        ("/", "serve_root"),
        ("/console", "serve_main_page"),
        ("/cmd", "handle_command_request"),
    ):
        assert (
            f'http_server.on("{path}", HTTP_GET, std::bind(&WebInterface::{handler}, this));'
            in required
        ), path
    assert (
        "http_server.onNotFound(std::bind(&WebInterface::handle_not_found, this));"
        in required
    )
    assert "begin(" not in required
    assert cpp.count("http_server.on(") == 3
    assert cpp.count("onNotFound(") == 1


@pytest.mark.unit
def test_server_starts_in_the_common_routine():
    # the common routine also runs on the module's first-boot setup boot (after
    # `$web_interface reset`), so the server listens on every enabled boot
    cpp = _cpp()
    common = _body(cpp, "void WebInterface::begin_routines_common()")
    assert re.match(
        r"void WebInterface::begin_routines_common\(\) \{\s+http_server\.begin\(port\);\s+"
        r'os\.serial\.print\("Web Interface now available at:\\nhttp://" \+ '
        r"wifi\.get_local_ip\(\) \+\s+"
        r'\(port == 80 \? std::string\(\) : ":" \+ std::to_string\(port\)\)\);$',
        common,
    ), common
    assert cpp.count(".begin(") == 1
    for hook in ("begin_routines_regular", "begin_routines_init"):
        assert hook not in cpp, hook
    header = (SRC / "WebInterface.h").read_text()
    assert "void begin_routines_required() override;" in header
    assert "void begin_routines_common() override;" in header
    for hook in ("begin_routines_regular", "begin_routines_init"):
        assert hook not in header, hook


@pytest.mark.unit
def test_catch_all_sends_cors_then_the_answer():
    handler = _body(_cpp(), "void WebInterface::handle_not_found()")
    # as in 2.3.0: the catch-all answers unconditionally
    assert "is_disabled" not in handler
    # the whole body: CORS on every answer (not only the preflight), then the answer
    assert re.match(
        r"void WebInterface::handle_not_found\(\) \{\s+"
        r"for \(const auto& header : web_interface_api::CORS_HEADERS\)\s+"
        r"http_server\.sendHeader\(header\[0\], header\[1\]\);\s+"
        r"const web_interface_api::Answer a =\s+"
        r"web_interface_api::answer\(http_server\.method\(\) == HTTP_OPTIONS\);\s+"
        r"if \(a\.type == nullptr\)\s+http_server\.send\(a\.code\);\s+"
        r"else\s+http_server\.send\(a\.code, a\.type, a\.body\);$",
        handler,
    ), handler


@pytest.mark.unit
def test_api_values():
    api = (SRC / "WebInterfaceApi.h").read_text()
    pairs = re.findall(r'\{"([^"]+)", "([^"]+)"\}', api)
    assert pairs == CORS
    assert 'PREFLIGHT = {204, nullptr, ""};' in api
    assert 'NOT_FOUND = {404, "text/plain", "404: Not Found"};' in api
    assert re.search(r"return is_options \? PREFLIGHT : NOT_FOUND;", api)


@pytest.mark.unit
def test_api_header_stays_pure():
    # included by the host test: no includes at all
    assert not re.findall(r"#\s*include", (SRC / "WebInterfaceApi.h").read_text())


@pytest.mark.unit
def test_api_unit_gpp(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_web_interface_api"
    subprocess.run(
        [
            gpp,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-o",
            str(exe),
            str(UNIT / "test_web_interface_api.cpp"),
        ],
        check=True,
    )
    run = subprocess.run(
        [str(exe)], capture_output=True, text=True, timeout=30, check=False
    )
    assert run.returncode == 0, run.stdout + run.stderr
    assert int(re.search(r"PASSED: (\d+) check", run.stdout)[1]) >= 18


@pytest.mark.unit
def test_console_page_bytes():
    m = re.search(
        rb'R"rawliteral\((.*)\)rawliteral"',
        (SRC / "WebInterface.cpp").read_bytes(),
        re.DOTALL,
    )
    assert (len(m[1]), hashlib.sha256(m[1]).hexdigest()) == INDEX_HTML


@pytest.mark.unit
def test_catch_all_equals_v230():
    repo = _v230_repo()
    if repo is None:
        msg = (
            f"xewe-led-os with commit {V230} not found (XEWE_LED_OS_REPO): "
            "the cross-check of the pinned values against 2.3.0 did not run"
        )
        warnings.warn(msg, stacklevel=2)
        pytest.skip(msg)
    cpp = subprocess.run(
        ["git", "-C", str(repo), "show", f"{V230}:{V230_WI}"],
        capture_output=True,
        check=True,
        text=True,
    ).stdout
    at = cpp.index("httpServer.onNotFound(")
    handler = cpp[at : cpp.index("});", at)]
    assert re.search(
        r"applyCORS\(\);\s+if \(httpServer\.method\(\) == HTTP_OPTIONS\) \{\s+"
        r"httpServer\.send\(204\);\s+\} else \{\s+"
        r'httpServer\.send\(404, "text/plain", "404: Not Found"\);',
        handler,
    )
    assert "is_disabled" not in handler
    apply = _body(cpp, "void WebInterface::applyCORS()")
    assert re.findall(r'sendHeader\("([^"]+)", "([^"]+)"\)', apply) == CORS
    # 2.3.0: catch-all installed in the required routine, server begun in the common routine
    assert "httpServer.onNotFound(" in _body(
        cpp, "void WebInterface::begin_routines_required("
    )
    assert "httpServer.begin();" in _body(
        cpp, "void WebInterface::begin_routines_common("
    )
    assert "httpServer.begin" not in _body(
        cpp, "void WebInterface::begin_routines_regular("
    )
    assert cpp.count("onNotFound(") == 1


@pytest.mark.unit
def test_rows_and_server_take_the_config_defaults():
    cpp = (SRC / "WebInterface.cpp").read_text()
    assert '"port", 1, 65535, XEWE_WEB_INTERFACE_PORT, "HTTP port"' in cpp
    assert '"root", 31, XEWE_WEB_INTERFACE_ROOT,' in cpp
    header = (SRC / "WebInterface.h").read_text()
    assert "WebServer http_server{XEWE_WEB_INTERFACE_PORT};" in header
    assert '#include "Config.h"' in header
