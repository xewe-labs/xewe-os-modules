# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
"""Led module tests. Run through a harness: `python -m xewe test --module led`.

Hardware preconditions: provisioned board with the led setup wizard completed (chip, voltage,
length and colour order answered; `xewe provision` only answers y/n prompts, so the wizard's menus
are answered by hand once), a strip on XEWE_MODULE_LED_PIN_DATA (no strip still runs every test;
nothing here reads the light back). Each test restores what it changes. The last test,
`test_reset_runs_wizard`, wipes the led settings and answers the wizard again; it runs only with
XEWE_TEST_LED_RESET=1.
"""

import os
import re

import pytest

from xewe.board.serialio import BOOT_READY, wait_for_banner

ID = "led"
NAME = "Led"  # module.properties name == C++ name argument
INPUT = (
    r"^(?:\(y/n\) )?> $"  # the input line of a firmware prompt; answer only after it
)


def _send(serial, cmd: str) -> None:
    """Send one command and wait for the board's echo of it. The core echoes a line when it reads
    it and keeps at most 4 read lines waiting (more are dropped with `! Input overflow: line
    dropped`); the CLI reads input only between commands, so waiting for each echo keeps at most
    one line queued. For commands that print nothing else (`set_mode`, `set_rgb`, ...)."""
    serial.command(cmd, expect=rf"^{re.escape(cmd)}$", timeout=5)


def _status(serial) -> str:
    serial.send(f"${ID} status")
    return "\n".join(serial.collect(silence=1.0, limit=10))


def _live(serial) -> dict:
    """Mode, colour, brightness and state from `$led status`."""
    text = _status(serial)
    return {
        "mode": int(re.search(r"Mode: \[(\d)\]", text)[1]),
        "rgb": re.search(r"Color \(RGB\):\s+\((\d+), (\d+), (\d+)\)", text).groups(),
        "brightness": int(re.search(r"Brightness:\s+(\d+)/255", text)[1]),
        "state": 1 if re.search(r"Power State:\s+ON", text) else 0,
    }


def _mode_params(serial) -> dict:
    """The current mode's live parameter values from `$led get_mode_params`."""
    serial.send(f"${ID} get_mode_params")
    text = "\n".join(serial.collect(silence=1.0, limit=10))
    return {
        k: int(v) for k, v in re.findall(r"- (\w+) \([^)]*\): (\d+) \[Range:", text)
    }


def _restore_live(serial, live: dict, params: dict) -> None:
    """Back to a recorded mode, its colour and parameters, brightness and state. The colour goes
    first: re-applying it rebuilds the mode, the parameters after it are then set exactly."""
    _send(serial, f"${ID} set_mode {live['mode']}")
    _send(serial, f"${ID} set_rgb {' '.join(live['rgb'])}")
    for key, value in params.items():
        _send(serial, f"${ID} set_mode_param {key} {value}")
    _send(serial, f"${ID} set_brightness {live['brightness']}")
    _send(serial, f"${ID} set_state {live['state']}")
    serial.command(f"${ID} status", expect=rf"Mode: \[{live['mode']}\]", timeout=5)


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(
        f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5
    )
    serial.expect(r"^Hardware:", timeout=5)
    serial.expect(r"Chip:\s+\w+", timeout=5)
    serial.expect(r"Data Pin:\s+GPIO_\d+", timeout=5)
    serial.expect(r"^Live State:", timeout=5)
    serial.expect(r"FPS:\s+\d+", timeout=5)
    serial.expect(r"^Mode: \[\d\] ", timeout=5)


def test_settings_table(serial):
    serial.command(f"${ID} get num_led", expect=r"num_led=\d+", timeout=5)
    serial.command(f"${ID} get chip", expect=r"chip=\d+", timeout=5)
    serial.command(f"${ID} schema", expect=r'\{"end":"led","count":\d+\}', timeout=5)


def test_not_disableable(serial):
    # can_be_disabled is false: there are no enable/disable commands
    serial.command(
        f"${ID} disable",
        expect=r"Error: Unknown command 'disable' in command group 'led'",
        timeout=5,
    )


def test_cli_errors_are_the_core_texts(serial):
    serial.command(
        f"${ID} nope",
        expect=r"Error: Unknown command 'nope' in command group 'led'",
        timeout=5,
    )
    serial.command(
        f"${ID} set_rgb 1 2",
        expect=r"Error: Argument count mismatch for '\$led set_rgb'; expected 3, got 2",
        timeout=5,
    )
    serial.expect(r"Usage: \$led set_rgb 255 0 0", timeout=5)


def test_length_and_color_order_messages(serial):
    serial.command(
        f"${ID} set_length 65535",
        expect=r"That's too many\. Max supported: \d+ LEDs",
        timeout=5,
    )
    serial.command(
        f"${ID} set_color_order XYZ",
        expect=r"Invalid color order\. Use RGB, RBG, GRB, GBR, BRG, or BGR",
        timeout=5,
    )


