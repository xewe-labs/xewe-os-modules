"""Led Modes module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module led-modes --unit-only`.
"""
import re
import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "led_modes"
NAME = "Led Modes"    # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/
EFFECTS_H = MODULE_DIR / "src" / "LedModes" / "Effects.h"

PARAM_RX = re.compile(r"\{\"(\w+)\", \"([^\"]+)\", (\d+), (\d+), (\d+), (\d+), '([ab])'\},")
TABLE_RX = re.compile(r"constexpr ParamDef (\w+)\[\] = \{\n(.*?)\n\};", re.S)
MODE_RX = re.compile(r"\{(\d+), \"([^\"]+)\", (\w+), LED_FX_COUNT_OF\((\w+)\)\},")


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["depends_modules"] == "led-strip"


@pytest.mark.unit
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


@pytest.mark.unit
def test_effects_unit_gpp(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_effects"
    src = MODULE_DIR / "tests" / "unit" / "test_effects.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 40, f"only {summary[1]} checks ran (44 when written); was the test emptied?"


@pytest.mark.unit
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
