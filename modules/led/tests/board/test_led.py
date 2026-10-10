"""Led module tests. Run through a harness: `python -m xewe test --module led`.

Hardware preconditions: provisioned board (first boot done). No LED strip is needed for the serial
tests: they read back settings, modes and parameters (`$led status`), the render task's fps and the
frame checksum (`$led checksum`). The harness lock must list FastLED in [libraries]. Tests that need
a real strip are skipped with "requires hardware". Every test that changes a setting or the mode
restores the value it found. Unit tests: tests/unit/test_led.py (`--unit-only`).

Status starts with the settings table rows (`num_led: 60`, `brightness: 128`, `state: true`, ...: the
persisted values); `Output:` is the live state. `$led set` replies `key=value` or a core `!` line; mode
commands reply with the `Led:` prefix.
"""
import json
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


def _output(text: str) -> tuple:
    # `Output:      on, 77/255` -> ("on", 77): the live state, not the stored one
    state, level = _field(text, "Output").split(", ")
    return state, int(level.split("/")[0])


def _checksum(serial) -> str:
    return serial.command(f"${ID} checksum", expect=r"Led: frame checksum: ([0-9a-f]{8})", timeout=5)[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)
    serial.expect(r"chip: \d+", timeout=5)                 # the settings table rows (core)
    serial.expect(r"num_led: \d+", timeout=5)
    serial.expect(r"brightness: \d+", timeout=5)
    serial.expect(r"state: (true|false)", timeout=5)
    serial.expect(r"pin_data: \d+", timeout=5)
    serial.expect(r"Chip:\s+\S+ \(id \d+\)", timeout=5)
    serial.expect(r"Pins:\s+data GPIO -?\d+", timeout=5)
    serial.expect(r"Output:\s+(on|off), \d+/255", timeout=5)
    serial.expect(r"FPS:\s+\d+", timeout=5)
    serial.expect(r"Mode:\s+\[\d+\] \w", timeout=5)
    serial.expect(r"Color:\s+[0-9a-f]{6}", timeout=5)
    serial.expect(r"Params:", timeout=5)


# ---- strip ---------------------------------------------------------------------------------------
def test_brightness_roundtrip(serial):
    before = _field(_status(serial), "brightness")
    try:
        serial.command(f"${ID} brightness 77", expect=r"Led: brightness 77", timeout=5)
        text = _status(serial)
        assert _field(text, "brightness") == "77" and _output(text)[1] == 77
        serial.command(f"${ID} brightness 300", expect=r"Led: brightness must be 0\.\.255", timeout=5)
        assert _field(_status(serial), "brightness") == "77"
        serial.command(f"${ID} set brightness 66", expect=r"^brightness=66", timeout=5)   # table path, applied live
        assert _output(_status(serial))[1] == 66
    finally:
        serial.command(f"${ID} brightness {before}", expect=rf"Led: brightness {before}", timeout=5)


def test_set_validates_and_applies(serial):
    before = _field(_status(serial), "num_led")
    try:
        serial.command(f"${ID} set num_led 12", expect=r"num_led=12", timeout=5)
        assert _field(_status(serial), "num_led") == "12"
        serial.command(f"${ID} get num_led", expect=r"num_led=12", timeout=5)
        serial.command(f"${ID} set num_led 0", expect=r"! \$led set num_led: expected u16 in \[1, \d+\]", timeout=5)
        serial.command(f"${ID} set chip NOPE", expect=r"Led: unknown chip 'NOPE'", timeout=5)
        serial.command(f"${ID} set colorder XYZ", expect=r"! \$led set colorder: expected u8 in \[0, 5\]", timeout=5)
        serial.command(f"${ID} set nope 1", expect=r"! \$led: no setting 'nope' \(see \$led schema\)", timeout=5)
    finally:
        serial.command(f"${ID} set num_led {before}", expect=rf"num_led={before}", timeout=5)