def test_mode_and_params_round_trip(serial):
    live = _live(serial)
    params = _mode_params(serial)
    _send(serial, f"${ID} set_mode 5")
    rainbow = _mode_params(serial)
    try:
        serial.command(
            f"${ID} get_mode_params",
            expect=r"Mode 5 \(Rainbow\) Parameters:",
            timeout=5,
        )
        serial.expect(r"- speed \(Speed\): \d+ \[Range: 1-20\]", timeout=5)
        _send(serial, f"${ID} set_mode_param speed 20")
        serial.command(
            f"${ID} get_mode_params",
            expect=r"- speed \(Speed\): 20 \[Range: 1-20\]",
            timeout=5,
        )
        _send(serial, f"${ID} reset_current_mode")
        serial.command(
            f"${ID} get_mode_params",
            expect=r"- speed \(Speed\): 5 \[Range: 1-20\]",
            timeout=5,
        )
    finally:
        _send(serial, f"${ID} set_mode 5")
        for key, value in rainbow.items():  # Rainbow's own stored parameters
            _send(serial, f"${ID} set_mode_param {key} {value}")
        _restore_live(serial, live, params)


def test_brightness_round_trip(serial):
    before = int(re.search(r"Brightness:\s+(\d+)/255", _status(serial))[1])
    try:
        _send(serial, f"${ID} set_brightness 10")
        serial.command(f"${ID} status", expect=r"Brightness:\s+10/255", timeout=5)
    finally:
        _send(serial, f"${ID} set_brightness {before}")
        serial.command(
            f"${ID} status", expect=rf"Brightness:\s+{before}/255", timeout=5
        )


def test_render_task_runs(serial):
    # the render task delivers frames every 20 ms (about 50 fps; long strips are slower), and the
    # console keeps answering while it runs (render task priority above loopTask, C3/C6 single core)
    fps = int(re.search(r"FPS:\s+(\d+)", _status(serial))[1])
    assert 1 <= fps <= 50
    for _ in range(5):
        serial.command(f"${ID} get num_led", expect=r"num_led=\d+", timeout=2)


def test_reset_runs_wizard(serial):
    # LAST in this file: `$led reset` wipes the led namespace (settings and mode parameters),
    # restarts, and the setup wizard asks again; the answers and the commands after it restore
    # every setting, the line lengths, the parameters of all 7 modes, mode, colour, brightness and
    # state
    if os.environ.get("XEWE_TEST_LED_RESET") != "1":
        pytest.skip(
            "destructive: set XEWE_TEST_LED_RESET=1 to reset the led module and answer its wizard"
        )

    def get(key: str) -> str:
        return serial.command(f"${ID} get {key}", expect=rf"{key}=(\w+)", timeout=5)[1]

    chip, num_led, colorder, voltage, lines = (
        int(get(k)) for k in ("chip", "num_led", "colorder", "voltage", "lines")
    )
    use_clk = get("use_clk") in ("true", "1")
    serial.send(f"${ID} schema")
    schema = "\n".join(serial.collect(silence=1.0, limit=10))
    line_counts = [
        int(v)
        for v in re.findall(
            r'"key":"l_\d+_cnt","group":"lines","type":"u16","value":(\d+)', schema
        )
    ]
    live = _live(serial)
    params = {}
    for mode in range(7):  # every mode's stored parameters (the reset wipes them)
        _send(serial, f"${ID} set_mode {mode}")
        params[mode] = _mode_params(serial)

    serial.command(f"${ID} reset", expect=rf"{NAME} module reset", timeout=10)
    serial.expect(r"Rebooting", timeout=10)

    def answer(prompt: str, value: str, timeout: float = 90) -> None:
        serial.expect(prompt, timeout=timeout)
        serial.expect(INPUT, timeout=10)
        serial.send(value)

    answer(r"Is LED data line connected to pin GPIO_\d+\?", "y")
    answer(r"Are you using LED with CLK line\?", "y" if use_clk else "n")
    if use_clk:
        answer(r"Is LED CLK line connected to pin GPIO_\d+\?", "y")
    answer(r"What is your LED Chip\?", str(chip + 1))
    answer(r"What is LED Voltage\?", {5: "1", 12: "2", 24: "3"}.get(voltage, "1"))
    if lines > 0:
        answer(r"Do you have parallel LED strips attached to data line\?", "y")
        answer(r"How many LED strips do you have in parallel\?", str(lines))
        for i, count in enumerate(line_counts):
            answer(
                rf"How many LEDs are connected on parallel line #{i + 1}\?", str(count)
            )
    else:
        answer(r"Do you have parallel LED strips attached to data line\?", "n")
        answer(r"How many LEDs do you have connected\?", str(num_led))
    answer(r"What color are LEDs now\?", "2")  # Green
    answer(r"What color are LEDs now\?", "1")  # Red -> "RGB"
    serial.expect(r"Color order set to RGB", timeout=20)
    serial.expect(r"LED setup success!", timeout=10)
    wait_for_banner(serial, BOOT_READY, 90, reset=False)
    serial.command(
        f"${ID} set colorder {colorder}", expect=rf"colorder={colorder}", timeout=5
    )
    serial.command(
        f"${ID} set num_led {num_led}", expect=rf"num_led={num_led}", timeout=5
    )
    for mode in range(7):
        _send(serial, f"${ID} set_mode {mode}")
        for key, value in params[mode].items():
            _send(serial, f"${ID} set_mode_param {key} {value}")
    _restore_live(serial, live, params[live["mode"]])
    serial.command(f"${ID} get lines", expect=rf"lines={lines}", timeout=5)
