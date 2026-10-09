"""Led (led-strip) module tests. Run through a harness: `python -m xewe test --module led-strip`.

Hardware preconditions: provisioned board (first boot done). No LED strip is needed for the serial
tests: they read back settings, the render task's fps and the frame checksum. The harness lock must
list FastLED in [libraries]. Tests that need a real strip are skipped with "requires hardware".
Every test that changes a setting restores the value it found.
"""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from xewe.serialio import BOOT_READY, wait_for_banner

ID = "led"
NAME = "Led"          # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]
SRC = MODULE_DIR / "src" / "LedStrip"


def _status(serial) -> str:
    start = len(serial.lines)
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Source:", timeout=5)
    return "\n".join(serial.lines[start:])


def _field(text: str, label: str) -> str:
    m = re.findall(rf"{label}:\s+(.+)", text)
    assert m, f"no '{label}:' line in status"
    return m[-1].strip()


def _restart(serial) -> None:
    serial.send("$system restart")
    serial.expect(r"Rebooting", timeout=10)
    wait_for_banner(serial, BOOT_READY, 90, reset=False)
    serial.collect(silence=2.0, limit=30)


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Chip:\s+\S+ \(id \d+\)", timeout=5)
    serial.expect(r"Data pin:\s+GPIO \d+", timeout=5)
    serial.expect(r"Length:\s+\d+ \(max \d+\)", timeout=5)
    serial.expect(r"Brightness:\s+\d+/255", timeout=5)
    serial.expect(r"FPS:\s+\d+", timeout=5)


def test_brightness_roundtrip(serial):
    before = _field(_status(serial), "Brightness").split("/")[0]
    try:
        serial.command(f"${ID} brightness 77", expect=r"Led: brightness 77", timeout=5)
        assert _field(_status(serial), "Brightness") == "77/255"
        serial.command(f"${ID} brightness 300", expect=r"Led: brightness must be 0\.\.255", timeout=5)
        assert _field(_status(serial), "Brightness") == "77/255"
    finally:
        serial.command(f"${ID} brightness {before}", expect=rf"Led: brightness {before}", timeout=5)


def test_set_validates_and_applies(serial):
    before = _field(_status(serial), "Length").split()[0]
    try:
        serial.command(f"${ID} set num_led 12", expect=r"Led: num_led set to 12", timeout=5)
        assert _field(_status(serial), "Length").startswith("12 ")
        serial.command(f"${ID} set num_led 0", expect=r"Led: num_led must be 1\.\.\d+", timeout=5)
        serial.command(f"${ID} set chip NOPE", expect=r"Led: unknown chip 'NOPE'", timeout=5)
        serial.command(f"${ID} set colorder XYZ", expect=r"Led: colorder must be one of", timeout=5)
        serial.command(f"${ID} set nope 1", expect=r"Led: unknown key 'nope'", timeout=5)
    finally:
        serial.command(f"${ID} set num_led {before}", expect=rf"Led: num_led set to {before}", timeout=5)


def test_render_task_runs(serial):
    # frames are produced without a strip: fps > 0 one second after boot
    serial.collect(silence=1.2, limit=5)
    assert int(_field(_status(serial), "FPS")) > 0


def test_fill_changes_checksum(serial):
    try:
        serial.command(f"${ID} fill ff0000", expect=r"Led: fill ff0000", timeout=5)
        serial.collect(silence=0.6, limit=5)
        red = serial.command(f"${ID} checksum", expect=r"Led frame checksum: ([0-9a-f]{8})", timeout=5)[1]
        again = serial.command(f"${ID} checksum", expect=r"Led frame checksum: ([0-9a-f]{8})", timeout=5)[1]
        assert red == again, "a static fill must give a stable checksum"
        serial.command(f"${ID} fill 00ff00", expect=r"Led: fill 00ff00", timeout=5)
        serial.collect(silence=0.6, limit=5)
        green = serial.command(f"${ID} checksum", expect=r"Led frame checksum: ([0-9a-f]{8})", timeout=5)[1]
        assert green != red
    finally:
        serial.command(f"${ID} fill off", expect=r"Led: fill off", timeout=5)


def test_on_off(serial):
    serial.command(f"${ID} off", expect=r"Led: off", timeout=5)
    assert _field(_status(serial), "State") == "off"
    serial.command(f"${ID} on", expect=r"Led: on", timeout=5)
    assert _field(_status(serial), "State") == "on"


