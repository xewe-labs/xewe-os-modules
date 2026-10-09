"""Led Modes module tests. Run through a harness: `python -m xewe test --module led-modes`.

Hardware preconditions: provisioned board (first boot done); led-strip is compiled in as the
dependency (harness lock lists FastLED). No LED strip is needed: modes and parameters are read back
through `$led_modes status`, frames through `$led checksum`. Tests restore the mode they found.
Unit tests (tests/unit/test_led-modes.py) parse src/LedModes/Effects.h and, when g++ is installed,
build and run tests/unit/test_effects.cpp.
"""
import re
import zlib

import pytest

from xewe.serialio import BOOT_READY, wait_for_banner

ID = "led_modes"
NAME = "Led Modes"    # module.properties name == C++ name argument


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
        # the frame is pre-brightness RGB: pure red on every pixel (Effects.h maths, see tests/unit)
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
