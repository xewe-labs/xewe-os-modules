# xewe-os-modules: module contract

The rules every module in this repository follows. It describes what `xewe-os-tools` parses and what
`tools/validate.py` checks; where a rule is enforced, the rule says by whom (**T** = the tools'
`xewe modules validate`, **R** = this repository's `tools/validate.py`). Rules without a checker are
reviewed by hand. Section 5 lists every validator rule.

---

## 1. Repository layout

```
xewe-os-modules/
├── modules/
│   └── <slug>/
│       ├── module.properties
│       ├── src/<Folder>/<Folder>.h
│       ├── src/<Folder>/<Folder>.cpp
│       ├── tests/board/test_<slug>.py   # pytest on the ESP32 through `xewe test` (required)
│       ├── tests/unit/                  # optional: developer-machine tests (`unit` pytest, C++ for g++)
│       └── README.md
├── libraries.toml          # library catalogue: repo and ref of every depends_libraries name
├── tools/validate.py       # the tools' validator plus the repository rules (section 5)
├── MODULES.md              # generated index: `tools/validate.py --write-index`
├── README.md               # what the modules are and how a project uses them
├── CONTRACT.md             # this file
├── .agents/                # agent workspace: AGENTS.md (how to work here), RULES.md (hard rules)
├── LICENSE.txt             # GPL-3.0-only, one file for the repository
└── .gitignore              # __pycache__/, *.pyc, .pytest_cache/, build/, .venv/, .DS_Store
```

How the tools read the repository (`xewe.modules.registry`):

- **Discovery.** `Registry.load(root)` iterates `sorted((root / "modules").iterdir())` and takes every
  directory that contains a `module.properties` file. The directory name must equal the `slug` key.
  Directories named `xewe-os-module-<slug>/` (the older one-repo-per-module layout) and a
  `module.properties` at the repository root are also picked up, so neither may exist here (R
  `stray`).
- **Parsing.** `key=value` lines, split at the first `=`. The first occurrence of a key wins, and
  `#` lines are comments. No quoting, no continuation lines, no spaces around `=` (a space would
  become part of the key).
- **Install.** Only `modules/<slug>/src/<folder>/` is compiled: it is copied to
  `build/modules/src/<folder>/` of the project (the generated Arduino library `XeWeModules`), with
  `.git` skipped. `include=src/<Folder>/<Folder>.h` becomes `#include "<Folder>/<Folder>.h"` in
  `XeWeModules.h`, which the project's generated `src/Modules.h` includes; the `declare=` line is
  copied into `src/Modules.h` verbatim, in dependency order.
- **Tests.** `xewe modules generate` copies `tests/board/` and `tests/unit/` to the project's
  `build/modules/tests/<slug>/`, beside the library (arduino-cli reads only `library.properties` and
  `src/`, so tests never reach the firmware). `xewe test` collects `build/modules/tests/<slug>/` for
  every module in `build/modules/modules.lock`, or only for the modules named with `--module`. A test
  that reads its module's files (`module.properties`, `src/`, `README.md`, a C++ test beside it) sets
  `MODULE_DIR = module_dir(__file__)` (`from xewe.testing import module_dir`): that is
  `modules/<slug>/` in the modules checkout, also when the test runs from the copy.
- **Dependency order.** Depth-first over `[modules] selected`, dependencies first; a cycle is an
  error. `depends_modules` is split on commas and/or whitespace.
- **Test discovery.** The pytest plugin comes in through the `pytest11` entry point and the runner
  uses `--import-mode=importlib`, so `tests/board/test_<slug>.py` and `tests/unit/test_<slug>.py` may
  share a basename, and no `conftest.py` or `__init__.py` is needed or allowed.

## 2. `module.properties`

Every key is present, even when empty, in this order (the order is a convention; no validator checks
it): `name`, `slug`, `id`, `version`, `description`, `repo`, `folder`, `include`, `declare`,
`depends_modules`, `depends_libraries`, `requires_core`. The tools require every key except `repo`
and `depends_libraries`, which they accept as legacy keys; any other key is a T warning. R requires
`repo` through its own rule.

| Key | Meaning | Checks |
|---|---|---|
| `name` | Display name. It equals the `name` argument passed to `xewe::Module` in the `.cpp`, because `status` prints `<name> module enabled` | T: present. R `name`: the literal `"<name>"` appears in the `.cpp` |
| `slug` | Directory name, `--modules` value, `depends_modules` target | T: `^[a-z0-9][a-z0-9-]*$`, unique, equals the directory name |
| `id` | CLI group (`$<id>`) **and** NVS namespace. It never changes once released | T: `^[a-z][a-z0-9_]*$`, ≤ 15 characters, unique. R `id-source`: the literal `"<id>"` appears in the `.cpp` |
| `version` | The module's semver: MINOR for added commands, MAJOR for removed or changed ones (before 1.0: MINOR for breaking changes) | T: `X.Y.Z` |
| `description` | One line, shown in `xewe modules list` and `MODULES.md` | T: non-empty, ≤ 100 characters |
| `repo` | Browse URL | R `repo`: exactly `https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>` |
| `folder` | The one installed folder, named like the class | T: `^[A-Z][A-Za-z0-9]*$`, unique, `src/<folder>/` exists. R `files`: `src/<folder>/<folder>.h` and `.cpp` exist |
| `include` | Header that the generated `XeWeModules.h` includes | T: starts with `src/<folder>/`, the file exists. R `files`: exactly `src/<folder>/<folder>.h` |
| `declare` | The line placed in `src/Modules.h` | T: `^(\w+)\s+(\w+)\s*\((.*)\)\s*;$`; variable unique and not `os`; every identifier argument is `os` or the variable of a transitive dependency; a type other than the folder is a warning. R `class`: type == folder (error). R `declare-os`: the first argument is exactly `os` |
| `depends_modules` | Comma-separated slugs, empty for none | T: each exists, no self-dependency, no cycle |
| `depends_libraries` | Comma-separated Arduino library names. Libraries bundled with the esp32 core (WiFi, WebServer, Wire, ...) and XeWeCore are **not** listed | R `depends_libraries`: see section 5 |
| `requires_core` | XeWeCore range, `>=X.Y.Z,<X.Y.Z` | T: `^>=\s*X.Y.Z(\s*,\s*<\s*X.Y.Z)?$`, checked against the harness `[core] ref` when that is a version. R `core21`: see section 5 |

`requires_core` uses the comma form with full versions, `>=2.1.0,<3.0.0`; `>=2.1.0 <3` does not
parse. A module that uses a settings table, `xewe::ListenerSet`, `xewe::SchemaOut` or the pin registry
needs core 2.1 and declares `>=2.1.0,<3.0.0` (R `core21`). Every module in this repository does.

The repository tag (`v0.3.0`) is what a project's `xewe.toml` pins; a module's `version` is
informational plus the promise for its commands.

Example, `modules/wifi/module.properties`:

```properties
name=Wifi
slug=wifi
id=wifi
version=0.3.0
description=Connects to a local WiFi network and keeps the connection alive
repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/wifi
folder=Wifi
include=src/Wifi/Wifi.h
declare=Wifi wifi(os);
depends_modules=
depends_libraries=
requires_core=>=2.1.0,<3.0.0
```

The scheduler's id (`schedule`) differs from its slug; that is allowed. The declares in this
repository: `Buttons buttons(os);`, `Fan fan(os);`, `Led led(os);`, `Mlx90614 mlx90614(os);`,
`Pins pins(os);`, `Scheduler scheduler(os, time_module);`, `Time time_module(os, wifi);`,
`WebInterface web_interface(os, wifi);`, `Wifi wifi(os);`.

## 3. C++ contract

**Base and include.** A module is `class <Folder> : public xewe::Module`. Its header has
`#include <XeWeCore.h>` as the only XeWeCore include (R `source`): arduino-cli finds the library only
through its top-level header. Host-includable core headers (`XeWeCore/Utils/Color.h`,
`Listeners.h`) may be included by pure headers that host tests compile. esp32-core headers
(`<WiFi.h>`, `<WebServer.h>`, `<Wire.h>`) are allowed. A required module's header is included
relatively, `#include "../Wifi/Wifi.h"`, because installed folders sit side by side in
`build/modules/src/`.

**Namespace: global.** Module classes and their config structs live in the global namespace. The
generated `declare` lines (`Wifi wifi(os);`) and the template sketch name them unqualified, and the
validator keeps class and variable names unique across the repository. A clash with a user's or a
third-party global name (for example a user's own `Time`) fails at compile time; the fix is a rename.

**Overrides.**

| Hook | Rule |
|---|---|
| `status(bool verbose) const` | **Must** override. Starts from `Module::status(false)` (`"<name> module enabled\|disabled"`, then one `key: value` line per settings row), appends what a row cannot show, and prints when `verbose`. The first printed line must match `<name> module (enabled\|disabled)` |
| `reset(verbose, do_restart, keep_enabled)` | **Must** override when the module holds RAM state or hardware: clear it, release claimed pins, then call `Module::reset(...)`, which wipes the NVS namespace and reloads the table defaults |
| `begin_routines_required/init/regular/common` | Optional; override only what is used (XeWeCore `doc/os/module.md`) |
| `loop()` | Optional. **Must not block**: no waiting loops, no prompts (exception: wifi, section 6) |
| `enable/disable` | May override; call the base |
| `settings()` | Override when the module has plain persistent settings (section 3.1) |
| `on_setting_changed(def)` | Override to apply a changed row (section 3.1) |
| `schema_extra(out)` | Override to report values that are not table rows (section 3.1) |

Every module passes `has_cli_commands = true`, so `$<id> status` exists for `test_status`. Commands
go through `register_command(Command{name, description, sample_usage, arg_count, fn})`, where `fn`
takes `xewe::span<const std::string>` (never `std::span`) and `sample_usage` is the literal
`"$<id> <cmd> ..."`. A command registered once per argument count (led `mode`, fan `curve`) dispatches
on its first argument; the core picks the registration whose count matches.

**Constructor parameters never reuse a member name.** `Module::os` is a protected member. A
constructor parameter also named `os` hides it in the constructor body: command lambdas written there
with `[this]` then fail with `'os' is not captured`, and with `[&]` they silently bind the parameter.
Hence:

- The Os parameter is named **`host`**. Bodies and lambdas use the member `os`, never `this->os`.
- A dependency parameter is named **`<var>_ref`**. The member is named like the dependency's
  `declare` variable: `wifi`, `time_module`.
- **No handler registered from a constructor uses `[&]` or `[=]`.** Capture `[this]` only. A value
  parameter such as `config` is stored in a member first.

```cpp
// modules/wifi/src/Wifi/Wifi.h
#pragma once
#include <WiFi.h>
#include <XeWeCore.h>

class Wifi : public xewe::Module {
public:
    explicit    Wifi    (xewe::Os& host);
    std::string status  (const bool verbose = false) const override;
    // ...
};

// modules/wifi/src/Wifi/Wifi.cpp
Wifi::Wifi(xewe::Os& host)
    : xewe::Module(host, "wifi", "Wifi", "Allows to connect to a local WiFi network",
                   /* requires_init_setup */ true,
                   /* can_be_disabled     */ true,
                   /* has_cli_commands    */ true) {
    register_command({"scan", "List available WiFi networks", "$wifi scan", 0,
                      [this](xewe::span<const std::string>) { scan(true); }});
}

// modules/web-interface/src/WebInterface/WebInterface.h  (dependency by reference)
#include "../Wifi/Wifi.h"
class WebInterface : public xewe::Module {
public:
    WebInterface(xewe::Os& host, Wifi& wifi_ref)
        : xewe::Module(host, "web_interface", "Web Interface", "...", false, true, true)
        , wifi(wifi_ref) {
        add_requirement(wifi);
    }
private:
    Wifi& wifi;
};
```

**Dependencies.** A module that uses another module takes it by reference after `host`, stores it,
and calls `add_requirement(dep)` in the constructor body. The relationship is written in three
places: `depends_modules=wifi` gives the order and the automatic selection, the `declare` line passes
the dependency's variable (`WebInterface web_interface(os, wifi);`, checked by T), and the header uses
a relative include. A config struct, if any, comes last and is defaulted (`= {}`), so the `declare`
line can omit it.

**The `cli` macro.** arduino-esp32 defines `cli()` as a function-like macro. Never write `cli(` in
module code: no `os.cli(...)`, no member or local named `cli` initialised with parentheses. Use
`os.cli.execute("...")` and brace-initialise anything named `cli` (R `source`).

**Ids and keys.** An id is ≤ 15 characters, `[a-z][a-z0-9_]*`. It is both the CLI group (`$wifi ...`)
and the NVS namespace; NVS rejects a longer namespace. NVS keys are ≤ 15 characters too
(`Nvs::MAX_KEY_LEN`). A key that is stored on devices is never renamed or retyped: the stored value
would be lost (a rename is a MAJOR version bump).

**Old API.** None of `std::span`, `xewe::os::`, `ModuleController`, `controller`, `xewe_cli`,
`this->os` or `<XeWeOS.h>` appears in `src/` (R `source`). `DBG_PRINTF`/`DBG_PRINTLN` come from
XeWeCore.

### 3.1 Settings, listeners, pin claims (core ≥ 2.1)

**Settings table.** A module with plain persistent settings declares them as one
`static constexpr xewe::SettingDef` table returned from `xewe::Settings settings() const override`
(`return {table, this};`), one `xewe::setting<&Class::member>("key", ...)` row per setting. The key
is the NVS key under the module id: 1–15 characters, no whitespace, quote or backslash, unique within
the module (R `core21`; the core checks the same at compile time). Credentials and other confidential
values carry `SettingDef::SECRET` (never printed); settings that apply only after a reboot carry
`SettingDef::RESTART`. The core loads the table before `begin_routines_required` (an out-of-range
stored value falls back to the default with one `!` line) and provides `$<id> set|get|schema`, the
status lines and the module's part of `$system schema`. A module's own command of the same name takes
precedence.

- **Applying a change.** `on_setting_changed(def)` runs after `set`/`apply_setting` stored a row. It
  also runs while the module is disabled, so check before touching hardware. It is not called by the
  load at boot. The module never reads or writes a table key by hand; the exception is a runtime
  setter that persists a value the user changed by other means (led `brightness`, `state`).
- **Keeping existing commands.** A module that already owns `set` (fan: `$fan set <pin> <speed>`)
  keeps it and forwards a non-numeric first argument to `apply_setting(key, value, true)`. Older
  command names (`set_addr`, `set_pins`, `set_zone`) stay and call `apply_setting`.
- **`status`** starts from `Module::status(false)`, which prints one `key: value` line per row, and
  adds only what a row cannot show (connection state, a hex address, live readings).
- **Values that are not rows** (mode parameters, fans, curve points, schedule blocks, button
  mappings) are reported by overriding `schema_extra`: one row per value with a `"group"` and a
  `"set"` command hint. The core registers `schema` only for modules with a table; a module with only
  extra rows (scheduler, buttons) registers its own `schema` command that prints them (`print_schema`)
  and the `{"end":"<id>","count":N}` line.
- **Stored FlexData blobs.** A blob carries a `schema` field and is accepted only when
  `has("schema")` is true and the value matches; a missing field is not the struct default. A blob
  from another layout is never reinterpreted and never overwritten at boot. When table rows replace
  an older blob (mlx90614 `data`), the module reads it once, copies the values with
  `apply_setting`, then removes it with `os.nvs.remove`.

**Listeners.** A module that announces changes to other code exposes
`xewe::ListenerSet<Iface, N>` (fixed size, default 4, no heap) and passes a `const void* origin` with
every event: a caller passes `this`, the CLI passes `nullptr`, and a listener ignores events whose
origin is itself. Callbacks run in the task that called the setter, after the change is applied;
keep them short (set a flag, work in `loop()`). A reading with no cause (a sensor poll) may omit the
origin.

**Pin claims.** A module that drives GPIOs claims each one in the core's pin registry with its id
(`xewe::pins::claim(gpio, id.c_str())`) before configuring it and releases it when it lets go
(`remove`, a pin change, `reset`). A refused claim (the pin is held by another module) aborts the
operation; the core prints the conflict. Strapping pins are claimed with a warning. `$pins claims`
lists the registry.

**Hex colours** use the core's `xewe::str::parse_hex_color` / `to_hex_color`; no module keeps its own
copy.

## 4. Tests

Every module has `tests/board/test_<slug>.py` (board tests) and may have `tests/unit/` (unit tests:
`tests/unit/test_<slug>.py` with `unit`-marked pytest tests, plus any C++ files it compiles). Both run
through `xewe test` (pytest in-process) inside an xewe-os project. The fixtures come from
`xewe.testing.plugin` and are never redefined:

| Fixture | Scope | Behaviour |
|---|---|---|
| `compiled` | session | Builds the project firmware for the session chip. A build failure fails every dependent test. Returns the binary `Path` |
| `board` | session | Compiles first, then detects a board. None → `pytest.skip("compiled, not run: …")`; with `--require-board` → fail |
| `firmware` | session | Depends on `board` and `compiled`. Flashes once and waits for the boot line |
| `serial` | function | A console with `send`, `expect(regex, timeout=10)` (a regex *search* per line), `command(cmd, expect, timeout)`, `collect`, `drain`, `lines`, `reset` |

**Markers.** `unit` means pure logic that runs on the developer machine, never needs a board or a
build, and is selected by `--unit-only` (`-m unit`). Unit tests live in `tests/unit/` and are marked
explicitly with `@pytest.mark.unit`. Any test that uses `compiled`, `board`, `firmware` or `serial`
is marked `board` automatically; `tests/board/` holds only board tests.

**Without a board** `firmware` → `board` → `compiled` still builds, so a compile error fails the run,
and every serial test reports "compiled, not run".

**Required board tests** (R `tests`):

| Test | Fixture | No board | Board |
|---|---|---|---|
| `test_compiles` | `compiled` | runs: the firmware with this module selected builds for the session chip (`--chip`, or each chip with `--all-chips`) | same |
| `test_status` | `serial` | "compiled, not run" | `$<id> status` → `<name> module (enabled\|disabled)` |
| one behaviour test, `test_<what>` | `serial` | "compiled, not run" | runs one of the module's own commands |

`test_compiles` takes `compiled`, not `firmware`, so it is a real assertion in no-board mode.

**Console syntax** (XeWeCore `Cli` and `Module::register_generic_commands`): `$<group> <command>
[args]`, group ids case-insensitive. Every module with commands gets `$<id> status` and
`$<id> reset`, plus `$<id> enable` and `$<id> disable` when it can be disabled. `$<id> status` calls
`status(true)`. `$help`, `$help <id>`, `$system status` and `$system schema` also exist.

Board test template:

```python
# modules/<slug>/tests/board/test_<slug>.py
"""<Name> module tests. Run through a harness: `python -m xewe test --module <slug>`.

Hardware preconditions: <what the board must have: provisioned, wiring, env vars>.
"""
ID = "<id>"
NAME = "<name>"          # module.properties name == C++ name argument


def test_compiles(compiled):
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_<behaviour>(serial):
    serial.command(f"${ID} <command> <args>", expect=r"<regex from the module's real output>", timeout=10)
```

Unit test template (every module has at least this one):

```python
# modules/<slug>/tests/unit/test_<slug>.py
"""<Name> module unit tests: pure logic on the developer machine, no board and no build."""
import pytest

from xewe.testing import module_dir

ID = "<id>"
NAME = "<name>"
MODULE_DIR = module_dir(__file__)   # modules/<slug>/, also when run from build/modules/tests/<slug>/unit/


@pytest.mark.unit
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
```

Rules:

- Regexes are copied from strings the module really prints.
- No `time.sleep`; use `expect(..., timeout=)`.
- No `conftest.py` or `__init__.py`, and nothing under `tests/` but `board/` and `unit/` (R `files`).
- A test that changes state undoes it (add, then remove; set, then restore).
- A test that needs credentials or wiring reads them from an environment variable
  (`XEWE_TEST_<ID>_<WHAT>`) and calls `pytest.skip("needs ...")` when it is unset, so it never hangs.
- The module docstring states the hardware preconditions. Board tests assume a provisioned board:
  first boot done and every first-boot prompt answered (`xewe provision`).

**C++ unit tests (optional).** Logic that does not touch hardware (effect frames, curve maths, value
conversion) is tested in C++ on the developer machine. `led` (`test_effects.cpp`,
`test_listeners.cpp`), `fan` (`test_curve.cpp`) and `mlx90614` (`test_convert.cpp`) do this.

- **Pure header.** The logic lives in `src/<Folder>/<Thing>.h`, which the module's `.cpp` calls
  instead of keeping its own copy. Pure means standard headers only (`<cstdint>`, `<cmath>`,
  `<vector>`, `<string>`, ...) plus the host-includable core headers: no `<Arduino.h>`,
  `<XeWeCore.h>` or esp32-core headers.
- **Location.** `tests/unit/*.cpp`, one `main()` per file, including the header by relative path
  (`#include "../../src/<Folder>/<Thing>.h"`). Only `src/<Folder>/` is installed into firmware.
- **Output.** One line per check (`ok   <expr>` / `FAIL <expr> (line N)`), then
  `PASSED: <n> check(s), 0 failure(s)` or `FAILED: ...`; a non-zero exit code on any failure.
- **Driver.** A `@pytest.mark.unit` test in `tests/unit/test_<slug>.py` compiles it with
  `g++ -std=c++17 -Wall -Wextra -Werror` into `tmp_path`, runs it, and asserts exit 0, `PASSED` and a
  minimum check count, so a silently emptied test fails. Without g++ it calls
  `pytest.skip("g++ not installed")`. A second unit test may assert that the header stays pure.

```python
@pytest.mark.unit
def test_<thing>_unit_gpp(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_<thing>"
    src = MODULE_DIR / "tests" / "unit" / "test_<thing>.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert int(re.search(r"PASSED: (\d+) check", run.stdout)[1]) >= <n>
```

**Running through a project.** Tests run in a *copy* of the xewe-os template, never in the template
itself (`setup --modules` rewrites its `xewe.toml`):

```sh
./setup.sh --modules-source /path/to/xewe-os-modules --modules wifi   # wifi and its dependencies
build/tools/.venv/bin/python -m xewe test --module wifi               # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips   # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --unit-only                 # unit tests of the selected modules
```

`--module` names a selected module (exit 2 otherwise); its dependencies are compiled in but their
tests do not run. Without `--module`, the tests of every selected module run.

A change is complete when it passes in two ways. **Alone:** for each changed module,
`xewe modules select <slug>` then `xewe test --module <slug> --all-chips`; this proves the module
compiles with only its dependencies. **Together:** `--modules all`, then `xewe build --all-chips` and
`xewe test`; this proves the modules do not clash.

## 5. Validator: `tools/validate.py`

`xewe modules validate` (`xewe.modules.registry.validate()`) implements the T rules of section 2.
`tools/validate.py` imports `xewe.modules.registry`, runs `validate(Registry.load(repo_root),
core_ref)`, and adds the repository rules below. It needs a Python with `xewe-os-tools` installed,
which is a project's venv:

```sh
<project>/build/tools/.venv/bin/python tools/validate.py [--harness <project>]
<project>/build/tools/.venv/bin/python tools/validate.py --write-index
```

Without the tools it exits 3 with `xewe-os-tools not importable; run with
<harness>/build/tools/.venv/bin/python`.

| Rule | Check |
|---|---|
| `class` | `declare` type == `folder` (an error here; T only warns). With unique folders this makes class names unique |
| `declare-os` | the first `declare` argument is exactly `os` |
| `name` | `module.properties` `name` appears as a string literal in `src/<folder>/<folder>.cpp` |
| `id-source` | `"<id>"` appears as a string literal in the `.cpp` |
| `files` | `src/<folder>/<folder>.h`, `.cpp`, `README.md` and `tests/board/test_<slug>.py` exist; `include` == `src/<folder>/<folder>.h`; `tests/` holds only the directories `board/` and `unit/`; neither holds `conftest.py` or `__init__.py` |
| `tests` | `tests/board/test_<slug>.py` parses (`ast`) and defines `test_compiles`, `test_status` and at least one other top-level `test_*` function |
| `repo` | `repo` equals `https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>` |
| `depends_libraries` | each name matches `^[A-Za-z0-9_.\- ]+$` and is not `XeWeCore`/`XeWeOS`. With `--harness`: a name in neither `libraries.toml` nor the harness `[libraries]` is an error. A name missing from `libraries.toml` is otherwise a warning |
| `source` | no line of any file in `src/<folder>/` matches `\bcli\s*\(`, `<XeWeOS\.h>`, `xewe::os::`, `ModuleController`, `\bcontroller\b`, `xewe_cli`, `this->os\b` or `std::span` (comments included); the header contains `#include <XeWeCore.h>` |
| `core21` | a module whose `src/` (`.h`, `.hpp`, `.cpp`, `.tpp`) uses `SettingDef`, `ListenerSet`, `SchemaOut` or `xewe::pins::` declares `requires_core` with a lower bound ≥ `2.1.0`; every `xewe::setting<...>("key", ...)` key is 1–15 characters without whitespace, quote or backslash, and unique within the module |
| `stray` (module) | no `*.ino` anywhere in the module, no `scripts/` and no `LICENSE.txt` in its directory |
| `stray` (repository) | no `module.properties` other than `modules/<slug>/module.properties`, no `*.ino` outside `modules/`, no `xewe-os-module-*` directory at the root or under `modules/` |
| `index` | `MODULES.md` equals what `--write-index` would write |

`--harness DIR` reads `DIR/xewe.toml` for `[core] ref` (passed to the T `requires_core` check) and
`[libraries]`. `--write-index` rewrites `MODULES.md` (a header comment saying it is generated, then
one row per module: slug, name, id, version, description, dependencies, `requires_core`) and exits.

**Output.** One line per finding, in the tools' format: `error: <slug>: <message>` or
`warning: <slug>: <message>` (`repo` instead of a slug for repository rules); the message starts with
the rule or key name. The last line is `N modules, E errors, W warnings`. **Exit codes:** 0 no errors
(warnings allowed), 1 at least one error, 2 usage error (bad path, unreadable manifest or
`libraries.toml`), 3 tools not importable. Standard library only, apart from the `xewe` import.

## 6. Known limits

1. **wifi `loop()` blocks** while disconnected (`while (WiFi.status() != WL_CONNECTED)` with a 5 s
   `get_yn` prompt per pass), which breaks the "loop must not block" rule: the console and every
   other module stall until WiFi is back. The fix is a non-blocking reconnect state machine. The wifi
   README lists it under Known issues.
2. **First-boot prompts.** A freshly erased board waits at "Would you like to enable …?" (`get_yn`
   without a timeout). Board tests therefore assume a provisioned board; `xewe provision` answers
   the prompts.
3. **`test_compiles` counts as "board passed"** in the tools' summary, although it needs no board.
4. **The class name `Time`** is a generic global name. It stays, because renaming the folder changes
   the install path for existing firmware; a clash with user code fails at compile time.
