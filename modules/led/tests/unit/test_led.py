# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
"""Led module unit tests: source checks, the C++ engine tests (g++ with FastLED's stub platform)
and the golden-frame comparison with the xewe-led-os 2.3.0 reference. Developer machine only, no
board and no firmware build. Run through a harness: `python -m xewe test --module led --unit-only`.

The reference values of the command table, the wizard and the mode parameters are pinned as
literals here and in test_led_engine.cpp, so these tests need nothing outside the module.
The C++ tests need FastLED 3.10.3 sources and XeWeCore's src/ (host-includable headers):
XEWE_FASTLED_SOURCE / XEWE_CORE_SOURCE, else the harness's build/libraries/{FastLED,XeWeCore}.
The golden-frame tests need the oracle folder (2.3.0 sources, harness and expected frames) in
XEWE_LED_GOLDEN_DIR; without it they skip and say so. With it, the pinned literals are also
compared with the 2.3.0 sources in that folder.
"""

from __future__ import annotations

import ast
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

try:
    from xewe.testing import module_dir

    MODULE_DIR = module_dir(__file__)
except ImportError:  # standalone run: python tests/unit/test_led.py
    MODULE_DIR = Path(__file__).resolve().parents[2]

ID = "led"
NAME = "Led"
SRC = MODULE_DIR / "src" / "Led"
UNIT = MODULE_DIR / "tests" / "unit"

# ---------------------------------------------------------------------------------------------
# Locating the libraries
# ---------------------------------------------------------------------------------------------


def _harness_libraries() -> list[Path]:
    """build/libraries of the xewe project this test runs in (or the dev project next to us)."""
    out = []
    for p in [Path(__file__).resolve(), *Path(__file__).resolve().parents]:
        if (p / "build" / "libraries").is_dir():
            out.append(p / "build" / "libraries")
    sibling = MODULE_DIR.parents[2] / "xewe-led-dev" / "build" / "libraries"
    if sibling.is_dir():
        out.append(sibling)
    return out


def _find_core_src() -> Path | None:
    env = os.environ.get("XEWE_CORE_SOURCE")
    candidates = [Path(env) / "src", Path(env)] if env else []
    candidates.append(MODULE_DIR.parents[2] / "xewe-os-core" / "src")
    candidates += [lib / "XeWeCore" / "src" for lib in _harness_libraries()]
    for c in candidates:
        if (c / "XeWeCore" / "Utils" / "Color.h").is_file():
            return c
    return None


def _find_fastled_src() -> Path | None:
    env = os.environ.get("XEWE_FASTLED_SOURCE")
    candidates = [Path(env) / "src", Path(env)] if env else []
    candidates += [lib / "FastLED" / "src" for lib in _harness_libraries()]
    golden = _find_golden()
    if golden:
        candidates.append(golden / "third_party" / "FastLED" / "src")
    for c in candidates:
        if (c / "FastLED.h").is_file() and (c / "platforms" / "stub").is_dir():
            return c
    return None


GOLDEN_SKIP = (
    "needs the 2.3.0 golden oracle: set XEWE_LED_GOLDEN_DIR to its folder (README.md, "
    "expected/, harness/, legacy_src/)"
)


def _find_golden() -> Path | None:
    env = os.environ.get("XEWE_LED_GOLDEN_DIR")
    if (
        env
        and (Path(env) / "README.md").is_file()
        and (Path(env) / "expected" / "MANIFEST.json").is_file()
    ):
        return Path(env)
    return None


def _legacy_led_strip() -> Path:
    return _find_golden() / "legacy_src" / "src" / "Modules" / "Hardware" / "LedStrip"


def _cache_dir(request, name: str) -> Path:
    try:
        return Path(request.config.cache.mkdir(name))
    except Exception:  # noqa: BLE001 - standalone run
        p = Path(os.environ.get("TMPDIR", "/tmp")) / f"xewe-led-{name}"
        p.mkdir(parents=True, exist_ok=True)
        return p


# ---------------------------------------------------------------------------------------------
# Host build of FastLED (stub platform) and the module engine
# ---------------------------------------------------------------------------------------------

# Same flags as the golden oracle (README "Exact compile commands"): IEEE single rounding per
# operation (no FMA contraction) so float results equal the ESP32-C3/C6 soft-float build.
COMMON = [
    "-std=gnu++20",
    "-O2",
    "-g0",
    "-ffp-contract=off",
    "-fno-fast-math",
    "-Wno-volatile",
    "-DFASTLED_STUB_IMPL",
]
ENGINE_SOURCES = [
    "Brightness.cpp",
    "LedCommands.cpp",
    "LedEngine.cpp",
    "ModeController.cpp",
    "ModeRegistry.cpp",
    "modes/Mode.cpp",
    "modes/Solid.cpp",
    "modes/FadeColor.cpp",
    "modes/FadeColorTwoZone.cpp",
    "modes/FadeBrightness.cpp",
    "modes/Pulse.cpp",
    "modes/Rainbow.cpp",
    "modes/ChristmasLights.cpp",
]


def _gpp() -> str:
    gpp = shutil.which(os.environ.get("CXX", "g++"))
    if gpp is None:
        pytest.skip("g++ not installed")
    return gpp


def _digest(*parts) -> str:
    h = hashlib.sha256()
    for p in parts:
        if isinstance(p, Path) and p.is_dir():
            for f in sorted(x for x in p.rglob("*") if x.is_file()):
                h.update(str(f.relative_to(p)).encode())
                h.update(f.read_bytes())
        elif isinstance(p, Path):
            h.update(p.read_bytes())
        else:
            h.update(str(p).encode())
    return h.hexdigest()[:16]


def _compile_all(jobs: list[tuple[list[str], Path]]) -> None:
    def one(job):
        cmd, obj = job
        if obj.is_file():
            return None
        r = subprocess.run(
            cmd + ["-o", str(obj) + ".tmp"], capture_output=True, text=True
        )
        if r.returncode != 0:
            return " ".join(cmd) + "\n" + r.stderr[-4000:]
        os.replace(str(obj) + ".tmp", obj)
        return None

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        errors = [e for e in ex.map(one, jobs) if e]
    assert not errors, errors[0]


def _fastled_objects(cache: Path, fastled_src: Path, patches: list[Path]) -> list[Path]:
    """FastLED compiled for its stub platform (no shim includes), once per source/patch set."""
    gpp = _gpp()
    key = _digest(fastled_src, *patches, gpp, *COMMON)
    root = cache / f"fastled-{key}"
    tree = root / "src"
    if not tree.is_dir():
        tmp = root / "tree.tmp"
        shutil.rmtree(tmp, ignore_errors=True)
        shutil.copytree(fastled_src, tmp / "src")
        for p in patches:
            subprocess.run(
                ["patch", "-s", "-d", str(tmp), "-p1", "-i", str(p)], check=True
            )
        os.replace(tmp / "src", tree)
    objdir = root / "obj"
    objdir.mkdir(exist_ok=True)
    jobs = []
    for f in sorted(tree.rglob("*.cpp")):
        rel = f.relative_to(tree)
        if str(rel) == "platforms/stub/time_stub.cpp":
            continue  # the clock comes from the test's own millis()
        obj = objdir / (str(rel).replace("/", "_") + ".o")
        jobs.append(([gpp, *COMMON, "-w", f"-I{tree}", "-c", str(f)], obj))
    _compile_all(jobs)
    return [j[1] for j in jobs]


