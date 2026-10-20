"""Wifi module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module wifi --unit-only`.
"""

import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "wifi"
NAME = "Wifi"  # module.properties name == C++ name argument
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
def test_rows_take_the_config_defaults():
    cpp = (MODULE_DIR / "src" / "Wifi" / "Wifi.cpp").read_text()
    assert '"ssid", 32, XEWE_WIFI_SSID,' in cpp
    assert (
        '"psw", 63, XEWE_WIFI_PSW, "Network password", xewe::SettingDef::SECRET' in cpp
    )


@pytest.mark.unit
def test_first_boot_pre_answers_gpp(tmp_path):
    """Default build: the scan flow (connect). SSID and password set: stored, joined without a scan,
    the scan only as the fallback. SSID alone: only the password is asked. Password alone: ignored."""
    source = MODULE_DIR / "src" / "Wifi" / "Wifi.cpp"

    def init(defines):
        out = _preprocessed(tmp_path, source, defines)
        return _body(
            out,
            "void Wifi::begin_routines_init()",
            "void Wifi::begin_routines_regular()",
        )

    default = init({})
    assert "connect(true)" in default and "Password: " not in default
    assert 'apply_setting("ssid"' not in default

    both = init({"XEWE_WIFI_SSID": '"x"', "XEWE_WIFI_PSW": '"y"'})
    assert (
        'apply_setting("ssid", "x");' in both and 'apply_setting("psw", "y");' in both
    )
    assert "attempt < 3" in both and "join(stored_ssid, stored_psw, 10000, 1)" in both
    assert "connect(true)" not in both and "Password: " not in both
    assert (
        both.index("join(")
        < both.index("! Wifi: unable to join ")
        < both.index("select_network()")
    )

    ssid = init({"XEWE_WIFI_SSID": '"x"'})
    assert "Password: " in ssid and 'join("x", pwd, 10000, 1)' in ssid
    assert "connect(true)" not in ssid

    assert init({"XEWE_WIFI_PSW": '"y"'}) == default