def test_2_3_x_command_names(serial):
    # LM2: the 2.3.x strip command names still work (aliases of on/off/brightness/set)
    serial.command(f"${ID} turn_off", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} toggle_state", expect=r"Led: on", timeout=5)
    serial.command(f"${ID} set_state 0", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} turn_on", expect=r"Led: on", timeout=5)
    assert _field(_status(serial), "State") == "on"
    before = _field(_status(serial), "Brightness").split("/")[0]
    serial.command(f"${ID} set_brightness {before}", expect=rf"Led: brightness {before}", timeout=5)
    order = _field(_status(serial), "Color order")
    serial.command(f"${ID} set_color_order {order}", expect=rf"Led: colorder set to {order}", timeout=5)


def test_settings_survive_restart(serial):
    text = _status(serial)
    before = (_field(text, "Brightness").split("/")[0], _field(text, "Length").split()[0], _field(text, "Color order"))
    try:
        serial.command(f"${ID} brightness 91", expect=r"Led: brightness 91", timeout=5)
        serial.command(f"${ID} set num_led 7", expect=r"Led: num_led set to 7", timeout=5)
        serial.command(f"${ID} set colorder BGR", expect=r"Led: colorder set to BGR", timeout=5)
        _restart(serial)
        text = _status(serial)
        assert _field(text, "Brightness") == "91/255"
        assert _field(text, "Length").startswith("7 ")
        assert _field(text, "Color order") == "BGR"
    finally:
        serial.command(f"${ID} brightness {before[0]}", expect=r"Led: brightness", timeout=5)
        serial.command(f"${ID} set num_led {before[1]}", expect=r"Led: num_led set to", timeout=5)
        serial.command(f"${ID} set colorder {before[2]}", expect=r"Led: colorder set to", timeout=5)


def test_brightness_zero_not_persisted(serial):
    # LX1 (2.3.x): `brightness 0` darkens the strip with State on, but NVS keeps the last non-zero value
    before = _field(_status(serial), "Brightness").split("/")[0]
    try:
        serial.command(f"${ID} on", expect=r"Led: on", timeout=5)
        serial.command(f"${ID} brightness 77", expect=r"Led: brightness 77", timeout=5)
        serial.command(f"${ID} brightness 0", expect=r"Led: brightness 0", timeout=5)
        text = _status(serial)
        assert _field(text, "Brightness") == "0/255 (stored 77)"
        assert _field(text, "State") == "on"
        _restart(serial)
        text = _status(serial)
        assert _field(text, "Brightness") == "77/255"
        assert _field(text, "State") == "on"
    finally:
        restore = before if before != "0" else "128"
        serial.command(f"${ID} brightness {restore}", expect=r"Led: brightness", timeout=5)


def test_strip_shows_colour(serial):
    pytest.skip("requires hardware: an LED strip on LED_PIN_DATA and a camera/eye to check the colour")


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (SRC / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert "FastLED" in props["depends_libraries"]


@pytest.mark.host
def test_chipset_table_matches_add_leds():
    table = re.findall(r"\{\s*(\d+),\s*\"(\w+)\",\s*(true|false)\s*\}", (SRC / "Chipsets.h").read_text())
    ids = [int(i) for i, _, _ in table]
    assert ids and len(ids) == len(set(ids)) and all(0 <= i <= 45 for i in ids)   # 2.3.x id range
    cases = re.findall(r"case (\d+):\s+c = &FastLED\.addLeds<(\w+),", (SRC / "LedStrip.cpp").read_text())
    assert sorted((int(i), n) for i, n in cases) == sorted((int(i), n) for i, n, _ in table)
    for _, name, clocked in table:   # a clocked chip gets the clock pin template argument
        line = next(l for l in (SRC / "LedStrip.cpp").read_text().splitlines() if f"addLeds<{name}," in l)
        assert ("LED_PIN_CLOCK" in line) == (clocked == "true"), name


@pytest.mark.host
def test_listener_fanout_host_gpp(tmp_path):
    # LedListener.h: fixed 4-slot set, no duplicates, registration order, origin echo suppression,
    # default no-op bodies for the led-modes callbacks
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_listeners"
    src = MODULE_DIR / "tests" / "host" / "test_listeners.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 28, f"only {summary[1]} checks ran (28 when written); was the test emptied?"


@pytest.mark.host
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