# ---------------------------------------------------------------------------------------------
# Source checks
# ---------------------------------------------------------------------------------------------


@pytest.mark.unit
def test_properties_match_source():
    props = dict(
        l.split("=", 1)
        for l in (MODULE_DIR / "module.properties").read_text().splitlines()
        if "=" in l
    )
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
    assert props["depends_libraries"] == "FastLED"


@pytest.mark.unit
def test_every_source_file_is_gpl():
    files = [f for f in (MODULE_DIR / "src").rglob("*") if f.is_file()]
    files += [f for f in UNIT.iterdir() if f.suffix in (".cpp", ".h", ".py")]
    files += list((MODULE_DIR / "tests" / "board").glob("*.py"))
    for f in files:
        head = f.read_text().splitlines()[:3]
        assert any("SPDX-License-Identifier: GPL-3.0-only" in line for line in head), f


@pytest.mark.unit
def test_one_file_pair_per_mode_and_registry_ids():
    names = [
        "Solid",
        "FadeColor",
        "FadeColorTwoZone",
        "FadeBrightness",
        "Pulse",
        "Rainbow",
        "ChristmasLights",
    ]
    for n in names:
        assert (SRC / "modes" / f"{n}.h").is_file() and (
            SRC / "modes" / f"{n}.cpp"
        ).is_file(), n
    reg = (SRC / "ModeRegistry.cpp").read_text()
    ids = re.findall(r"\{(\d), factory_of<(\w+)>\(\)\}", reg)
    assert ids == [(str(i), n) for i, n in enumerate(names)]
    for i, n in enumerate(names):
        assert re.search(
            rf'ModeConfig\({i}, "', (SRC / "modes" / f"{n}.cpp").read_text()
        ), n


@pytest.mark.unit
def test_chipset_switch_covers_the_table():
    cpp = (SRC / "Led.cpp").read_text()
    cases = re.findall(
        r"case LEDChipset::(\w+):\s*FastLED\.addLeds<(\w+),\s*D(,\s*C)?(,\s*RGB)?>", cpp
    )
    table = re.findall(
        r'\{(\d+), LEDChipset::(\w+), "(\w+)", (true|false)\}',
        (SRC / "Chipsets.h").read_text(),
    )
    assert len(table) == 46 and [int(t[0]) for t in table] == list(range(46))
    assert [c[0] for c in cases] == [t[1] for t in table]
    for (enum, tmpl, clock, order), (_, _, name, clocked) in zip(cases, table):
        assert bool(clock) == (clocked == "true"), name
        assert bool(order) == (enum != "NEOPIXEL"), name  # NEOPIXEL fixes GRB itself
    assert sum(t[3] == "true" for t in table) == 14


@pytest.mark.unit
def test_source_comments_describe_the_code():
    """Comments explain behaviour; where a line comes from lives outside the code."""
    for f in [*SRC.rglob("*.h"), *SRC.rglob("*.cpp")]:
        text = f.read_text()
        for word in (
            "PORT-FIX",
            "PORT-QUIRK",
            "DECISIONS",
            "migration lead",
            "golden",
            "oracle",
            "2.3.0",
        ):
            assert word not in text, (f.name, word)


# The 30 commands of xewe-led-os 2.3.0 (name, description, sample usage after "$led", argument
# count), in table order. Buttons and schedules store these strings, so they never change.
COMMANDS_2_3_0 = [
    ("set_rgb", "Set RGB color", " set_rgb 255 0 0", "3"),
    ("set_r", "Set red channel", " set_r 127", "1"),
    ("set_g", "Set green channel", " set_g 255", "1"),
    ("set_b", "Set blue channel", " set_b 200", "1"),
    ("adj_rgb", "Adjust RGB color by deltas", " adj_rgb 10 -20 5", "3"),
    ("adj_r", "Adjust red channel by delta", " adj_r -10", "1"),
    ("adj_g", "Adjust green channel by delta", " adj_g 15", "1"),
    ("adj_b", "Adjust blue channel by delta", " adj_b -5", "1"),
    ("set_hsv", "Set HSV color", " set_hsv 75 255 0", "3"),
    ("set_hue", "Set hue channel", " set_hue 255", "1"),
    ("set_sat", "Set saturation channel", " set_sat 0", "1"),
    ("set_val", "Set value channel", " set_val 255", "1"),
    ("adj_hsv", "Adjust HSV color by deltas", " adj_hsv 10 -20 5", "3"),
    ("adj_hue", "Adjust hue channel by delta", " adj_h -10", "1"),
    ("adj_sat", "Adjust saturation channel by delta", " adj_s 15", "1"),
    ("adj_val", "Adjust value channel by delta", " adj_v -5", "1"),
    ("set_brightness", "Set global brightness", " set_brightness 255", "1"),
    ("adj_brightness", "Adjust global brightness by delta", " adj_brightness -10", "1"),
    ("set_state", "Set on/off state", " set_state 0", "1"),
    ("toggle_state", "If off->on, if on->off", " toggle_state", "0"),
    ("turn_on", "Turn strip on", " turn_on", "0"),
    ("turn_off", "Turn strip off", " turn_off", "0"),
    ("set_mode", "Set LED strip mode", " set_mode 0", "1"),
    ("adj_mode", "Adjust mode by delta (next/prev)", " adj_mode 1", "1"),
    (
        "set_mode_param",
        "Set a parameter for the current mode",
        " set_mode_param hue 128",
        "2",
    ),
    (
        "adj_mode_param",
        "Adjust a parameter for the current mode by delta",
        " adj_mode_param hue -15",
        "2",
    ),
    ("get_mode_params", "Get parameters of the current mode", " get_mode_params", "0"),
    (
        "reset_current_mode",
        "Reset current mode parameters to defaults",
        " reset_current_mode",
        "0",
    ),
    ("set_length", "Set new number of LEDs", " set_length 500", "1"),
    (
        "set_color_order",
        "Set LED color order (RGB/RBG/GRB/GBR/BRG/BGR)",
        " set_color_order GRB",
        "1",
    ),
]


@pytest.mark.unit
def test_command_table_equals_2_3_0():
    """LedCommands.cpp against the pinned 2.3.0 table: same order, names, descriptions, sample
    usages, argument counts. With XEWE_LED_GOLDEN_DIR, the pinned table is also read back from the
    2.3.0 LedStrip.cpp (the oracle's byte-identical copy of 91bd344)."""
    port = re.findall(
        r'\{\s*"(\w+)",\s*"([^"]*)",\s*"\$led([^"]*)",\s*(\d+),',
        (SRC / "LedCommands.cpp").read_text(),
    )
    assert port == COMMANDS_2_3_0
    if _find_golden() is not None:
        legacy = (_legacy_led_strip() / "LedStrip.cpp").read_text()
        old = re.findall(
            r'Command\{\s*"(\w+)",\s*"([^"]*)",\s*std::string\("\$"\) \+ id \+ "([^"]*)",\s*(\d+),',
            legacy,
        )
        assert old == COMMANDS_2_3_0


