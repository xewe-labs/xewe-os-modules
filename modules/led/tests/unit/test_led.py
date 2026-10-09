"""Led module unit tests: pure logic on the developer machine, no board and no build.
Run through a harness: `python -m xewe test --module led --unit-only`.
"""
import re
import shutil
import subprocess

import pytest

from xewe.testing import module_dir

ID = "led"
NAME = "Led"          # module.properties name == C++ name argument
MODULE_DIR = module_dir(__file__)  # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/
SRC = MODULE_DIR / "src" / "Led"
MODES_DIR = SRC / "modes"
UNIT = MODULE_DIR / "tests" / "unit"

PARAM_RX = re.compile(r"\{\"(\w+)\", \"([^\"]+)\", (\d+), (\d+), (\d+), (\d+), '([ab])'\},")
TABLE_RX = re.compile(r"inline constexpr ParamDef PARAMS\[\] = \{\n(.*?)\n\};", re.S)
MODEDEF_RX = re.compile(
    r"inline constexpr ModeDef (MODE_\w+) = \{(\d+), \"([^\"]+)\", (\w+)::PARAMS, LED_FX_COUNT_OF\((\w+)::PARAMS\),")
STD_HEADERS = {"cmath", "cstddef", "cstdint", "cstring", "vector", "string", "array", "algorithm"}


def _run_gpp(tmp_path, name: str) -> int:
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / name
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(UNIT / f"{name}.cpp")],
                   check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    summary = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert summary, run.stdout
    return int(summary[1])


