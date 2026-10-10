"""Fan module tests. Run through a harness: `python -m xewe test --module fan`.

Hardware preconditions: a provisioned board (first boot done; `xewe provision` answers the
"Would you like to enable ...?" prompt). Nothing needs to be wired for most tests. The add/set/remove
test needs a free output GPIO that is not a default fan pin (FAN1_PWM.. defines): XEWE_TEST_FAN_PIN;
the shared-pin test also XEWE_TEST_FAN_PIN2 (PWM into nothing is harmless). The RPM test needs a real
4-wire fan: XEWE_TEST_FAN_PWM_PIN + XEWE_TEST_FAN_TACH_PIN wired to it.
In a project that feeds the curve from a sensor (the cooling pad), the curve rewrites every fan speed
once a second, so speeds set by hand are not read back.
"""
import os

import pytest

from xewe.board.serialio import BOOT_READY, BOOT_UNPROVISIONED, wait_for_banner

ID = "fan"
NAME = "Fan"          # module.properties name == C++ name argument


def _pin(var):
    pin = os.environ.get(var)
    if not pin:
        pytest.skip(f"needs {var} (a free output GPIO, not a default fan pin)")
    return pin


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_add_set_remove(serial):
    pin = _pin("XEWE_TEST_FAN_PIN")
    serial.command(f"$fan add {pin}", expect=r"Fan added\.", timeout=5)
    try:
        serial.command(f"$fan set {pin} 128", expect=r"Speed updated\.", timeout=5)
        serial.command("$fan status", expect=rf"PWM pin {pin}, speed \d+, no tach", timeout=5)
        serial.command(f"$fan add {pin}", expect=r"Failed to add fan\.", timeout=5)      # duplicate
    finally:
        serial.command(f"$fan remove {pin}", expect=r"Fan removed\.", timeout=5)
    serial.command(f"$fan set {pin} 10", expect=r"Failed to set fan speed\.", timeout=5)


def test_shared_pin_refused(serial):
    pwm, tach = _pin("XEWE_TEST_FAN_PIN"), _pin("XEWE_TEST_FAN_PIN2")
    serial.command(f"$fan add_w_tach {pwm} {tach}", expect=r"Fan with tach added\.", timeout=5)
    try:
        serial.command(f"$fan add {tach}", expect=r"Failed to add fan\.", timeout=5)               # PWM on a tach pin
        serial.command(f"$fan add_w_tach {tach} {pwm}", expect=r"Failed to add fan\.", timeout=5)  # tach on a PWM pin
        serial.command(f"$fan add_w_tach {pwm} {pwm}", expect=r"Failed to add fan\.", timeout=5)   # same pin twice
    finally:
        serial.command(f"$fan remove {pwm}", expect=r"Fan removed\.", timeout=5)


def test_set_rejects_out_of_range(serial):
    serial.command("$fan set 4 256", expect=r"Failed to set fan speed\.", timeout=5)


def test_settings_table(serial):
    # core 2.1 table: `$fan set <key>` goes to the table, `$fan schema` lists 2 rows + fans + curve
    serial.command("$fan get curve_ms", expect=r"curve_ms=\d+", timeout=5)
    serial.command("$fan set stale_ms 999", expect=r"! \$fan set stale_ms: expected u32 in \[1000, 600000\]", timeout=5)
    serial.command("$fan set nope 1", expect=r"! \$fan: no setting 'nope'", timeout=5)
    serial.command("$fan schema", expect=r'\{"end":"fan","count":4\}', timeout=5)


def test_argc_error(serial):
    serial.command("$fan set 1", expect=r"Argument count mismatch for '\$fan set'; expected 2, got 1", timeout=5)


def test_curve_add_remove(serial):
    serial.command("$fan curve add 99.5 42", expect=r"Curve point added\.", timeout=5)
    try:
        serial.command("$fan curve list", expect=r"99\.50 C -> 42 % \(PWM 107\)", timeout=5)
        serial.command("$fan curve add 99.5 43", expect=r"Curve point added\.", timeout=5)   # replaces
        serial.command("$fan print_json", expect=r'\{"temp":99\.5,"speed":43\}', timeout=5)
        serial.command("$fan curve add 50 101", expect=r"Failed to add curve point\.", timeout=5)
    finally:
        serial.command("$fan curve remove 99.5", expect=r"Curve point removed\.", timeout=5)
    serial.command("$fan curve remove 99.5", expect=r"Failed to remove curve point\.", timeout=5)


def test_curve_set_rejects_bad_specs(serial):
    # nothing is changed by a rejected `set`
    serial.command("$fan curve set 22:101", expect=r"Failed to set curve: expected T:P", timeout=5)
    serial.command("$fan curve set 22:0,22:50", expect=r"Failed to set curve: two points at the same temperature",
                   timeout=5)
    serial.command("$fan curve set 300:50", expect=r"Failed to set curve: temperature outside", timeout=5)
    serial.command("$fan curve frob", expect=r"Usage: \$fan curve list", timeout=5)


def test_temperature_drives_curve(serial):
    # a point above every realistic one: `$fan temp` beyond it gives that point's speed
    serial.command("$fan curve add 199 37", expect=r"Curve point added\.", timeout=5)
    try:
        serial.command("$fan temp 199.5", expect=r"Temperature set to 199\.50 C, curve target 37 %\.", timeout=5)
    finally:
        serial.command("$fan curve remove 199", expect=r"Curve point removed\.", timeout=5)
    serial.command("$fan temp hot", expect=r"Invalid temperature\.", timeout=5)


def test_settings_survive_restart(serial):
    serial.command("$fan curve add 88.25 33", expect=r"Curve point added\.", timeout=5)
    try:
        serial.send("$system restart")
        m = wait_for_banner(serial, f"{BOOT_READY}|{BOOT_UNPROVISIONED}", 90, reset=False)
        assert m[0] == BOOT_READY, "board came back unprovisioned"
        serial.collect(silence=0.5)
        serial.command("$fan curve list", expect=r"88\.25 C -> 33 %", timeout=5)
    finally:
        serial.command("$fan curve remove 88.25", expect=r"Curve point removed\.", timeout=5)


def test_rpm_reading(serial):
    pwm = os.environ.get("XEWE_TEST_FAN_PWM_PIN")
    tach = os.environ.get("XEWE_TEST_FAN_TACH_PIN")
    if not (pwm and tach):
        pytest.skip("requires hardware: a 4-wire fan on XEWE_TEST_FAN_PWM_PIN / XEWE_TEST_FAN_TACH_PIN")
    serial.command(f"$fan set {pwm} 255", expect=r"Speed updated\.", timeout=5)
    m = None
    for _ in range(10):  # the RPM filter needs a few 1 s windows
        serial.send("$fan status")
        m = serial.expect(rf"PWM pin {pwm}, speed \d+, tach pin {tach}, (\d+) RPM", timeout=5)
        if int(m[1]) > 0:
            break
        serial.collect(silence=1.5, limit=2)   # next 1 s RPM window
    assert m and int(m[1]) > 0, "fan reports 0 RPM"