def test_schema(serial):
    # `$led schema`: the 8 table rows, then one row per mode parameter (group mode:<name>, set hint)
    start = len(serial.lines)
    serial.command(f"${ID} schema", expect=r'\{"end":"led","count":(\d+)\}', timeout=10)
    lines = [l for l in serial.lines[start:] if l.startswith("{")]
    rows = [json.loads(l) for l in lines[:-1]]
    end = json.loads(lines[-1])
    assert end["count"] == len(rows)
    table = {r["key"]: r for r in rows if "group" not in r}
    assert list(table) == ["chip", "num_led", "colorder", "voltage", "brightness", "state", "pin_data", "pin_clock"]
    assert table["num_led"]["type"] == "u16" and table["num_led"]["min"] == 1
    assert table["pin_data"]["restart"] is True and table["chip"]["restart"] is True
    modes = [r for r in rows if r.get("group", "").startswith("mode:")]
    assert len(modes) == 27                                   # every m:<id>:<key> (README NVS table)
    speed = next(r for r in modes if r["group"] == "mode:rainbow" and r["key"] == "speed")
    assert speed["min"] == 1 and speed["max"] == 20 and speed["set"] == "$led mode param 5 speed <v>"


def test_set_pin_data(serial):
    # pin_data is a RESTART row: stored at once, used from the next boot (clockless chips; APA102 keeps
    # the build pins). Moves the data pin to the current clock pin's GPIO and back.
    text = _status(serial)
    before, other = _field(text, "pin_data"), _field(text, "pin_clock")
    serial.command(f"${ID} set pin_data 99", expect=r"Led: GPIO 99 cannot drive the strip", timeout=5)
    serial.command(f"${ID} set pin_data {before}", expect=rf"pin_data={before}", timeout=5)
    serial.expect(r"Takes effect after \$system restart", timeout=5)
    if _field(text, "Chip").startswith("APA102"):
        pytest.skip("APA102 runs on the build pins")
    try:
        serial.command(f"${ID} set pin_data {other}", expect=rf"pin_data={other}", timeout=5)
        _restart(serial)
        assert re.match(rf"data GPIO {other}\b", _field(_status(serial), "Pins"))
        assert int(_field(_status(serial), "FPS")) > 0
    finally:
        serial.command(f"${ID} set pin_data {before}", expect=rf"pin_data={before}", timeout=5)
        _restart(serial)
    assert re.match(rf"data GPIO {before}\b", _field(_status(serial), "Pins"))


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


def test_fill_fade(serial):
    # `fill <rrggbb> <ms>` cross-fades from the current frame; at the end the frame is the plain fill
    try:
        serial.command(f"${ID} fill 0000ff", expect=r"Led: fill 0000ff", timeout=5)
        serial.collect(silence=0.6, limit=5)
        blue = _checksum(serial)
        serial.command(f"${ID} fill ff0000", expect=r"Led: fill ff0000", timeout=5)
        serial.collect(silence=0.6, limit=5)
        red = _checksum(serial)
        serial.command(f"${ID} fill 0000ff 3000", expect=r"Led: fill 0000ff", timeout=5)
        serial.collect(silence=0.5, limit=5)
        mid = _status(serial)
        assert _field(mid, "Transition") == "running"
        assert _checksum(serial) not in (blue, red), "half-way through the fade"
        serial.collect(silence=3.0, limit=10)
        assert _checksum(serial) == blue
        serial.command(f"${ID} fill 00ff00 70000", expect=r"Led: fill fade must be 0\.\.60000 ms", timeout=5)
    finally:
        serial.command(f"${ID} fill off", expect=r"Led: fill off", timeout=5)


def test_on_off(serial):
    serial.command(f"${ID} off", expect=r"Led: off", timeout=5)
    text = _status(serial)
    assert _output(text)[0] == "off" and _field(text, "state") == "false"
    serial.command(f"${ID} on", expect=r"Led: on", timeout=5)
    text = _status(serial)
    assert _output(text)[0] == "on" and _field(text, "state") == "true"


