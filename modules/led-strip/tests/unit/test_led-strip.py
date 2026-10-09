"""Led (led-strip) module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module led-strip --unit-only`.
"""
import re
import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "led"
NAME = "Led"          # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/
SRC = MODULE_DIR / "src" / "LedStrip"


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (SRC / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert "FastLED" in props["depends_libraries"]


@pytest.mark.unit
def test_chipset_table_matches_add_leds():
    table = re.findall(r"\{\s*(\d+),\s*\"(\w+)\",\s*(true|false)\s*\}", (SRC / "Chipsets.h").read_text())
    ids = [int(i) for i, _, _ in table]
    assert ids and len(ids) == len(set(ids)) and all(0 <= i <= 45 for i in ids)   # 2.3.x id range
    cases = re.findall(r"case (\d+):\s+c = &FastLED\.addLeds<(\w+),", (SRC / "LedStrip.cpp").read_text())
    assert sorted((int(i), n) for i, n in cases) == sorted((int(i), n) for i, n, _ in table)
    for _, name, clocked in table:   # a clocked chip gets the clock pin template argument
        line = next(l for l in (SRC / "LedStrip.cpp").read_text().splitlines() if f"addLeds<{name}," in l)
        assert ("LED_PIN_CLOCK" in line) == (clocked == "true"), name


@pytest.mark.unit
def test_listener_fanout_unit_gpp(tmp_path):
    # LedListener.h: fixed 4-slot set, no duplicates, registration order, origin echo suppression,
    # default no-op bodies for the led-modes callbacks
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_listeners"
    src = MODULE_DIR / "tests" / "unit" / "test_listeners.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 28, f"only {summary[1]} checks ran (28 when written); was the test emptied?"


@pytest.mark.unit
def test_setters_notify_listeners():
    # listeners are told after the change, outside the render mutex, only when the value changed,
    # and never from the render task
    cpp = (SRC / "LedStrip.cpp").read_text()
    for name, cb in (("set_brightness", "on_brightness"), ("set_state", "on_state")):
        body = re.search(rf"void LedStrip::{name}\([^)]*const void\* origin\) \{{(.*?)\n\}}", cpp, re.S)
        assert body, f"{name} has no origin parameter"
        text = body[1]
        assert f"l.{cb}(" in text, f"{name} does not call {cb}"
        assert text.rfind("LockGuard") < text.find("listeners.notify"), f"{name} notifies under the lock"
    render = re.search(r"void LedStrip::render_task\(\) \{(.*?)\n\}", cpp, re.S)[1]
    render += re.search(r"void LedStrip::render_frame\(\) \{(.*?)\n\}", cpp, re.S)[1]
    assert "notify" not in render
    header = (SRC / "LedStrip.h").read_text()
    assert '#include "LedListener.h"' in header
    assert re.search(r"LED_LISTENERS_MAX 4\b", (SRC / "LedListener.h").read_text())