@pytest.mark.unit
def test_reset_clears_mode_params_and_stops_output():
    """`$led reset`: the mode parameters live in the led namespace (wiped by Module::reset) and the
    RAM copy is cleared first; the render task stops before the pins are released."""
    cpp = (SRC / "Led.cpp").read_text()
    body = cpp[cpp.index("void Led::reset(") : cpp.index("std::string Led::status(")]
    order = [
        "engine.sync_all(",
        "mode_params.params.clear();",
        "stop_render_task();",
        "release_pins();",
        "Module::reset(verbose, do_restart, keep_enabled);",
    ]
    assert [body.index(s) for s in order] == sorted(body.index(s) for s in order)
    assert 'os.nvs.write_flex(led.id, "mode_params", led.mode_params);' in cpp
    assert 'os.nvs.read_flex(id, "mode_params", stored) && stored.has("schema")' in cpp
    # the task ends its loop on the flag and signals; reset waits for it
    task = cpp[
        cpp.index("void Led::render_task()") : cpp.index("void Led::count_frame()")
    ]
    assert (
        "if (render_task_stop)" in task and "xSemaphoreGive(render_task_ended);" in task
    )


# The first-boot wizard of 2.3.0, in source order: console calls (prompt text, menu, get_int bounds),
# stored keys (literal values only), strip calls and waits of the colour-order calibration, and the
# point where the found order is applied. Differences from the 2.3.0 source: the parallel-lines
# branch stores num_led (the longest line), and the calibration first sets the plain RGB order, so
# calibrating an already calibrated strip finds its order again. The chip registration failure
# message moved to Led::start_strip (checked below).
WIZARD_2_3_0 = [
    (
        "print",
        "Guide: https://github.com/xewe-labs/xewe-os-modules/blob/main/modules/led/README.md",
    ),
    ("get_yn", "Is LED data line connected to pin GPIO_{}?"),
    (
        "print_header",
        "Pins can not be changed after upload. \\nOptions:\\\\sep1. Upload the version with the "
        "correct pin\\\\sep2. If there is no compiled version with pin that you need, set pins in Config.h and "
        "compile yourself",
    ),
    ("get_yn", "Are you using LED with CLK line?"),
    ("store", "use_clk", "true"),
    ("get_yn", "Is LED CLK line connected to pin GPIO_{}?"),
    (
        "print_header",
        "Pins can not be changed after upload.\\nOptions:\\\\sep1. Upload the version with the "
        "correct pin\\\\sep2. If there is no compiled version with pin that you need, set pins in Config.h and "
        "compile yourself",
    ),
    ("store", "use_clk", "false"),
    ("get_menu_choice", "What is your LED Chip?", "chipset_names"),
    ("store", "chip", "{}"),
    ("get_menu_choice", "What is LED Voltage?", '{"5V", "12V", "24V"}'),
    ("store", "voltage", "5"),
    ("store", "voltage", "12"),
    ("store", "voltage", "24"),
    ("get_yn", "Do you have parallel LED strips attached to data line?"),
    ("get_int", "How many LED strips do you have in parallel?", "1, MAX"),
    ("store", "lines", "{}"),
    ("get_int", "How many LEDs are connected on parallel line #{}?", "1, MAX"),
    ("store", "l_<i>_cnt", "{}"),
    ("store", "num_led", "{}"),  # the longest line (not stored by 2.3.0)
    ("store", "lines", "0"),
    ("get_int", "How many LEDs do you have connected?", "0, MAX"),
    ("store", "num_led", "{}"),
    ("order_reset",),  # the test colours in plain RGB order (not in 2.3.0)
    ("set_rgb", "{0, 255, 0}"),
    ("set_brightness", "50"),
    ("set_mode", "0"),
    ("turn_on", ""),
    ("print_header", "Color Order Calibration"),
    ("wait",),
    (
        "get_menu_choice",
        "What color are LEDs now?",
        '{"Red", "Green", "Blue", "Other"}',
    ),
    ("print_header", "Double check pins and LED chip type.\\nRGBW is not supported."),
    ("restart",),
    ("print", "Changing color"),
    ("set_rgb", "{255, 0, 0}"),
    ("wait",),
    ("get_menu_choice", "What color are LEDs now?", '{"Red", "Green", "Blue"}'),
    ("order_applied",),  # at the break: the fade-out below already uses the new order
    ("print", "Incorrect entries. Let's try again"),
    ("turn_off", ""),
    ("print", "Setting color order"),
    ("wait",),
    ("turn_on", ""),
    ("set_rgb", "{0, 255, 0}"),
    ("store", "colorder", "{}"),
    ("print", "Color order set to {}"),
    ("print", "LED setup success!"),
]


def _args(text, open_idx):
    """Text between the parenthesis at open_idx and its match (string literals respected)."""
    depth, i, out = 0, open_idx, []
    while i < len(text):
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
            continue
        if c == "(":
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif c == ")":
            depth -= 1
            if depth == 0:
                return "".join(out)
        out.append(c)
        i += 1
    raise ValueError("unbalanced")


def _split(args):
    parts, depth, cur, i = [], 0, "", 0
    while i < len(args):
        c = args[i]
        if c == '"':
            j = i + 1
            while args[j] != '"':
                j += 2 if args[j] == "\\" else 1
            cur += args[i : j + 1]
            i = j + 1
            continue
        if c in "({":
            depth += 1
        if c in ")}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
        i += 1
    parts.append(cur)
    return [p.strip() for p in parts]


def _text(arg):
    """A message argument: adjacent literals joined, anything computed between them as {}."""
    out, last = "", 0
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', arg):
        gap = arg[last : m.start()]
        if out and re.search(r"\w", gap):
            out += "{}"
        out += m[1]
        last = m.end()
    if re.search(r"\w", arg[last:].replace(".c_str()", "")):
        out += "{}"
    return out


def _norm(arg):
    arg = re.sub(r"\s+", " ", arg)
    arg = arg.replace("XEWE_LED_NUM_LEDS_MAX", "MAX").replace(
        "LED_STRIP_NUM_LEDS_MAX", "MAX"
    )
    return arg


def _value(arg):
    arg = arg.strip().strip('"')
    return arg if re.fullmatch(r"\d+|true|false", arg) else "{}"


CALL = re.compile(
    r"(?:os\.serial|controller\.serial_port)\.(get_yn|get_menu_choice|get_int|print_header|print)\(|"
    r"(?:os\.nvs|controller\.nvs)\.write<\w+>\(|apply_setting\(|"
    r"(?<![\w.])(?:engine\.)?(set_rgb|set_brightness|set_mode|turn_on|turn_off)\(|"
    r"(run_with_dots|wait_with_dots)\(|(?:os|controller)\.system\.restart\(\)|"
    r"engine\.set_color_order_index\((?:color_order_index|0)\)|"
    r"color_order_index = static_cast<uint8_t>\(it - color_orders\.begin\(\)\)|"
    r"(?<![\w.:])(set_color_order|calibrate_color_order)\(\)"
)


