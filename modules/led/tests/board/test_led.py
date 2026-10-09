"""Led module tests. Run through a harness: `python -m xewe test --module led`.

Hardware preconditions: provisioned board (first boot done). No LED strip is needed for the serial
tests: they read back settings, modes and parameters (`$led status`), the render task's fps and the
frame checksum (`$led checksum`). The harness lock must list FastLED in [libraries]. Tests that need
a real strip are skipped with "requires hardware". Every test that changes a setting or the mode
restores the value it found. Unit tests: tests/unit/test_led.py (`--unit-only`).

The `$led mode ...` tests and the merged status are unverified on a board: written 2026-10-09 (LM15)
while the board was offline; the strip tests are the led-strip ones, the mode tests the led-modes ones
with the new command syntax and the `Led:` reply prefix.
"""
import re
import zlib

import pytest

from xewe.board.serialio import BOOT_READY, wait_for_banner

ID = "led"
NAME = "Led"          # module.properties name == C++ name argument


def _status(serial) -> str:
    start = len(serial.lines)
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"Params:", timeout=5)
    serial.collect(silence=0.5, limit=5)         # the parameter lines
    return "\n".join(serial.lines[start:])


def _field(text: str, label: str) -> str:
    m = re.findall(rf"{label}:\s+(.+)", text)
    assert m, f"no '{label}:' line in status"
    return m[-1].strip()


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
    return serial.command(f"${ID} checksum", expect=r"Led frame checksum: ([0-9a-f]{8})", timeout=5)[1]


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
    serial.expect(r"Mode:\s+\[\d+\] \w", timeout=5)
    serial.expect(r"Color:\s+[0-9a-f]{6}", timeout=5)
    serial.expect(r"Params:", timeout=5)


# ---- strip ---------------------------------------------------------------------------------------
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
        red = _checksum(serial)
        assert _checksum(serial) == red, "a static fill must give a stable checksum"
        assert _field(_status(serial), "Source") == "fill"
        serial.command(f"${ID} fill 00ff00", expect=r"Led: fill 00ff00", timeout=5)
        serial.collect(silence=0.6, limit=5)
        assert _checksum(serial) != red
    finally:
        serial.command(f"${ID} fill off", expect=r"Led: fill off", timeout=5)
    assert _field(_status(serial), "Source") == "mode"


def test_on_off(serial):
    serial.command(f"${ID} off", expect=r"Led: off", timeout=5)
    assert _field(_status(serial), "State") == "off"
    serial.command(f"${ID} on", expect=r"Led: on", timeout=5)
    assert _field(_status(serial), "State") == "on"


def test_2_3_x_command_names(serial):
    # LM2/LM15: the 2.3.x strip and mode command names still work (aliases)
    serial.command(f"${ID} turn_off", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} toggle_state", expect=r"Led: on", timeout=5)
    serial.command(f"${ID} set_state 0", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} turn_on", expect=r"Led: on", timeout=5)
    text = _status(serial)
    assert _field(text, "State") == "on"
    before = _field(text, "Brightness").split("/")[0]
    serial.command(f"${ID} set_brightness {before}", expect=rf"Led: brightness {before}", timeout=5)
    order = _field(text, "Color order")
    serial.command(f"${ID} set_color_order {order}", expect=rf"Led: colorder set to {order}", timeout=5)
    mode = _mode(text)
    try:
        serial.command(f"${ID} set_mode 5", expect=r"Led: mode \[5\] Rainbow", timeout=5)
        serial.command(f"${ID} set_mode_param 5 speed 6", expect=r"Led: \[5\] speed = 6", timeout=5)
    finally:
        serial.command(f"${ID} mode param 5 speed 5", expect=r"speed = 5", timeout=5)
        serial.command(f"${ID} mode set {mode}", expect=r"Led: mode", timeout=5)


def test_settings_survive_restart(serial):
    text = _status(serial)
    before = (_field(text, "Brightness").split("/")[0], _field(text, "Length").split()[0],
              _field(text, "Color order"), _mode(text))
    try:
        serial.command(f"${ID} brightness 91", expect=r"Led: brightness 91", timeout=5)
        serial.command(f"${ID} set num_led 7", expect=r"Led: num_led set to 7", timeout=5)
        serial.command(f"${ID} set colorder BGR", expect=r"Led: colorder set to BGR", timeout=5)
        serial.command(f"${ID} mode set 6", expect=r"Led: mode \[6\] Christmas Lights", timeout=5)
        serial.command(f"${ID} mode param 6 density 3", expect=r"Led: \[6\] density = 3", timeout=5)
        _restart(serial)
        text = _status(serial)
        assert _field(text, "Brightness") == "91/255"
        assert _field(text, "Length").startswith("7 ")
        assert _field(text, "Color order") == "BGR"
        assert "[6] Christmas Lights" in text
        assert re.search(r"density = 3 \[1-10\]", text)
    finally:
        serial.command(f"${ID} brightness {before[0]}", expect=r"Led: brightness", timeout=5)
        serial.command(f"${ID} set num_led {before[1]}", expect=r"Led: num_led set to", timeout=5)
        serial.command(f"${ID} set colorder {before[2]}", expect=r"Led: colorder set to", timeout=5)
        serial.command(f"${ID} mode param 6 density 1", expect=r"density = 1", timeout=5)
        serial.command(f"${ID} mode set {before[3]}", expect=r"Led: mode", timeout=5)


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


