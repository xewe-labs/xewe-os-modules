"""Led Modes module tests. Run through a harness: `python -m xewe test --module led-modes`.

Hardware preconditions: provisioned board (first boot done); led-strip is compiled in as the
dependency (harness lock lists FastLED). No LED strip is needed: modes and parameters are read back
through `$led_modes status`, frames through `$led checksum`. Tests restore the mode they found.
Host tests parse src/LedModes/Effects.h and, when g++ is installed, build and run tests/host.
"""
import re
import shutil
import subprocess
import zlib
from pathlib import Path

import pytest

from xewe.serialio import BOOT_READY, wait_for_banner

ID = "led_modes"
NAME = "Led Modes"    # module.properties name == C++ name argument
MODULE_DIR = Path(__file__).resolve().parents[1]
EFFECTS_H = MODULE_DIR / "src" / "LedModes" / "Effects.h"

PARAM_RX = re.compile(r"\{\"(\w+)\", \"([^\"]+)\", (\d+), (\d+), (\d+), (\d+), '([ab])'\},")
TABLE_RX = re.compile(r"constexpr ParamDef (\w+)\[\] = \{\n(.*?)\n\};", re.S)
MODE_RX = re.compile(r"\{(\d+), \"([^\"]+)\", (\w+), LED_FX_COUNT_OF\((\w+)\)\},")


def _status(serial) -> str:
    start = len(serial.lines)
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Params:", timeout=5)
    serial.collect(silence=0.5, limit=5)         # the parameter lines
    return "\n".join(serial.lines[start:])


def _mode(text: str) -> int:
    m = re.search(r"Mode:\s+\[(\d+)\]", text)
    assert m, "no Mode line in status"
    return int(m[1])


def _restart(serial) -> None:
    serial.send("$system restart")
    serial.expect(r"Rebooting", timeout=10)
    wait_for_banner(serial, BOOT_READY, 90, reset=False)
    serial.collect(silence=2.0, limit=30)


def _checksum(serial) -> str:
    return serial.command("$led checksum", expect=r"Led frame checksum: ([0-9a-f]{8})", timeout=5)[1]


def test_compiles(compiled):
    # build of the harness firmware (led-strip + led-modes) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Mode:\s+\[\d\] \w", timeout=5)


def test_list(serial):
    serial.command(f"${ID} list", expect=r"\[0\] Solid: hue sat", timeout=5)
    serial.expect(r"\[6\] Christmas Lights: density speed", timeout=5)


def test_set_mode_and_params(serial):
    before = _mode(_status(serial))
    try:
        serial.command(f"${ID} set 5", expect=r"Led Modes: mode \[5\] Rainbow", timeout=5)
        assert "[5] Rainbow" in _status(serial)
        serial.command(f"${ID} param 5 speed 99", expect=r"Led Modes: \[5\] speed = 20", timeout=5)   # clamped
        serial.command(f"${ID} speed 7", expect=r"Led Modes: speed 7", timeout=5)
        assert re.search(r"speed = 7 \[1-20\]", _status(serial))
        serial.command(f"${ID} param rainbow nope 1", expect=r"has no parameter 'nope'", timeout=5)
        serial.command(f"${ID} set 9", expect=r"unknown mode '9'", timeout=5)
        serial.command(f"${ID} set solid", expect=r"Led Modes: mode \[0\] Solid", timeout=5)
        serial.command(f"${ID} color 00ff00", expect=r"Led Modes: color 00ff00", timeout=5)
        assert re.search(r"Color:\s+00ff00", _status(serial))
    finally:
        serial.command(f"${ID} param 5 speed 5", expect=r"speed = 5", timeout=5)
        serial.command(f"${ID} set {before}", expect=r"Led Modes: mode", timeout=5)


def test_color_query(serial):
    # unverified: written without a board (2026-10-09 night run, LH1); `color` with no argument
    # prints the `Color:` value of status in the same format as the setter's reply
    text = _status(serial)
    expected = re.search(r"Color:\s+([0-9a-f]{6})", text)[1]
    serial.command(f"${ID} color", expect=rf"Led Modes: color {expected}", timeout=5)


def test_reset_params(serial):
    # unverified: written without a board (2026-10-09 night run, LH1)
    before = _mode(_status(serial))
    try:
        serial.command(f"${ID} set 5", expect=r"mode \[5\] Rainbow", timeout=5)
        serial.command(f"${ID} param 5 speed 9", expect=r"\[5\] speed = 9", timeout=5)
        serial.command(f"${ID} reset_params", expect=r"Led Modes: \[5\] Rainbow parameters reset to defaults", timeout=5)
        assert re.search(r"speed = 5 \[1-20\]", _status(serial))          # table default
        serial.command(f"${ID} param 6 density 4", expect=r"\[6\] density = 4", timeout=5)
        serial.command(f"${ID} reset_params 6", expect=r"Led Modes: \[6\] Christmas Lights parameters reset", timeout=5)
        assert "[5] Rainbow" in _status(serial)                              # other mode: no switch
        serial.command(f"${ID} reset_params 9", expect=r"unknown mode '9'", timeout=5)
    finally:
        serial.command(f"${ID} set {before}", expect=r"Led Modes: mode", timeout=5)