def _events(body, splice=None):
    out = []
    for m in CALL.finditer(body):
        s = m[0]
        if s.endswith("restart()"):
            out.append(("restart",))
            continue
        if "color_order_index" in s:
            out.append(("order_reset",) if s.endswith("(0)") else ("order_applied",))
            continue
        if m[4]:
            out.extend(splice or [("calibration",)])
            continue
        a = _split(_args(body, m.end() - 1))
        if m[1]:
            ev = (m[1], _text(a[0]))
            if m[1] == "get_int":
                ev += (_norm(", ".join(a[1:])),)
            if m[1] == "get_menu_choice":
                ev += (_norm(a[1]),)
            out.append(ev)
        elif "write<" in s:
            key = "l_<i>_cnt" if a[1].startswith('"l_"') else a[1].strip('"')
            out.append(("store", key, _value(a[2])))
        elif s.startswith("apply_setting"):
            out.append(("store", a[0].strip('"'), _value(a[1])))
        elif m[2]:
            out.append((m[2], _norm(", ".join(a))))
        elif m[3]:
            out.append(("wait",))
    return out


PROMPTS = re.findall(r"#define (XEWE_LED_\w+_PROMPT) 1", (SRC / "Config.h").read_text())


def _resolve(text, defined):
    """`text` with its #if/#ifdef/#ifndef/#elif/#else/#endif lines resolved for the macros in
    `defined` (every other macro undefined); conditions use only defined(), !, && and ||."""

    def holds(cond):
        cond = re.sub(
            r"defined\s*\(?\s*(\w+)\s*\)?", lambda m: str(m[1] in defined), cond
        )
        cond = cond.replace("&&", " and ").replace("||", " or ")
        return eval(re.sub(r"!(?!=)", " not ", cond))

    text = re.sub(r"\\\n", " ", text)
    out, stack = [], []  # stack: (this branch is kept, an earlier branch was kept)
    for line in text.splitlines():
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        if not m:
            if all(keep for keep, _ in stack):
                out.append(line)
            continue
        kind, rest = m[1], m[2].strip()
        if kind in ("ifdef", "ifndef", "if"):
            keep = {
                "ifdef": lambda: rest in defined,
                "ifndef": lambda: rest not in defined,
            }.get(kind, lambda: holds(rest))()
            stack.append((keep, keep))
        elif kind == "elif":
            done = stack[-1][1]
            keep = not done and holds(rest)
            stack[-1] = (keep, done or keep)
        elif kind == "else":
            stack[-1] = (not stack[-1][1], True)
        else:
            stack.pop()
    return "\n".join(out)


def _port_wizard(cpp, defined=None):
    """The wizard as a build sees it: every question open unless `defined` names the prompts left."""
    defined = set(PROMPTS) if defined is None else defined
    init = cpp[
        cpp.index("void Led::begin_routines_init()") : cpp.index(
            "void Led::begin_routines_regular()"
        )
    ]
    init = _resolve(init, defined)
    cal = cpp[
        cpp.index("void Led::calibrate_color_order()") : cpp.index(
            "void Led::wait_with_dots("
        )
    ]
    return _events(init, splice=_events(cal))


def _legacy_wizard(cpp):
    init = cpp[
        cpp.index("void LedStrip::begin_routines_init(") : cpp.index(
            "//// HARDCODED DEV"
        )
    ]
    # the chip registration failure is in Led::start_strip in the port (checked on its own)
    init = re.sub(r"if \(!set_leds_chipset\(.*?\n    \}\n", "", init, flags=re.S)
    so = cpp[cpp.index("void LedStrip::set_color_order(std::string_view order)") :]
    so = so[so.index("    } else {") : so.index("// =====", 10)]
    return _events(init, splice=_events(so))


@pytest.mark.unit
def test_wizard_sequence_is_2_3_0():
    """The wizard and its colour-order calibration, in order, against the pinned 2.3.0 sequence
    (prompt texts, menus, get_int bounds, stored keys, the voltage mapping 1/2/3 -> 5/12/24, the
    calibration's strip calls and where the found order is applied). With XEWE_LED_GOLDEN_DIR the
    pinned sequence is also extracted from the 2.3.0 source."""
    cpp = (SRC / "Led.cpp").read_text()
    assert _port_wizard(cpp) == WIZARD_2_3_0
    voltage = cpp[
        cpp.index('"What is LED Voltage?"') : cpp.index(
            '"Do you have parallel LED strips'
        )
    ]
    assert re.findall(r'case (\d):\s*apply_setting\("voltage", "(\d+)"\)', voltage) == [
        ("1", "5"),
        ("2", "12"),
        ("3", "24"),
    ]
    # the parallel-lines branch stores the longest line as num_led
    body = cpp[
        cpp.index("How many LED strips do you have in parallel?") : cpp.index(
            "How many LEDs do you have connected?"
        )
    ]
    assert "longest_line = std::max(longest_line, leds_per_line)" in body
    assert 'apply_setting("num_led", std::to_string(longest_line))' in body
    # no endless loop on a "no" to the pin questions
    assert "while (true);" not in cpp
    start = cpp[
        cpp.index("bool Led::start_strip()") : cpp.index("void Led::release_pins()")
    ]
    assert start.index(
        'os.serial.print("Failed to initialize selected led chip");'
    ) < start.index("os.system.restart();")
    common = cpp[
        cpp.index("void Led::begin_routines_common()") : cpp.index(
            "void Led::calibrate_color_order()"
        )
    ]
    assert 'os.serial.print("Shining the light", "");' in common
    if _find_golden() is not None:
        legacy = _legacy_wizard((_legacy_led_strip() / "LedStrip.cpp").read_text())
        # not in 2.3.0: the parallel-lines num_led store (the first one) and the order reset; the
        # guide line (the first event on both sides) names this module's README, not the 2.3.0 URL
        fixes = {
            0,
            WIZARD_2_3_0.index(("store", "num_led", "{}")),
            WIZARD_2_3_0.index(("order_reset",)),
        }
        assert legacy[0][0] == "print"
        assert legacy[1:] == [e for i, e in enumerate(WIZARD_2_3_0) if i not in fixes]


@pytest.mark.unit
def test_first_boot_values_are_guarded():
    """Each first-boot value of Config.h has its _PROMPT and is asked only under it: dropping one
    prompt removes exactly that question from the wizard and stores the build's value instead."""
    assert PROMPTS == [
        f"XEWE_LED_{k}_PROMPT"
        for k in ("USE_CLK", "CHIP", "VOLTAGE", "LINES", "NUM_LED", "COLORDER")
    ]
    cpp = (SRC / "Led.cpp").read_text()
    full = _port_wizard(cpp)
    asked = {
        "USE_CLK": [("get_yn", "Are you using LED with CLK line?")],
        "CHIP": [("get_menu_choice", "What is your LED Chip?", "chipset_names")],
        "VOLTAGE": [
            ("get_menu_choice", "What is LED Voltage?", '{"5V", "12V", "24V"}')
        ],
        "COLORDER": [("print_header", "Color Order Calibration")],
    }
    for key, events in asked.items():
        view = _port_wizard(cpp, set(PROMPTS) - {f"XEWE_LED_{key}_PROMPT"})
        for event in events:
            assert event in full and event not in view, (key, event)
        assert ("store", key.lower(), "{}") in view or key == "USE_CLK", key
    # colorder pre-answered: no calibration, the build's order and the calibration's end state
    view = _port_wizard(cpp, set(PROMPTS) - {"XEWE_LED_COLORDER_PROMPT"})
    assert ("calibration",) not in view and ("store", "colorder", "{}") in view
    assert view[-7:-2] == [
        ("store", "colorder", "{}"),
        ("set_rgb", "{0, 255, 0}"),
        ("set_brightness", "50"),
        ("set_mode", "0"),
        ("turn_on", ""),
    ]


