# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
# xewe-os-modules/tools/test_validate.py
"""Unit tests of the validator's `config` rule: one fixture module per finding. Standard library
and pytest only (no xewe-os-tools): `python -m pytest tools/test_validate.py`."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "validate", Path(__file__).with_name("validate.py")
)
validate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(validate)

OWNERS = validate.config_owners(["led", "led-web", "wifi"])

GOOD_CONFIG = """\
// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Config.h
//
// Fixture.
#pragma once

// ---- runtime defaults ----

// Voltage (led/voltage), V
#ifndef XEWE_LED_VOLTAGE
#define XEWE_LED_VOLTAGE 5
#endif

// ---- first-boot values ----

// Chip (led/chip), 0 to 45; asked at first boot
#ifndef XEWE_LED_CHIP
#define XEWE_LED_CHIP 0
#define XEWE_LED_CHIP_PROMPT 1
#endif

// ---- compile-time constants ----

// Data pin
#ifndef XEWE_LED_PIN_DATA
#if defined(XEWE_CHIP_C3)
#define XEWE_LED_PIN_DATA 4
#else
#define XEWE_LED_PIN_DATA 48
#endif
#endif
"""

GOOD_CPP = """\
#include "Led.h"

xewe::Settings Led::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Led::voltage>("voltage", 5, 24, XEWE_LED_VOLTAGE, "Volts, (5, 12, 24)"),
        xewe::setting<&Led::chip>(
            "chip", 0, 45, XEWE_LED_CHIP, "Chip", xewe::SettingDef::RESTART),
    };
    return {table, this};
}