def test_checksum_changes_with_mode(serial):
    before = _mode(_status(serial))
    start = len(serial.lines)
    serial.command("$led status", expect=r"Length:\s+\d+", timeout=5)
    length = int(re.search(r"Length:\s+(\d+)", "\n".join(serial.lines[start:]))[1])
    serial.command(f"${ID} set 0", expect=r"mode \[0\] Solid", timeout=5)
    solid_text = _status(serial)
    hue, sat = (re.search(rf"{k} = (\d+)", solid_text)[1] for k in ("hue", "sat"))
    try:
        serial.command("$led fill off", expect=r"Led: fill off", timeout=5)
        serial.command(f"${ID} color ff0000", expect=r"Led Modes: color ff0000", timeout=5)
        serial.collect(silence=1.5, limit=5)        # past the 900 ms cross-fade
        solid = _checksum(serial)
        assert _checksum(serial) == solid, "Solid is static: its checksum must be stable"
        # the frame is pre-brightness RGB: pure red on every pixel (Effects.h maths, see tests/host)
        assert solid == "%08x" % zlib.crc32(b"\xff\x00\x00" * length)
        serial.command(f"${ID} set 5", expect=r"mode \[5\] Rainbow", timeout=5)
        serial.collect(silence=1.5, limit=5)
        assert _checksum(serial) != solid
    finally:
        serial.command(f"${ID} param 0 hue {hue}", expect=r"hue = ", timeout=5)
        serial.command(f"${ID} param 0 sat {sat}", expect=r"sat = ", timeout=5)
        serial.command(f"${ID} set {before}", expect=r"Led Modes: mode", timeout=5)


def test_settings_survive_restart(serial):
    before = _mode(_status(serial))
    try:
        serial.command(f"${ID} set 6", expect=r"mode \[6\] Christmas Lights", timeout=5)
        serial.command(f"${ID} param 6 density 3", expect=r"\[6\] density = 3", timeout=5)
        _restart(serial)
        text = _status(serial)
        assert "[6] Christmas Lights" in text
        assert re.search(r"density = 3 \[1-10\]", text)
    finally:
        serial.command(f"${ID} param 6 density 1", expect=r"density = 1", timeout=5)
        serial.command(f"${ID} set {before}", expect=r"Led Modes: mode", timeout=5)


def test_effects_look_right(serial):
    pytest.skip("requires hardware: an LED strip to judge the effects and the cross-fade visually")


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["depends_modules"] == "led-strip"


@pytest.mark.host
def test_param_tables_consistent():
    src = EFFECTS_H.read_text()
    tables = {name: PARAM_RX.findall(body) for name, body in TABLE_RX.findall(src)}
    modes = MODE_RX.findall(src)
    assert [int(m[0]) for m in modes] == list(range(7)), "mode ids unique and contiguous 0..6"
    assert len({m[1] for m in modes}) == 7, "mode names unique"
    max_params = int(re.search(r"MAX_PARAMS\s*=\s*(\d+)", src)[1])
    for mode_id, name, table, counted in modes:
        assert table == counted and table in tables, name
        params = tables[table]
        body = re.search(rf"constexpr ParamDef {table}\[\] = \{{\n(.*?)\n\}};", src, re.S)[1]
        assert len(params) == len([l for l in body.splitlines() if l.strip()]), f"{table}: unparsed row"
        assert 0 < len(params) <= max_params, name
        assert len({p[0] for p in params}) == len(params), f"{name}: duplicate key"
        for key, _display, lo, hi, default, step, _type in params:
            lo, hi, default, step = int(lo), int(hi), int(default), int(step)
            assert lo <= default <= hi <= 65535, f"{name}.{key}"
            assert step >= 1, f"{name}.{key}"
            assert len(f"m:{mode_id}:{key}") <= 15, f"NVS key m:{mode_id}:{key} longer than 15"


@pytest.mark.host
def test_effects_host_gpp(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_effects"
    src = MODULE_DIR / "tests" / "host" / "test_effects.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 40, f"only {summary[1]} checks ran (44 when written); was the test emptied?"


@pytest.mark.host
def test_listener_forwarding_and_reset_api():
    # LH1: setters take an origin and forward on_mode/on_color/on_param through led-strip's listeners;
    # reset_params writes the defaults in one pass with one activate() (one cross-fade)
    h = (MODULE_DIR / "src" / "LedModes" / "LedModes.h").read_text()
    cpp = (MODULE_DIR / "src" / "LedModes" / "LedModes.cpp").read_text()
    for decl in ("set_mode", "set_param", "set_color", "set_speed", "reset_params"):
        assert re.search(rf"\b{decl}\s*\([^;]*const void\* origin = nullptr\);", h, re.S), decl
    assert re.search(r"uint32_t\s+get_color\s*\(\) const;", h)
    body = re.search(r"bool LedModes::reset_params\(int mode_id, const void\* origin\) \{(.*?)\n\}", cpp, re.S)[1]
    assert body.count("activate(") == 1 and body.count("persist_params(") == 1 and "set_param(" not in body
    assert "default_params(" in body and "notify_params(" in body
    assert "l.on_mode(" in cpp and "l.on_color(" in cpp and "l.on_param(" in cpp
    assert "notify" not in re.search(r"void LedModes::render\(.*?\n\}", cpp, re.S)[0], "render task must not notify"
    assert re.search(r'register_command\(\{"color", [^}]*, 0,', cpp), "`color` without argument"
    assert re.search(r'register_command\(\{"reset_params", [^}]*, 0,', cpp)
    assert re.search(r'register_command\(\{"reset_params", [^}]*, 1,', cpp)