LINE_QUESTIONS = {
    "parallel": ("get_yn", "Do you have parallel LED strips attached to data line?"),
    "strips": ("get_int", "How many LED strips do you have in parallel?", "1, MAX"),
    "per_line": (
        "get_int",
        "How many LEDs are connected on parallel line #{}?",
        "1, MAX",
    ),
    "count": ("get_int", "How many LEDs do you have connected?", "0, MAX"),
}


@pytest.mark.parametrize(
    "lines_prompt, num_led_prompt, asked",
    [
        (True, True, {"parallel", "strips", "per_line", "count"}),
        (True, False, {"parallel", "strips", "per_line", "count"}),
        (
            False,
            True,
            {"per_line", "count"},
        ),  # lines > 0 asks the lengths, lines 0 the count
        (False, False, set()),  # every line takes XEWE_LED_NUM_LED
    ],
)
@pytest.mark.unit
def test_lines_and_num_led_matrix(lines_prompt, num_led_prompt, asked):
    cpp = (SRC / "Led.cpp").read_text()
    defined = set(PROMPTS) - {
        name
        for name, keep in (
            ("XEWE_LED_LINES_PROMPT", lines_prompt),
            ("XEWE_LED_NUM_LED_PROMPT", num_led_prompt),
        )
        if not keep
    }
    view = _port_wizard(cpp, defined)
    assert {k for k, e in LINE_QUESTIONS.items() if e in view} == asked
    assert ("store", "lines", "0") in view and ("store", "l_<i>_cnt", "{}") in view


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


@pytest.mark.unit
def test_colorder_pre_answer_skips_the_calibration_gpp(tmp_path):
    """A build with XEWE_LED_COLORDER skips the calibration and stores that order; the default build
    calibrates. With every value pre-answered the pin confirmations are skipped too."""

    def init(defines):
        out = _preprocessed(tmp_path, SRC / "Led.cpp", defines)
        body = out[
            out.index("void Led::begin_routines_init()") : out.index(
                "void Led::begin_routines_regular()"
            )
        ]
        return out, body

    out, body = init({})
    assert (
        "calibrate_color_order();" in body
        and "constexpr bool wizard_asks = true;" in out
    )
    out, body = init({"XEWE_LED_COLORDER": "2"})
    assert "calibrate_color_order();" not in body
    assert 'apply_setting("colorder", std::to_string(2));' in body
    assert "constexpr bool wizard_asks = true;" in out
    every = {
        "XEWE_LED_USE_CLK": "0",
        "XEWE_LED_CHIP": "39",
        "XEWE_LED_VOLTAGE": "12",
        "XEWE_LED_LINES": "0",
        "XEWE_LED_NUM_LED": "60",
        "XEWE_LED_COLORDER": "2",
    }
    out, body = init(every)
    assert "constexpr bool wizard_asks = false;" in out
    assert not re.search(r"get_(menu_choice|int)\(", body)
    assert (
        body.count("os.serial.get_yn(") == 2
    )  # the two pin confirmations, behind wizard_asks
    asserts = re.findall(r'static_assert\((.*?),\s*"', out, re.S)
    assert [" ".join(a.split()) for a in asserts][:3] == [
        "39 < LED_CHIPSET_COUNT",
        "12 == 5 || 12 == 12 || 12 == 24",
        "60 <= 600",
    ]


@pytest.mark.unit
def test_set_color_order_without_a_name_calibrates():
    """`$led set_color_order ""` runs the wizard's calibration, outside the command lock (it waits
    for answers while the render task shows the test colours)."""
    cpp = (SRC / "Led.cpp").read_text()
    assert (
        "void Led::Io::calibrate_color_order() {\n    led.calibrate_color_order();\n}"
        in cpp
    )
    cmds = (SRC / "LedCommands.cpp").read_text()
    row = cmds[cmds.index('{"set_color_order"') :]
    assert re.match(
        r'\{"set_color_order",[^}]*\[\]\(LedEngine& e, Args a\) \{ e\.set_color_order\(a\[0\]\); \},\s*false\}',
        row,
        re.S,
    )


@pytest.mark.unit
def test_set_refuses_a_bad_voltage_before_the_core_stores_it():
    """`$led set` is the module's own command: it asks led_voltage_refusal first (printing the
    refusal and returning, so nothing is stored or echoed) and otherwise runs the core's set
    (apply_setting, verbose). Its description and usage are the core's, so `$led help` is unchanged.
    The texts and the write count are pinned by the engine test (test_voltage_refusal)."""
    cpp = (SRC / "Led.cpp").read_text()
    ctor = cpp[cpp.index("Led::Led(xewe::Os& host)") : cpp.index("// Settings\n")]
    m = re.search(
        r'register_command\(\{"set", "([^"]*)", "([^"]*)",\s*2,\s*\[this\]\(xewe::span<const std::string> args\) \{'
        r"\s*const std::string refusal = led_voltage_refusal\(args\[0\], args\[1\], active_voltage\);"
        r"\s*if \(!refusal\.empty\(\)\) \{\s*os\.serial\.print\(refusal\);\s*return;\s*\}"
        r"\s*apply_setting\(args\[0\], args\[1\], true\);\s*\}\}\);",
        ctor,
    )
    assert m, "the set handler refuses first, then runs the core's set"
    assert m.groups() == (
        "Set a setting: validated, applied, saved",
        "$led set <key> <value>",
    )
    core = _find_core_src()
    if core is not None:
        module = (core / "XeWeCore" / "Module" / "Module.cpp").read_text()
        row = '{"set", "Set a setting: validated, applied, saved", " set <key> <value>", 2}'
        assert row in module
    # the settings hook keeps the same rule for other apply_setting callers (puts the value back)
    hook = cpp[
        cpp.index("void Led::on_setting_changed(") : cpp.index(
            "void Led::schema_extra("
        )
    ]
    assert "led_voltage_refusal(key, std::to_string(voltage), active_voltage)" in hook
    assert 'apply_setting("voltage", std::to_string(active_voltage));' in hook


def _serial_calls(node: ast.AST, names: set[str]) -> list[ast.Call]:
    """Calls `serial.<name>(...)` with name in `names` anywhere under `node`."""
    return [
        n
        for n in ast.walk(node)
        if isinstance(n, ast.Call)
        and isinstance(n.func, ast.Attribute)
        and isinstance(n.func.value, ast.Name)
        and n.func.value.id == "serial"
        and n.func.attr in names
    ]


