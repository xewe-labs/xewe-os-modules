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

S1 = "/* ---- start default runtime values ---- */"
E1 = "/* ---- end default runtime values ---- */"
S2 = "/* ---- start compile time defined constants ---- */"
E2 = "/* ---- end compile time defined constants ---- */"
OWNERS = validate.config_owners(["led", "led-web", "wifi"])

GOOD_CONFIG = """\
// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Config.h
/*
Fixture.
*/
#pragma once

/* ---- start default runtime values ---- */

/* Chip (led/chip), 0 to 45; asked at first boot */
#ifndef XEWE_LED_CHIP
#define XEWE_LED_CHIP 0
#define XEWE_LED_CHIP_PROMPT 1
#endif

/* Voltage (led/voltage), V */
#ifndef XEWE_LED_VOLTAGE
#define XEWE_LED_VOLTAGE 5
#endif

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* Data pin */
#ifndef XEWE_LED_PIN_DATA
#define XEWE_LED_PIN_DATA 4
#endif

/* ---- end compile time defined constants ---- */
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
    found = _findings(tmp_path, GOOD_CONFIG.replace(S2 + "\n", ""))
    _one(found, f"marker '{S2}' missing")


def test_missing_end(tmp_path):
    found = _findings(tmp_path, GOOD_CONFIG.replace(E1 + "\n", ""))
    _one(found, f"marker '{E1}' missing")


def test_end_before_start(tmp_path):
    config = (
        GOOD_CONFIG.replace(S1, "/* tmp */").replace(E1, S1).replace("/* tmp */", E1)
    )
    _one(_findings(tmp_path, config), "out of order")


def test_markers_out_of_order(tmp_path):
    config = (
        GOOD_CONFIG.replace(S1, "/* tmp */").replace(S2, S1).replace("/* tmp */", S2)
    )
    _one(_findings(tmp_path, config), "out of order")


def test_group_between_end_and_next_start(tmp_path):
    config = GOOD_CONFIG.replace(
        E1 + "\n",
        E1 + "\n\n/* Late */\n#ifndef XEWE_LED_LATE\n#define XEWE_LED_LATE 1\n#endif\n",
    )
    _one(_findings(tmp_path, config), "only blank lines between an end marker")


def test_group_after_the_last_end(tmp_path):
    config = (
        GOOD_CONFIG
        + "\n/* Late */\n#ifndef XEWE_LED_LATE\n#define XEWE_LED_LATE 1\n#endif\n"
    )
    _one(_findings(tmp_path, config), "or after the last end marker")


def test_single_marker_form(tmp_path):
    config = GOOD_CONFIG.replace(S1, "/* ---- default runtime values ---- */").replace(
        E1 + "\n\n", ""
    )
    found = _findings(tmp_path, config)
    _one(found, "single section marker '/* ---- default runtime values ---- */'")
    _one(found, f"expected lines '{S1}', '{E1}', '{S2}', '{E2}'")


def test_old_slash_slash_marker_form(tmp_path):
    config = GOOD_CONFIG.replace(S1, "// ---- default runtime values ----")
    found = _findings(tmp_path, config)
    _one(found, "the '//' section marker form is gone")
    _one(found, f"expected lines '{S1}'")


def test_marker_inside_a_conditional(tmp_path):
    config = GOOD_CONFIG.replace(S2, "#if 1\n" + S2)
    _one(_findings(tmp_path, config), "inside a conditional")


def test_chip_conditional_inside_a_group(tmp_path):
    config = GOOD_CONFIG.replace(
        "#define XEWE_LED_PIN_DATA 4\n",
        "#if defined(XEWE_CHIP_C3)\n#define XEWE_LED_PIN_DATA 4\n#else\n"
        "#define XEWE_LED_PIN_DATA 48\n#endif\n",
    )
    found = _findings(tmp_path, config)
    _one(found, "#if is not allowed")
    _one(found, "#else is not allowed")
    assert not any("unclosed" in f or "without #if" in f for f in found), found


def test_nested_ifndef_inside_a_group(tmp_path):
    config = GOOD_CONFIG.replace(
        "#define XEWE_LED_PIN_DATA 4\n",
        "#ifndef XEWE_LED_OTHER\n#endif\n#define XEWE_LED_PIN_DATA 4\n",
    )
    found = _findings(tmp_path, config)
    _one(found, "nested inside the #ifndef XEWE_LED_PIN_DATA group")
    assert not any("unclosed" in f or "without #if" in f for f in found), found


def test_group_before_the_first_marker(tmp_path):
    config = GOOD_CONFIG.replace(
        "#pragma once\n",
        "#pragma once\n\n/* Early */\n#ifndef XEWE_LED_EARLY\n#define XEWE_LED_EARLY 1\n#endif\n",
    )
    _one(
        _findings(tmp_path, config),
        "#ifndef XEWE_LED_EARLY before the first start marker",
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
        "XEWE_LED_PIN_DATA is not a define of the default runtime values",
    )


def test_rows_without_config(tmp_path):
    _one(_findings(tmp_path, config=None), "settings rows need src/<Folder>/Config.h")


def test_prompt_in_the_compile_time_section(tmp_path):
    config = GOOD_CONFIG.replace(
        "#define XEWE_LED_PIN_DATA 4\n",
        "#define XEWE_LED_PIN_DATA 4\n#define XEWE_LED_PIN_DATA_PROMPT 1\n",
    )
    _one(
        _findings(tmp_path, config),
        "XEWE_LED_PIN_DATA_PROMPT outside the default runtime values section",
    )


def test_empty_sections_pass(tmp_path):
    config = GOOD_CONFIG.split(S1)[0] + f"{S1}\n\n{E1}\n\n{S2}\n\n{E2}\n"
    assert _findings(tmp_path, config, cpp="") == []


def test_slash_slash_comment_line(tmp_path):
    config = GOOD_CONFIG.replace("/* Data pin */", "// Data pin")
    found = _findings(tmp_path, config)
    _one(found, "no '//' line")
    _one(found, "#ifndef XEWE_LED_PIN_DATA needs exactly one single-line")


def test_third_marker(tmp_path):
    config = GOOD_CONFIG + "\n/* ---- first-boot values ---- */\n"
    _one(_findings(tmp_path, config), "unknown section marker")


def test_marker_twice(tmp_path):
    config = GOOD_CONFIG + "\n" + E2 + "\n"
    _one(_findings(tmp_path, config), "appears twice")


def test_multi_line_comment_below_the_header(tmp_path):
    config = GOOD_CONFIG.replace("/* Data pin */", "/* Data\npin */")
    _one(_findings(tmp_path, config), "must be one single-line")


def test_prompt_define_must_be_one(tmp_path):
    config = GOOD_CONFIG.replace("XEWE_LED_CHIP_PROMPT 1", "XEWE_LED_CHIP_PROMPT 0")
    _one(
        _findings(tmp_path, config),
        "XEWE_LED_CHIP_PROMPT must be '#define XEWE_LED_CHIP_PROMPT 1'",
    )


def test_first_boot_never_guarded(tmp_path):
    cpp = GOOD_CPP.replace("#ifdef XEWE_LED_CHIP_PROMPT", "#if 1")
    _one(
        _findings(tmp_path, cpp=cpp),
        "XEWE_LED_CHIP defines XEWE_LED_CHIP_PROMPT but is never asked under #ifdef XEWE_LED_CHIP_PROMPT",
    )


def test_prompt_tested_without_a_prompt_define(tmp_path):
    cpp = GOOD_CPP + "#if defined(XEWE_LED_VOLTAGE_PROMPT)\n#endif\n"
    _one(
        _findings(tmp_path, cpp=cpp),
        "XEWE_LED_VOLTAGE_PROMPT is tested, but no default runtime value",
    )


def test_missing_guard(tmp_path):
    config = GOOD_CONFIG.replace(
        S2 + "\n",
        S2 + "\n\n#define XEWE_LED_BARE 1\n",
    )
    _one(
        _findings(tmp_path, config),
        "#define XEWE_LED_BARE must sit inside #ifndef XEWE_LED_BARE",
    )


def test_comment_line_above_each_ifndef(tmp_path):
    none = GOOD_CONFIG.replace("/* Voltage (led/voltage), V */\n", "")
    _one(
        _findings(tmp_path / "a", none),
        "#ifndef XEWE_LED_VOLTAGE needs exactly one single-line",
    )
    two = GOOD_CONFIG.replace(
        "/* Voltage (led/voltage), V */\n", "/* Voltage */\n/* (led/voltage), V */\n"
    )
    _one(
        _findings(tmp_path / "b", two),
        "#ifndef XEWE_LED_VOLTAGE needs exactly one single-line",
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
        + "\n/* Not ours */\n#ifndef XEWE_LED_WEB_X\n#define XEWE_LED_WEB_X 1\n#endif\n"
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
