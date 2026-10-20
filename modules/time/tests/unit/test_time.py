"""Time module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module time --unit-only`.
"""

import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "time"
NAME = "Time"  # module.properties name == C++ name argument
MODULE_DIR = module_dir(
    __file__
)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/


@pytest.mark.unit
def test_properties_match_source():
    props = dict(
        l.split("=", 1)
        for l in (MODULE_DIR / "module.properties").read_text().splitlines()
        if "=" in l
    )
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp


def _preprocessed(tmp_path, source, defines):
    """`source` through g++ -E with Config.h forced first and `defines` as -D values, the way a
    build with those values pre-answered sees it; #include lines are dropped (nothing else needed)."""
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    text = "\n".join(
        l
        for l in source.read_text().splitlines()
        if not l.lstrip().startswith("#include")
    )
    src = tmp_path / source.name
    src.write_text(text)
    cmd = [gpp, "-E", "-P", "-x", "c++", "-include", str(source.parent / "Config.h")]
    cmd += [f"-D{k}={v}" for k, v in defines.items()] + [str(src)]
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def _body(text, start, end):
    return text[text.index(start) : text.index(end)]


@pytest.mark.unit
def test_row_takes_the_config_default():
    cpp = (MODULE_DIR / "src" / "Time" / "Time.cpp").read_text()
    assert '"tz_gmt_str", 9, XEWE_TIME_TZ_GMT_STR,' in cpp


@pytest.mark.unit
def test_timezone_pre_answer_gpp(tmp_path):
    """Default build: detection and confirmation. XEWE_TIME_TZ_GMT_STR set: no detection and no
    question, the value applied and NTP synced; an unparsable value falls back to the typed one."""
    source = MODULE_DIR / "src" / "Time" / "Time.cpp"

    def init(defines):
        out = _preprocessed(tmp_path, source, defines)
        return _body(
            out,
            "void Time::begin_routines_init()",
            "void Time::begin_routines_regular()",
        )

    default = init({})
    assert "Detecting Timezone..." in default and "Is your time:" in default

    pre = init({"XEWE_TIME_TZ_GMT_STR": '"GMT+01:00"'})
    assert "Detecting Timezone..." not in pre and "Is your time:" not in pre
    assert 'parse_gmt_offset("GMT+01:00", build_gmt)' in pre
    assert pre.index('apply_setting("tz_gmt_str", build_gmt);') < pre.index(
        "sync_and_print();"
    )
    assert pre.index("sync_and_print();") < pre.index("Enter your timezone offset")
