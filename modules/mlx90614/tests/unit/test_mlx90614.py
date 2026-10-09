"""MLX90614 module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module mlx90614 --unit-only`.
"""
import re
import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "mlx90614"
NAME = "MLX90614"     # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/mlx90614/, also when run from build/modules/tests/mlx90614/unit/
SRC = MODULE_DIR / "src" / "Mlx90614"
UNIT = MODULE_DIR / "tests" / "unit"


def mlx_celsius(lsb: int, msb: int):
    """Python mirror of mlx90614_fx::raw_to_celsius: None for an error reply."""
    if msb & 0x80:
        return None
    return ((msb << 8) | lsb) * 0.02 - 273.15


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (SRC / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["declare"] == "Mlx90614 mlx90614(os);" and props["depends_modules"] == ""
    assert props["depends_libraries"] == "ArduinoJson"


@pytest.mark.unit
def test_value_mapping():
    assert mlx_celsius(0xFF, 0xFF) is None            # glitched bus: never 1037.5 C
    assert abs(mlx_celsius(0x8D, 0x3A) - 26.63) < 0.01
    assert abs(mlx_celsius(0xFF, 0x7F) - 382.19) < 0.01
    assert max(mlx_celsius(l, m) for m in range(0x80) for l in (0, 0xFF)) < 383   # no valid word reads as > 383 C


@pytest.mark.unit
def test_convert_unit_gpp(tmp_path):
    """tests/unit/test_convert.cpp: raw -> C with the error flag, address validation, scan error streak."""
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_convert"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(UNIT / "test_convert.cpp")],
                   check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    assert int(summary[1]) >= 27


@pytest.mark.unit
def test_source_uses_pure_header():
    """Convert.h stays pure; the .cpp maps values through it and bounds the scan."""
    includes = re.findall(r"#include\s*[<\"]([^>\"]+)", (SRC / "Convert.h").read_text())
    assert includes and all(re.fullmatch(r"c[a-z]+|string", i) for i in includes), includes
    cpp = (SRC / "Mlx90614.cpp").read_text()
    for needle in ("mlx90614_fx::raw_to_celsius(", "mlx90614_fx::parse_address(", "mlx90614_fx::scan_error_streak(",
                   "config.scan_budget_ms", "Wire.setTimeOut(saved_timeout)"):
        assert needle in cpp, needle
    assert "1037" not in cpp.replace("1037 C", "")    # only the comment that explains the error flag
