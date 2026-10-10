"""Fan module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module fan --unit-only`.
"""
import re
import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "fan"
NAME = "Fan"          # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/fan/, also when run from build/modules/tests/fan/unit/
SRC = MODULE_DIR / "src" / "Fan"
UNIT = MODULE_DIR / "tests" / "unit"


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (SRC / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["declare"] == "Fan fan(os);" and props["depends_modules"] == ""
    assert props["depends_libraries"] == "ArduinoJson"


@pytest.mark.unit
def test_curve_unit_gpp(tmp_path):
    """tests/unit/test_curve.cpp: interpolation, clamps, validation, schema, `curve set` parsing,
    hysteresis and colours of src/Fan/Curve.h."""
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_curve"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(UNIT / "test_curve.cpp")],
                   check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 72      # 73 from the pad + 14 `curve set` parsing - 15 hex checks (now core CC3)


@pytest.mark.unit
def test_curve_header_is_pure():
    """Curve.h builds with g++ alone, and Fan.cpp calls it instead of keeping its own copy."""
    includes = re.findall(r"#include\s*[<\"]([^>\"]+)", (SRC / "Curve.h").read_text())
    assert includes and all(re.fullmatch(r"c[a-z]+|string|vector", i) for i in includes), includes
    cpp = (SRC / "Fan.cpp").read_text()
    for needle in ("curve_math::schema_ok(stored.schema)", "curve_math::validate_points(",
                   "curve_math::speed_to_pwm(", "curve_math::target_speed(", "curve_math::parse_curve_spec("):
        assert needle in cpp, needle
    assert "namespace curve_math {" not in cpp


@pytest.mark.unit
def test_r1_fixes_kept():
    """Night-run review R1: 64-bit tach maths, shared-pin refusal, schema mismatch never overwritten."""
    cpp = (SRC / "Fan.cpp").read_text()
    assert "static_cast<uint64_t>(pulses) * 30000u" in cpp
    assert "f->pin_tach == pwm_pin" in cpp and "f->pin_pwm == tach_pin" in cpp
    load = cpp[cpp.index("void Fan::load()"):cpp.index("void Fan::save()")]
    assert "if (foreign)" in load and load.index("if (foreign)") < load.rindex("save();")
    # the curve loop never writes NVS
    run = cpp[cpp.index("void Fan::run_curve"):cpp.index("bool Fan::curve_add")]
    assert "set_all(" in run and ", false)" in run and "save" not in run