def test_2_3_x_command_names(serial):
    # the xewe-led-os strip and mode command names still work (aliases)
    serial.command(f"${ID} turn_off", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} toggle_state", expect=r"Led: on", timeout=5)
    serial.command(f"${ID} set_state 0", expect=r"Led: off", timeout=5)
    serial.command(f"${ID} turn_on", expect=r"Led: on", timeout=5)
    text = _status(serial)
    assert _output(text)[0] == "on"
    before = _field(text, "brightness")
    serial.command(f"${ID} set_brightness {before}", expect=rf"Led: brightness {before}", timeout=5)
    order = _field(text, "Color order")
    serial.command(f"${ID} set_color_order {order}", expect=rf"colorder={_field(text, 'colorder')}", timeout=5)
    serial.command(f"${ID} set_length {_field(text, 'num_led')}", expect=rf"num_led={_field(text, 'num_led')}", timeout=5)
    mode = _mode(text)
    try:
        serial.command(f"${ID} set_mode 5", expect=r"Led: mode \[5\] Rainbow", timeout=5)
        serial.command(f"${ID} set_mode_param 5 speed 6", expect=r"Led: \[5\] speed = 6", timeout=5)
    finally:
        serial.command(f"${ID} mode param 5 speed 5", expect=r"speed = 5", timeout=5)
        serial.command(f"${ID} mode set {mode}", expect=r"Led: mode", timeout=5)


def test_settings_survive_restart(serial):
    text = _status(serial)
    before = (_field(text, "brightness"), _field(text, "num_led"), _field(text, "Color order"), _mode(text))
    try:
        serial.command(f"${ID} brightness 91", expect=r"Led: brightness 91", timeout=5)
        serial.command(f"${ID} set num_led 7", expect=r"num_led=7", timeout=5)
        serial.command(f"${ID} set colorder BGR", expect=r"colorder=5", timeout=5)
        serial.command(f"${ID} mode set 6", expect=r"Led: mode \[6\] Christmas Lights", timeout=5)
        serial.command(f"${ID} mode param 6 density 3", expect=r"Led: \[6\] density = 3", timeout=5)
        _restart(serial)
        text = _status(serial)
        assert _field(text, "brightness") == "91" and _output(text)[1] == 91
        assert _field(text, "num_led") == "7"
        assert _field(text, "Color order") == "BGR"
        assert "[6] Christmas Lights" in text
        assert re.search(r"density = 3 \[1-10\]", text)
    finally:
        serial.command(f"${ID} brightness {before[0]}", expect=r"Led: brightness", timeout=5)
        serial.command(f"${ID} set num_led {before[1]}", expect=r"num_led=", timeout=5)
        serial.command(f"${ID} set colorder {before[2]}", expect=r"colorder=", timeout=5)
        serial.command(f"${ID} mode param 6 density 1", expect=r"density = 1", timeout=5)
        serial.command(f"${ID} mode set {before[3]}", expect=r"Led: mode", timeout=5)


def test_brightness_zero_not_persisted(serial):
    # `brightness 0` darkens the strip with State on, but NVS keeps the last non-zero value
    before = _field(_status(serial), "brightness")
    try:
        serial.command(f"${ID} on", expect=r"Led: on", timeout=5)
        serial.command(f"${ID} brightness 77", expect=r"Led: brightness 77", timeout=5)
        serial.command(f"${ID} brightness 0", expect=r"Led: brightness 0", timeout=5)
        text = _status(serial)
        assert _output(text) == ("on", 0) and _field(text, "brightness") == "77"
        assert _field(text, "state") == "true"
        _restart(serial)
        text = _status(serial)
        assert _output(text) == ("on", 77) and _field(text, "brightness") == "77"
    finally:
        serial.command(f"${ID} brightness {before}", expect=r"Led: brightness", timeout=5)


def test_off_transient_not_persisted(serial):
    # `$led set state false` is the stored state; the C++ set_state(false, origin, false) (no CLI) is not.
    # Here: the stored state survives a restart, the live one follows it.
    try:
        serial.command(f"${ID} set state false", expect=r"state=false", timeout=5)
        assert _output(_status(serial))[0] == "off"
        _restart(serial)
        text = _status(serial)
        assert _output(text)[0] == "off" and _field(text, "state") == "false"
    finally:
        serial.command(f"${ID} set state true", expect=r"state=true", timeout=5)
    assert _output(_status(serial))[0] == "on"


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
    length = int(_field(text, "num_led"))
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