@pytest.mark.unit
def test_board_test_waits_for_each_command():
    """The core reads serial input between commands and keeps at most 4 lines waiting; a line
    beyond that is dropped (`! Input overflow: line dropped`). The board test therefore never has
    more than one line unanswered: every bare `serial.send` is followed directly by a wait for its
    reply (`expect`/`collect`/`command`), or answers a prompt the firmware is blocked on (directly
    after `serial.expect(INPUT)`); every other command goes through `serial.command` or `_send`,
    which waits for the board's echo of the line."""
    board = MODULE_DIR / "tests" / "board" / "test_led.py"
    tree = ast.parse(board.read_text())
    waits = {"expect", "collect", "command"}
    assert not _serial_calls(tree, {"write"}), "raw writes bypass the pacing"

    checked = 0
    for node in ast.walk(tree):
        for field in ("body", "orelse", "finalbody"):
            block = getattr(node, field, None)
            if not isinstance(block, list):
                continue
            for i, stmt in enumerate(block):
                if not (
                    isinstance(stmt, ast.Expr)
                    and _serial_calls(stmt, {"send"}) == [stmt.value]
                ):
                    continue
                checked += 1
                where = f"test_led.py:{stmt.lineno}"
                if i + 1 < len(block):
                    assert _serial_calls(block[i + 1], waits), (
                        f"{where}: the next statement does not wait for the reply"
                    )
                else:  # a prompt answer: the firmware waits for exactly this line
                    prev = block[i - 1] if i else None
                    assert (
                        prev is not None
                        and _serial_calls(prev, {"expect"})
                        and ast.unparse(prev).startswith("serial.expect(INPUT")
                    ), f"{where}: a send at the end of a block must answer a prompt"
    sends = _serial_calls(tree, {"send"})
    assert checked == len(sends) > 0, "every serial.send is a statement of its own"

    send_helper = next(
        f
        for f in ast.walk(tree)
        if isinstance(f, ast.FunctionDef) and f.name == "_send"
    )
    (call,) = _serial_calls(send_helper, waits)
    assert call.func.attr == "command"
    expect = next(k.value for k in call.keywords if k.arg == "expect")
    assert "re.escape(cmd)" in ast.unparse(expect)
    # the setters and mode changes print nothing but their echo: they go through _send
    assert not re.search(
        r"serial\.send\(f\"\$\{ID\} (set_|reset_current_mode)", board.read_text()
    )


# ---------------------------------------------------------------------------------------------
# C++ engine tests (CLI contract, clamping, chipsets, registry, fixes)
# ---------------------------------------------------------------------------------------------


def _engine_binary(
    request, cpp: Path, name: str, extra_defines: list[str] | None = None
) -> Path:
    core = _find_core_src()
    fastled = _find_fastled_src()
    if core is None or fastled is None:
        pytest.skip(
            "needs XeWeCore src/ and FastLED src/ (XEWE_CORE_SOURCE / XEWE_FASTLED_SOURCE or a harness)"
        )
    golden = _find_golden()
    patches = sorted((golden / "shims" / "patches").glob("*.diff")) if golden else []
    cache = _cache_dir(request, "led_host")
    fl_objs = _fastled_objects(cache, fastled, patches)
    gpp = _gpp()
    defines = extra_defines or []
    key = _digest(
        SRC, UNIT / "host", cpp, core / "XeWeCore" / "Utils", gpp, *COMMON, *defines
    )
    objdir = cache / f"{name}-{key}"
    objdir.mkdir(exist_ok=True)
    flags = [
        gpp,
        *COMMON,
        *defines,
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{UNIT / 'host'}",
        f"-isystem{fastled}",
        f"-isystem{core}",
    ]
    srcs = [SRC / s for s in ENGINE_SOURCES] + [UNIT / "host" / "host_arduino.cpp", cpp]
    jobs = [(flags + ["-c", str(s)], objdir / (s.stem + ".o")) for s in srcs]
    _compile_all(jobs)
    exe = objdir / name
    if not exe.is_file():
        subprocess.run(
            [
                gpp,
                "-o",
                str(exe),
                *map(str, fl_objs),
                *(str(j[1]) for j in jobs),
                "-lpthread",
            ],
            check=True,
        )
    return exe


def _run_checks(exe: Path, minimum: int) -> str:
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
    assert run.returncode == 0, run.stdout[-6000:] + run.stderr[-2000:]
    m = re.search(r"PASSED: (\d+) check\(s\), 0 failure\(s\)", run.stdout)
    assert m, run.stdout[-3000:]
    assert int(m[1]) >= minimum, m[0]
    return run.stdout


@pytest.mark.unit
def test_engine_cli_contract_gpp(request):
    """tests/unit/test_led_engine.cpp: the 30 commands (names, args, descriptions, samples, outputs,
    NVS writes, listener events), argument parsing, clamping table, chipsets, registry, fixes."""
    _run_checks(
        _engine_binary(request, UNIT / "test_led_engine.cpp", "test_led_engine"), 400
    )


# ---------------------------------------------------------------------------------------------
# Golden frames: the 2.3.0 oracle (XEWE_LED_GOLDEN_DIR) against this port
# ---------------------------------------------------------------------------------------------

# Scenario files that cannot equal expected/, with the reason. Instead of the byte compare, the
# cadence scenario is checked against the port's own fast-poll boot scenario (same frames, earlier).
CADENCE_SKIPS = {
    "strip_cadence_poll1_n60.json": (
        "the render task draws a frame every 20 ms whatever the loop() polling rate; the reference "
        "drew one every 21 ms when loop() ran once per ms (its frame timer throttles to 5 ms)"
    ),
}
CADENCE_SAME_FRAMES_AS = {
    "strip_cadence_poll1_n60.json": "strip_boot_solid_green_b50_grb_n60.json"
}
# Scenarios compared with the oracle rebuilt from the 2.3.0 sources + golden_230_fixes.diff (the
# fixes applied to the reference tree) instead of expected/: every strip_* scenario boots through
# LedStrip, so the pixels-past-num_led fix shows in mode_controller_num_leds, two enter mode 2 (Two
# Zone), and four (plus mc_set_rgb_hsv_n60) apply a colour to Color Fade, whose saturation taken
# from the colour is now kept inside its 0..245 range.
FIXED_REFERENCE_PREFIX = "strip_"
FIXED_REFERENCE_EXTRA = {"mc_set_rgb_hsv_n60"}
# What the fixes may change in the reference output, per file: only mode_controller_num_leds
# (num_led instead of the buffer maximum), nothing when num_led is the maximum, for the two Two Zone
# files also the colour-derived values (frames, CRC chains, Hue A and the stored colour of mode 2),
# and for the Color Fade files the saturation and what follows from it (FIX_DELTA_COLOR_FADE).
FIX_DELTA_NONE = {"strip_boot_christmas_b128_bgr_n600.json"}
FIX_DELTA_NUM_LEDS = re.compile(r".*/mode_controller_num_leds")
FIX_DELTA_TWO_ZONE = re.compile(
    r".*/mode_controller_num_leds|/crc_chain_all_renders|/renders\[\d+\]/(crc32|first16.*)|"
    r"(/state_after_boot|/final_state|/state_after_ops\[\d+\]/state)/params/hue|/nvs/led/rgb/.*|/nvs/mc/m:2:hue/.*"
)
_STATE = r"(/state_after_boot|/final_state|/state_after_ops\[\d+\]/state)"
_SAT = rf".*/mode_controller_num_leds|{_STATE}/(params/sat|get_rgb\[\d\])|/nvs/mc/m:1:sat/.*"
FIX_DELTA_COLOR_FADE = {
    # only the stored saturation of mode 1 (set_rgb/set_hsv on the ModeController)
    "mc_set_rgb_hsv_n60.json": re.compile(r"/nvs/mc/m:1:sat/.*"),
    # saturation, the colour built from it, the stored saturation
    "strip_boot_fadecolor_b255_gbr_n7.json": re.compile(_SAT),
    "strip_set_mode_0_to_1_n60.json": re.compile(_SAT),
    # colour commands on Color Fade: also the frames, the colour re-derived from the clamped
    # saturation (hue) and the stored colour
    "strip_color_ops_fadecolor_n60.json": re.compile(
        _SAT
        + rf"|/crc_chain_all_renders|/renders\[\d+\]/(crc32|first16.*)|{_STATE}/params/hue|"
        r"/nvs/led/rgb/.*"
    ),
}