void Led::begin_routines_init() {
#ifdef XEWE_LED_CHIP_PROMPT
    ask();
#else
    store(XEWE_LED_CHIP);
#endif
}
"""


def _findings(
    tmp_path: Path,
    config: str | None = GOOD_CONFIG,
    cpp: str = GOOD_CPP,
    slug: str = "led",
) -> list[str]:
    src = tmp_path / "src" / "Led"
    src.mkdir(parents=True)
    if config is not None:
        (src / "Config.h").write_text(config)
    (src / "Led.cpp").write_text(cpp)
    out: list[str] = []
    validate.check_config(src, slug, out.append, OWNERS)
    return out


def _one(findings: list[str], text: str) -> None:
    assert any(text in f for f in findings), findings


def test_good_module_passes(tmp_path):
    assert _findings(tmp_path) == []


def test_wrong_prefix(tmp_path):
    config = GOOD_CONFIG.replace("XEWE_LED_VOLTAGE", "XEWE_LEDX_VOLTAGE")
    _one(
        _findings(tmp_path, config),
        "'XEWE_LEDX_VOLTAGE' must match XEWE_LED_<PARAM>",
    )


def test_missing_marker(tmp_path):
    config = GOOD_CONFIG.replace("// ---- first-boot values ----\n", "")
    found = _findings(tmp_path, config)
    _one(found, "marker '// ---- first-boot values ----' missing")


def test_markers_out_of_order(tmp_path):
    config = GOOD_CONFIG.replace("// ---- runtime defaults ----", "// ---- tmp ----")
    config = config.replace(
        "// ---- compile-time constants ----", "// ---- runtime defaults ----"
    )
    config = config.replace("// ---- tmp ----", "// ---- compile-time constants ----")
    _one(_findings(tmp_path, config), "out of order")


def test_group_before_the_first_marker(tmp_path):
    config = GOOD_CONFIG.replace(
        "#pragma once\n",
        "#pragma once\n\n// Early\n#ifndef XEWE_LED_EARLY\n#define XEWE_LED_EARLY 1\n#endif\n",
    )
    _one(
        _findings(tmp_path, config),
        "#ifndef XEWE_LED_EARLY before the first section marker",
    )


def test_literal_row_default(tmp_path):
    cpp = GOOD_CPP.replace("XEWE_LED_VOLTAGE,", "5,")
    _one(
        _findings(tmp_path, cpp=cpp),
        "settings row 'voltage' default must be XEWE_LED_VOLTAGE, got '5'",
    )


def test_row_default_must_be_a_stored_value(tmp_path):
    cpp = GOOD_CPP.replace("XEWE_LED_VOLTAGE,", "XEWE_LED_PIN_DATA,").replace(
        '"voltage"', '"pin_data"'
    )
    _one(
        _findings(tmp_path, cpp=cpp),
        "XEWE_LED_PIN_DATA is not a runtime-default or first-boot define",
    )


def test_rows_without_config(tmp_path):
    _one(_findings(tmp_path, config=None), "settings rows need src/<Folder>/Config.h")


def test_prompt_outside_first_boot(tmp_path):
    config = GOOD_CONFIG.replace(
        "#define XEWE_LED_VOLTAGE 5\n",
        "#define XEWE_LED_VOLTAGE 5\n#define XEWE_LED_VOLTAGE_PROMPT 1\n",
    )
    _one(
        _findings(tmp_path, config),
        "XEWE_LED_VOLTAGE_PROMPT outside the first-boot section",
    )


def test_first_boot_without_prompt_define(tmp_path):
    config = GOOD_CONFIG.replace("#define XEWE_LED_CHIP_PROMPT 1\n", "")
    _one(
        _findings(tmp_path, config),
        "first-boot XEWE_LED_CHIP must define XEWE_LED_CHIP_PROMPT 1",
    )


def test_first_boot_never_guarded(tmp_path):
    cpp = GOOD_CPP.replace("#ifdef XEWE_LED_CHIP_PROMPT", "#if 1")
    _one(
        _findings(tmp_path, cpp=cpp),
        "first-boot XEWE_LED_CHIP is never asked under #ifdef XEWE_LED_CHIP_PROMPT",
    )


def test_prompt_tested_for_a_value_that_is_not_first_boot(tmp_path):
    cpp = GOOD_CPP + "#if defined(XEWE_LED_VOLTAGE_PROMPT)\n#endif\n"
    _one(
        _findings(tmp_path, cpp=cpp),
        "XEWE_LED_VOLTAGE_PROMPT is tested, but XEWE_LED_VOLTAGE is no first-boot value",
    )


def test_missing_guard(tmp_path):
    config = GOOD_CONFIG.replace(
        "// ---- compile-time constants ----\n",
        "// ---- compile-time constants ----\n\n#define XEWE_LED_BARE 1\n",
    )
    _one(
        _findings(tmp_path, config),
        "#define XEWE_LED_BARE must sit inside #ifndef XEWE_LED_BARE",
    )


def test_comment_line_above_each_ifndef(tmp_path):
    none = GOOD_CONFIG.replace("// Voltage (led/voltage), V\n", "")
    _one(
        _findings(tmp_path / "a", none),
        "#ifndef XEWE_LED_VOLTAGE needs exactly one comment line",
    )
    two = GOOD_CONFIG.replace(
        "// Voltage (led/voltage), V\n", "// Voltage\n// (led/voltage), V\n"
    )
    _one(
        _findings(tmp_path / "b", two),
        "#ifndef XEWE_LED_VOLTAGE needs exactly one comment line",
    )


def test_include_and_code_refused(tmp_path):
    config = GOOD_CONFIG.replace(
        "#pragma once\n", "#pragma once\n#include <stdint.h>\nint x;\n"
    )
    found = _findings(tmp_path, config)
    _one(found, "no #include")
    _one(found, "only comment and preprocessor lines are allowed")


def test_led_may_not_define_a_led_web_name(tmp_path):
    config = (
        GOOD_CONFIG
        + "\n// Not ours\n#ifndef XEWE_LED_WEB_X\n#define XEWE_LED_WEB_X 1\n#endif\n"
    )
    _one(
        _findings(tmp_path / "a", config),
        "'XEWE_LED_WEB_X' has the prefix of led-web, not XEWE_LED_",
    )
    cpp = GOOD_CPP + "#define XEWE_LED_WEB_X 1\n"
    _one(
        _findings(tmp_path / "b", cpp=cpp),
        "'XEWE_LED_WEB_X' is defined outside Config.h (a led-web name)",
    )


@pytest.mark.parametrize(
    "name", ["XEWE_LED_FRAME_MS", "XEWE_CORE_SERIAL_BAUD", "XEWE_WIFI_DEBUG"]
)
def test_no_module_or_core_define_outside_config(tmp_path, name):
    cpp = GOOD_CPP + f"#undef {name}\n"
    _one(_findings(tmp_path, cpp=cpp), f"'{name}' is undefined outside Config.h")


def test_reserved_prefix(tmp_path):
    found = _findings(tmp_path, config=None, cpp="", slug="core")
    _one(found, "prefix XEWE_CORE_ collides with XEWE_CORE_")


def test_setting_rows_parser():
    rows = validate.setting_rows(
        GOOD_CPP + 'xewe::setting<&A::b>("on", XEWE_LED_ON, "x, (y)");\n'
        'xewe::setting<&A::s>("s", 31, XEWE_LED_S, "z");\n'
    )
    assert [(k, d) for k, d, _ in rows] == [
        ("voltage", "XEWE_LED_VOLTAGE"),
        ("chip", "XEWE_LED_CHIP"),
        ("on", "XEWE_LED_ON"),
        ("s", "XEWE_LED_S"),
    ]