# ---- modes ---------------------------------------------------------------------------------------
def test_mode_list(serial):
    serial.command(f"${ID} mode list", expect=r"\[0\] Solid: hue sat", timeout=5)
    serial.expect(r"\[6\] Christmas Lights: density speed", timeout=5)


def test_mode_set_and_params(serial):
    before = _mode(_status(serial))
    try:
        serial.command(f"${ID} mode set 5", expect=r"Led: mode \[5\] Rainbow", timeout=5)
        assert "[5] Rainbow" in _status(serial)
        serial.command(f"${ID} mode param 5 speed 99", expect=r"Led: \[5\] speed = 20", timeout=5)   # clamped
        serial.command(f"${ID} mode speed 7", expect=r"Led: speed 7", timeout=5)
        assert re.search(r"speed = 7 \[1-20\]", _status(serial))
        serial.command(f"${ID} mode param rainbow nope 1", expect=r"has no parameter 'nope'", timeout=5)
        serial.command(f"${ID} mode set 9", expect=r"Led: unknown mode '9'", timeout=5)
        serial.command(f"${ID} mode nope", expect=r"Led: usage: \$led mode list", timeout=5)
        serial.command(f"${ID} mode set solid", expect=r"Led: mode \[0\] Solid", timeout=5)
        serial.command(f"${ID} mode color 00ff00", expect=r"Led: color 00ff00", timeout=5)
        assert re.search(r"Color:\s+00ff00", _status(serial))
    finally:
        serial.command(f"${ID} mode param 5 speed 5", expect=r"speed = 5", timeout=5)
        serial.command(f"${ID} mode set {before}", expect=r"Led: mode", timeout=5)


def test_mode_color_query(serial):
    # `mode color` with no argument prints the `Color:` value of status in the setter's reply format
    expected = re.search(r"Color:\s+([0-9a-f]{6})", _status(serial))[1]
    serial.command(f"${ID} mode color", expect=rf"Led: color {expected}", timeout=5)


def test_mode_reset_params(serial):
    before = _mode(_status(serial))
    try:
        serial.command(f"${ID} mode set 5", expect=r"mode \[5\] Rainbow", timeout=5)
        serial.command(f"${ID} mode param 5 speed 9", expect=r"\[5\] speed = 9", timeout=5)
        serial.command(f"${ID} mode reset_params", expect=r"Led: \[5\] Rainbow parameters reset to defaults", timeout=5)
        assert re.search(r"speed = 5 \[1-20\]", _status(serial))          # table default
        serial.command(f"${ID} mode param 6 density 4", expect=r"\[6\] density = 4", timeout=5)
        serial.command(f"${ID} mode reset_params 6", expect=r"Led: \[6\] Christmas Lights parameters reset", timeout=5)
        assert "[5] Rainbow" in _status(serial)                              # other mode: no switch
        serial.command(f"${ID} mode reset_params 9", expect=r"Led: unknown mode '9'", timeout=5)
    finally:
        serial.command(f"${ID} mode set {before}", expect=r"Led: mode", timeout=5)


def test_checksum_changes_with_mode(serial):
    text = _status(serial)
    before = _mode(text)
    length = int(_field(text, "Length").split()[0])
    serial.command(f"${ID} mode set 0", expect=r"mode \[0\] Solid", timeout=5)
    solid_text = _status(serial)
    hue, sat = (re.search(rf"{k} = (\d+)", solid_text)[1] for k in ("hue", "sat"))
    try:
        serial.command(f"${ID} fill off", expect=r"Led: fill off", timeout=5)
        serial.command(f"${ID} mode color ff0000", expect=r"Led: color ff0000", timeout=5)
        serial.collect(silence=1.5, limit=5)        # past the 900 ms cross-fade
        solid = _checksum(serial)
        assert _checksum(serial) == solid, "Solid is static: its checksum must be stable"
        # the frame is pre-brightness RGB: pure red on every pixel (modes maths, see tests/unit)
        assert solid == "%08x" % zlib.crc32(b"\xff\x00\x00" * length)
        serial.command(f"${ID} mode set 5", expect=r"mode \[5\] Rainbow", timeout=5)
        serial.collect(silence=1.5, limit=5)
        assert _checksum(serial) != solid
    finally:
        serial.command(f"${ID} mode param 0 hue {hue}", expect=r"hue = ", timeout=5)
        serial.command(f"${ID} mode param 0 sat {sat}", expect=r"sat = ", timeout=5)
        serial.command(f"${ID} mode set {before}", expect=r"Led: mode", timeout=5)


def test_strip_shows_colour(serial):
    pytest.skip("requires hardware: an LED strip on LED_PIN_DATA and a camera/eye to check the colour")


def test_effects_look_right(serial):
    pytest.skip("requires hardware: an LED strip to judge the effects and the cross-fade visually")