def _leaves(x, path=""):
    if isinstance(x, dict):
        for k, v in x.items():
            yield from _leaves(v, f"{path}/{k}")
    elif isinstance(x, list):
        for i, v in enumerate(x):
            yield from _leaves(v, f"{path}[{i}]")
    else:
        yield path, x


def _changed_fields(a: bytes, b: bytes) -> list[str]:
    fa, fb = dict(_leaves(json.loads(a))), dict(_leaves(json.loads(b)))
    return sorted(
        k for k in set(fa) | set(fb) if fa.get(k, KeyError) != fb.get(k, KeyError)
    )


def _build_oracle(
    cache: Path,
    golden: Path,
    fl_objs: list[Path],
    variant: str,
    tree_src: Path,
    extra_patches: list[Path],
    driver: Path,
    port: bool,
    core: Path | None,
    heap_fill: str | None = None,
) -> Path:
    gpp = _gpp()
    key = _digest(
        tree_src,
        *extra_patches,
        driver,
        golden / "shims",
        gpp,
        variant,
        port,
        heap_fill or "",
        (core / "XeWeCore" / "Utils") if core else "",
        UNIT / "golden_port_adapter.h",
        UNIT / "host" / "XeWeCore.h",
    )
    root = cache / f"{'port' if port else 'ref'}-{variant}-{key}"
    exe = root / "oracle"
    if exe.is_file():
        return exe
    root.mkdir(parents=True, exist_ok=True)
    fastled_tree = fl_objs[0].parent.parent / "src"
    incs = [f"-I{golden / 'shims' / 'include'}", f"-I{fastled_tree}"]
    defines = [
        f'-DFASTLED_GIT_COMMIT="20667c3a6413ed46a828f78ec95fb57e58d753f8"',
        f'-DORACLE_VARIANT="{variant}"',
    ]
    if heap_fill:
        defines.append(f"-DORACLE_HEAP_FILL={heap_fill}")
    if port:
        # the module headers include <XeWeCore.h>; on the host it forwards to the utility headers
        overlay = root / "s3inc"
        overlay.mkdir(parents=True, exist_ok=True)
        shutil.copy(UNIT / "host" / "XeWeCore.h", overlay / "XeWeCore.h")
        incs.insert(0, f"-I{overlay}")
        if variant == "esp32s3":
            # the ESP32-S3 compiler fuses these two expressions of hsv_to_rgb into msub.s
            # (oracle README "Float semantics"); make the fusion explicit on the host
            color = (core / "XeWeCore" / "Utils" / "Color.h").read_text()
            q = "    const float q = v_f * (1.0f - f * s_f);"
            t = "    const float t = v_f * (1.0f - (1.0f - f) * s_f);"
            assert color.count(q) == 1 and color.count(t) == 1
            color = color.replace(
                "#include <array>", "#include <array>\n#include <cmath>", 1
            )
            color = color.replace(
                q, "    const float q = v_f * std::fmaf(-f, s_f, 1.0f);"
            )
            color = color.replace(
                t, "    const float t = v_f * std::fmaf(-(1.0f - f), s_f, 1.0f);"
            )
            (overlay / "XeWeCore" / "Utils").mkdir(parents=True, exist_ok=True)
            (overlay / "XeWeCore" / "Utils" / "Color.h").write_text(color)
        incs += [f"-I{core}", f"-I{UNIT}"]
        units = [SRC / s for s in ENGINE_SOURCES] + [driver]
    else:
        tree = root / "tree"
        shutil.copytree(tree_src, tree)
        shutil.copytree(golden / "shims" / "overlay", tree, dirs_exist_ok=True)
        for p in extra_patches:
            subprocess.run(
                ["patch", "-s", "-d", str(tree), "-p1", "-i", str(p)], check=True
            )
        if variant == "esp32s3":
            for p in sorted((golden / "shims" / "patches-esp32s3").glob("*.diff")):
                subprocess.run(
                    ["patch", "-s", "-d", str(tree), "-p1", "-i", str(p)], check=True
                )
        incs += [f"-I{golden / 'third_party' / 'ArduinoJson' / 'src'}", f"-I{tree}"]
        units = sorted(tree.rglob("*.cpp")) + [driver]
    units += sorted((golden / "shims" / "src").glob("*.cpp"))
    objdir = root / "obj"
    objdir.mkdir(exist_ok=True)
    jobs = [
        (
            [gpp, *COMMON, "-w", *defines, *incs, "-c", str(u)],
            objdir / f"{i:03d}_{u.stem}.o",
        )
        for i, u in enumerate(units)
    ]
    _compile_all(jobs)
    subprocess.run(
        [
            gpp,
            "-o",
            str(exe) + ".tmp",
            *map(str, fl_objs),
            *(str(j[1]) for j in jobs),
            "-lpthread",
        ],
        check=True,
    )
    os.replace(str(exe) + ".tmp", exe)
    return exe


def _run_oracle(exe: Path, out: Path, names: list[str]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), "--manifest", str(out)], check=True)

    def one(n):
        return subprocess.run(
            [str(exe), "--run", n, str(out)],
            capture_output=True,
            text=True,
            timeout=600,
        )

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        for n, r in zip(names, ex.map(one, names)):
            assert r.returncode == 0, f"{n}: {r.stderr[-2000:]}"