def _mode_files() -> list:
    return sorted(p for p in MODES_DIR.glob("*.h") if p.name not in ("Mode.h", "Registry.h"))


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (SRC / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["depends_libraries"] == "FastLED" and props["depends_modules"] == ""
    assert props["declare"] == "Led led(os);"


@pytest.mark.unit
def test_chipset_table_matches_add_leds():
    table = re.findall(r"\{\s*(\d+),\s*\"(\w+)\",\s*(true|false)\s*\}", (SRC / "Chipsets.h").read_text())
    ids = [int(i) for i, _, _ in table]
    assert ids and len(ids) == len(set(ids)) and all(0 <= i <= 45 for i in ids)   # 2.3.x id range
    cpp = (SRC / "Led.cpp").read_text()
    cases = re.findall(r"case (\d+):\s+c = &FastLED\.addLeds<(\w+),", cpp)
    assert sorted((int(i), n) for i, n in cases) == sorted((int(i), n) for i, n, _ in table)
    for _, name, clocked in table:   # a clocked chip gets the clock pin template argument
        line = next(l for l in cpp.splitlines() if f"addLeds<{name}," in l)
        assert ("LED_PIN_CLOCK" in line) == (clocked == "true"), name


@pytest.mark.unit
def test_effects_unit_gpp(tmp_path):
    # registry lookups, every mode's frames (pinned CRC per mode id), maths, cross-fade, seeds
    count = _run_gpp(tmp_path, "test_effects")
    assert count >= 56, f"only {count} checks ran (56 when written, 44 before the split); was the test emptied?"


@pytest.mark.unit
def test_listener_fanout_unit_gpp(tmp_path):
    # LedListener.h: fixed 4-slot set, no duplicates, registration order, origin echo suppression,
    # default no-op bodies
    count = _run_gpp(tmp_path, "test_listeners")
    assert count >= 28, f"only {count} checks ran (28 when written); was the test emptied?"


@pytest.mark.unit
def test_registry_consistent():
    # LM13/LM14: one file per mode, Registry.h is the one list; ids and names unique, keys fit NVS
    registry = (MODES_DIR / "Registry.h").read_text()
    includes = re.findall(r'^#include "(\w+\.h)"', registry, re.M)
    rows = re.findall(r"^\s+(MODE_\w+),\s*$", re.search(r"MODES\[\] = \{\n(.*?)\n\};", registry, re.S)[1], re.M)
    files = _mode_files()
    assert files, "no mode files"
    assert includes[0] == "Mode.h" and sorted(includes[1:]) == sorted(f.name for f in files), "include per mode file"
    assert len(rows) == len(set(rows)), "a mode listed twice"
    max_params = int(re.search(r"MAX_PARAMS\s*=\s*(\d+)", (MODES_DIR / "Mode.h").read_text())[1])
    seen_ids, seen_names, ids_by_name, consts = {}, {}, {}, []
    for f in files:
        text = f.read_text()
        defs = MODEDEF_RX.findall(text)
        assert len(defs) == 1, f"{f.name}: exactly one `inline constexpr ModeDef MODE_<NAME> = {{<id>, \"<name>\", ...`"
        const, mode_id, name, ns, ns2 = defs[0]
        mode_id = int(mode_id)
        assert ns == ns2 and re.search(rf"^namespace {ns} \{{", text, re.M), f.name
        assert 0 <= mode_id <= 99, f"{f.name}: id {mode_id} outside 0..99 (NVS key length)"
        assert mode_id not in seen_ids, f"{f.name}: id {mode_id} also used by {seen_ids.get(mode_id)}"
        assert name not in seen_names, f"{f.name}: name '{name}' also used by {seen_names.get(name)}"
        seen_ids[mode_id], seen_names[name] = f.name, f.name
        ids_by_name[name] = mode_id
        consts.append(const)
        body = TABLE_RX.search(text)
        assert body, f"{f.name}: no PARAMS table"
        params = PARAM_RX.findall(body[1])
        assert len(params) == len([l for l in body[1].splitlines() if l.strip()]), f"{f.name}: unparsed row"
        assert 0 < len(params) <= max_params, f.name
        assert len({p[0] for p in params}) == len(params), f"{name}: duplicate key"
        for key, _display, lo, hi, default, step, _type in params:
            lo, hi, default, step = int(lo), int(hi), int(default), int(step)
            assert lo <= default <= hi <= 65535, f"{name}.{key}"
            assert step >= 1, f"{name}.{key}"
            assert len(f"m:{mode_id}:{key}") <= 15, f"NVS key m:{mode_id}:{key} longer than 15"
    assert sorted(rows) == sorted(consts), "every mode file listed once in MODES[] (and nothing else)"
    # the ids of 2.3.x are kept (LM14)
    assert ids_by_name == {"Solid": 0, "Color Fade": 1, "Color Fade Two Zone": 2, "Brightness Fade": 3,
                       "Pulse": 4, "Rainbow": 5, "Christmas Lights": 6}


@pytest.mark.unit
def test_no_positional_mode_lookup():
    # LM14: modes are found by id (find_mode); MODES[...] appears only in Registry.h (default_mode = first row)
    for f in sorted(SRC.rglob("*")):
        if f.is_file() and f.name != "Registry.h":
            assert not re.search(r"\bMODES\[[^\]]", f.read_text()), f"{f.relative_to(SRC)} indexes MODES[]"
    assert re.findall(r"\bMODES\[[^\]]+\]", (MODES_DIR / "Registry.h").read_text()) == ["MODES[0]"]


@pytest.mark.unit
def test_mode_headers_pure():
    # fx/, modes/, Pixel.h and LedListener.h compile on the host: standard headers and each other only
    pure = [*sorted((SRC / "fx").glob("*.h")), *sorted(MODES_DIR.glob("*.h")), SRC / "Pixel.h", SRC / "LedListener.h"]
    for f in pure:
        for inc in re.findall(r'^#include\s+([<"][^>"]+[>"])', f.read_text(), re.M):
            if inc.startswith("<"):
                assert inc[1:-1] in STD_HEADERS, f"{f.name}: {inc} is not a standard header"
            else:
                target = (f.parent / inc[1:-1]).resolve()
                assert target in {p.resolve() for p in pure}, f"{f.name}: {inc} is not a pure header"


@pytest.mark.unit
def test_setters_notify_listeners():
    # listeners are told after the change, outside the render mutex, and never from the render task
    cpp = (SRC / "Led.cpp").read_text()
    for name, cb in (("set_brightness", "on_brightness"), ("set_state", "on_state")):
        body = re.search(rf"void Led::{name}\([^)]*const void\* origin\) \{{(.*?)\n\}}", cpp, re.S)
        assert body, f"{name} has no origin parameter"
        text = body[1]
        assert f"l.{cb}(" in text, f"{name} does not call {cb}"
        assert text.rfind("LockGuard") < text.find("listeners.notify"), f"{name} notifies under the lock"
    render = "".join(re.search(rf"void Led::{fn}\([^)]*\) \{{(.*?)\n\}}", cpp, re.S)[1]
                     for fn in ("render_task", "render_frame", "render_modes"))
    assert "notify" not in render, "render task must not notify"
    assert "nvs" not in render and "serial" not in render, "render task must not touch NVS or serial"
    header = (SRC / "Led.h").read_text()
    assert '#include "LedListener.h"' in header and '#include "modes/Registry.h"' in header
    assert re.search(r"LED_LISTENERS_MAX 4\b", (SRC / "LedListener.h").read_text())


@pytest.mark.unit
def test_mode_api_for_led_web():
    # LM17/LM18: the names the project-local LedWeb calls (led.<name>); setters take an origin
    h = (SRC / "Led.h").read_text()
    for decl in ("set_mode", "set_param", "set_color", "set_speed", "reset_params", "set_brightness", "set_state"):
        assert re.search(rf"\b{decl}\s*\([^;]*const void\* origin = nullptr\);", h, re.S), decl
    for getter in ("get_brightness", "get_state", "get_length", "get_fps", "get_mode", "get_param", "get_color",
                   "add_listener", "remove_listener", "fill", "clear_fill", "get_frame_checksum", "notify_listeners"):
        public = h.split("public:", 1)[1].split("private:", 1)[0]
        assert re.search(rf"\b{getter}\s*\(", public), f"{getter} not public"
    assert "LedFrameSource" not in h and "set_frame_source" not in h, "frame source is internal (LM17)"
    cpp = (SRC / "Led.cpp").read_text()
    body = re.search(r"bool Led::reset_params\(int mode_id, const void\* origin\) \{(.*?)\n\}", cpp, re.S)[1]
    assert body.count("activate(") == 1 and body.count("persist_params(") == 1 and "set_param(" not in body
    assert "default_params(" in body and "notify_params(" in body
    assert "l.on_mode(" in cpp and "l.on_color(" in cpp and "l.on_param(" in cpp


@pytest.mark.unit
def test_command_table():
    # LM15: the `$help` table as registered: strip commands, `mode` per arg count, 2.3.x aliases;
    # names fit the core's 15-character limit; every `mode` subcommand is dispatched
    cpp = (SRC / "Led.cpp").read_text()
    table = re.findall(r'register_command\(\{"(\w+)", "[^"]*", "([^"]*)", (\d+),', cpp)
    cmds = {(n, int(a)) for n, _, a in table}
    assert len(cmds) == len(table), "a (name, arg count) pair registered twice"
    assert all(len(n) <= 15 for n, _ in cmds)
    assert {("on", 0), ("off", 0), ("brightness", 1), ("set", 2), ("fill", 1), ("checksum", 0)} <= cmds
    assert {("mode", 1), ("mode", 2), ("mode", 4)} <= cmds
    assert {("set_mode", 1), ("set_mode_param", 3)} <= cmds
    assert {("set_brightness", 1), ("set_state", 1), ("toggle_state", 0), ("turn_on", 0), ("turn_off", 0),
            ("set_length", 1), ("set_color_order", 1)} <= cmds
    for name, usage, argc in table:   # sample usage has the registered number of arguments
        assert usage.split()[:2] == ["$led", name] and len(usage.split()) - 2 == int(argc), usage
    dispatch = re.search(r"void Led::cli_mode\(.*?\n\}", cpp, re.S)[0]
    for argc, subs in ((1, ("list", "color", "resetparams")), (2, ("set", "color", "resetparams", "speed")),
                       (4, ("param",))):
        case = re.search(rf"case {argc}:(.*?)break;\n", dispatch, re.S)[1]
        for sub in subs:
            assert f'sub == "{sub}"' in case, f"`$led mode {sub}` with {argc} argument(s) not dispatched"