def golden_compare(request=None, report: Path | None = None) -> dict:
    golden = _find_golden()
    if golden is None:
        pytest.skip(GOLDEN_SKIP)
    core = _find_core_src()
    if core is None:
        pytest.skip("needs XeWeCore src/ (XEWE_CORE_SOURCE or a harness)")
    cache = _cache_dir(request, "led_golden")
    fl_objs = _fastled_objects(
        cache,
        golden / "third_party" / "FastLED" / "src",
        sorted((golden / "shims" / "patches").glob("*.diff")),
    )
    manifest = json.loads((golden / "expected" / "MANIFEST.json").read_text())
    scenarios = [s[: -len(".json")] for s in manifest["scenarios"]]
    flagged = set(manifest.get("depends_on_uninitialised_memory", []))
    strip = [
        s
        for s in scenarios
        if s.startswith(FIXED_REFERENCE_PREFIX) or s in FIXED_REFERENCE_EXTRA
    ]
    results: dict = {
        "scenarios": len(scenarios),
        "variants": {},
        "fixed_reference_changes": {},
        "fixed_reference_fields": {},
        "cadence": {},
    }
    work = cache / "runs"
    shutil.rmtree(work, ignore_errors=True)
    for variant in ("riscv", "esp32s3"):
        port = _build_oracle(
            cache,
            golden,
            fl_objs,
            variant,
            SRC,
            [],
            UNIT / "golden_driver.cpp",
            True,
            core,
        )
        ref = _build_oracle(
            cache,
            golden,
            fl_objs,
            variant,
            golden / "legacy_src",
            [UNIT / "golden_230_fixes.diff"],
            golden / "harness" / "oracle.cpp",
            False,
            None,
        )
        _run_oracle(port, work / variant / "port", scenarios)
        _run_oracle(ref, work / variant / "ref", strip)
        v = {
            "matched_expected": [],
            "matched_fixed_reference": [],
            "skipped": {},
            "mismatch": [],
        }
        for s in scenarios:
            f = s + ".json"
            got = (work / variant / "port" / f).read_bytes()
            if f in CADENCE_SKIPS:
                v["skipped"][f] = CADENCE_SKIPS[f]
                continue
            if s in strip:
                want = (work / variant / "ref" / f).read_bytes()
                (v["matched_fixed_reference"] if got == want else v["mismatch"]).append(
                    f
                )
                continue
            exp = golden / "expected" / f
            if variant == "esp32s3" and (golden / "expected" / "esp32s3" / f).is_file():
                exp = golden / "expected" / "esp32s3" / f
            (
                v["matched_expected"] if got == exp.read_bytes() else v["mismatch"]
            ).append(f)
        # MANIFEST.json: identical apart from nothing (the driver writes it unchanged)
        mf_exp = (
            (golden / "expected" / "MANIFEST.json").read_bytes()
            if variant == "riscv"
            else None
        )
        if mf_exp is not None:
            (
                v["matched_expected"]
                if (work / variant / "port" / "MANIFEST.json").read_bytes() == mf_exp
                else v["mismatch"]
            ).append("MANIFEST.json")
        results["variants"][variant] = v
        # what the fixes change in the 2.3.0 output (reference vs expected/), field by field
        changed, fields = [], {}
        for s in strip:
            f = s + ".json"
            exp = golden / "expected" / f
            if variant == "esp32s3" and (golden / "expected" / "esp32s3" / f).is_file():
                exp = golden / "expected" / "esp32s3" / f
            delta = _changed_fields(
                (work / variant / "ref" / f).read_bytes(), exp.read_bytes()
            )
            if delta:
                changed.append(f)
            fields[f] = delta
        results["fixed_reference_changes"][variant] = changed
        results["fixed_reference_fields"][variant] = fields
        # the cadence scenario: the port's renders are the first renders of its fast-poll boot
        for f, same in CADENCE_SAME_FRAMES_AS.items():
            a = json.loads((work / variant / "port" / f).read_bytes())
            b = json.loads((work / variant / "port" / same).read_bytes())
            results["cadence"].setdefault(variant, {})[f] = {
                "renders": len(a["renders"]),
                "render_count": a["render_count"],
                "same_as_first_renders_of": same,
                "equal": a["renders"] == b["renders"][: len(a["renders"])]
                and a["first_render_t_rel_ms"] == b["first_render_t_rel_ms"],
            }
    results["flagged_uninitialised"] = sorted(flagged)
    if report:
        report.write_text(json.dumps(results, indent=1))
    return results


@pytest.mark.unit
def test_golden_frames_match_2_3_0(request):
    """Needs XEWE_LED_GOLDEN_DIR (skips otherwise). Every scenario of the 2.3.0 oracle, both float
    variants: byte-equal to expected/, or for strip_* and mc_set_rgb_hsv_n60 byte-equal to the
    reference built from the 2.3.0 sources plus the fixes, whose own delta to expected/ is asserted
    field by field."""
    r = golden_compare(request)
    for variant, v in r["variants"].items():
        assert not v["mismatch"], f"{variant}: {v['mismatch']}"
        assert set(v["skipped"]) == set(CADENCE_SKIPS), variant
        for f, c in r["cadence"][variant].items():
            assert c["equal"] and c["renders"] > 50, (variant, f, c)
        for f, delta in r["fixed_reference_fields"][variant].items():
            if f in FIX_DELTA_NONE:
                assert delta == [], (variant, f, delta)
            elif f in FIX_DELTA_COLOR_FADE:
                assert delta and all(
                    FIX_DELTA_COLOR_FADE[f].fullmatch(d) for d in delta
                ), (variant, f, delta[:10])
                assert any(
                    d.endswith("/params/sat") or "/m:1:sat/" in d for d in delta
                ), (
                    variant,
                    f,
                )
            elif f in r["flagged_uninitialised"]:
                assert delta and all(FIX_DELTA_TWO_ZONE.fullmatch(d) for d in delta), (
                    variant,
                    f,
                    delta[:10],
                )
                assert any("/renders[" in d for d in delta), (variant, f)
            else:
                assert delta and all(FIX_DELTA_NUM_LEDS.fullmatch(d) for d in delta), (
                    variant,
                    f,
                    delta[:10],
                )
        # every scenario is accounted for: matched expected/, matched the fixed reference, or skipped
        n = (
            len(v["matched_expected"])
            + len(v["matched_fixed_reference"])
            + len(v["skipped"])
        )
        assert n == r["scenarios"] + (1 if variant == "riscv" else 0), (variant, n)
        # the two files flagged as uninitialised-memory dependent never compare with expected/
        for f in r["flagged_uninitialised"]:
            assert f in v["matched_fixed_reference"], (variant, f)


@pytest.mark.unit
def test_golden_two_zone_fix_independent_of_heap(request):
    """Needs XEWE_LED_GOLDEN_DIR (skips otherwise). Entering mode 2 (Two Zone) reads no
    uninitialised memory: the port gives the same frames with the oracle's heap fill 0xA5 and 0x00."""
    golden = _find_golden()
    if golden is None:
        pytest.skip(GOLDEN_SKIP)
    core = _find_core_src()
    if core is None:
        pytest.skip("needs XeWeCore src/ (XEWE_CORE_SOURCE or a harness)")
    cache = _cache_dir(request, "led_golden")
    fl_objs = _fastled_objects(
        cache,
        golden / "third_party" / "FastLED" / "src",
        sorted((golden / "shims" / "patches").glob("*.diff")),
    )
    manifest = json.loads((golden / "expected" / "MANIFEST.json").read_text())
    names = [f[: -len(".json")] for f in manifest["depends_on_uninitialised_memory"]]
    outs = []
    for fill in ("0xA5", "0x00"):
        exe = _build_oracle(
            cache,
            golden,
            fl_objs,
            "riscv",
            SRC,
            [],
            UNIT / "golden_driver.cpp",
            True,
            core,
            fill,
        )
        out = cache / "heap" / fill
        shutil.rmtree(out, ignore_errors=True)
        _run_oracle(exe, out, names)
        outs.append([(out / (n + ".json")).read_bytes() for n in names])
    assert outs[0] == outs[1]


if __name__ == "__main__":  # standalone: python test_led.py golden <report.json>
    if len(sys.argv) >= 2 and sys.argv[1] == "golden":
        res = golden_compare(None, Path(sys.argv[2]) if len(sys.argv) > 2 else None)
        summary = {
            "scenarios": res["scenarios"],
            "variants": {
                v: {k: len(x) for k, x in d.items()} for v, d in res["variants"].items()
            },
            "mismatch": {v: d["mismatch"] for v, d in res["variants"].items()},
            "cadence": res["cadence"],
            "fixed_reference_fields": {
                v: {
                    f: sorted({re.sub(r"\[\d+\]", "[]", x) for x in d})
                    for f, d in fs.items()
                }
                for v, fs in res["fixed_reference_fields"].items()
            },
        }
        print(json.dumps(summary, indent=1))
