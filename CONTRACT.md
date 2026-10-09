# xewe-os-modules: module contract

Status: decided by A8 on 2026-10-08. A9 builds the repo and ports the six modules against this file.
Sources: `priorities.md` (D4, D12, D13, D14, D21, D22, Q1, Q2, steering log), `next/xewe-os-core`
(`Module.h/.cpp`, `XeWeOs.h`, `Cli.h`, `extras/ModuleTemplate`, `examples/05_Os`), `next/xewe-os-tools`
(`SPEC.md` §9/§10, `src/xewe/modules.py`, `src/xewe/testing/plugin.py`, `runner.py`), the six
`xewe-os/xewe-os-module-*` clones and `org-tooling/.github/guidelines/modules.md`.

The contract follows what the tools already parse. **It needs no tools change** (section 7 lists one
optional cosmetic change).

---

## 1. Repo layout

```
xewe-os-modules/
├── modules/
│   └── <slug>/
│       ├── module.properties
│       ├── src/<Folder>/<Folder>.h
│       ├── src/<Folder>/<Folder>.cpp
│       ├── tests/test_<slug>.py
│       └── README.md
├── libraries.toml          # library catalogue: `[Name] repo = "...", ref = "..."` for every depends_libraries name; `xewe setup` installs those the selected modules need (a harness xewe.lock [libraries] pin wins)
├── tools/validate.py       # thin wrapper around the tools' validator plus repo rules (section 5)
├── MODULES.md              # generated index: `tools/validate.py --write-index`
├── README.md               # what a module is, how to add one, how to test through a harness
├── AGENTS.md               # agent rules (section 6)
├── LICENSE.txt             # GPL-3.0-only, one file for the repo
└── .gitignore
```

What `xewe/modules.py` actually expects. A9 matches this exactly:

- **Discovery.** `Registry.load(root)` iterates `sorted((root / "modules").iterdir())` and takes every
  directory that contains a `module.properties` file. The directory name is `dir_slug`, and the
  `slug` key must equal it. Old-layout directories (`xewe-os-module-<slug>/`) are still read, so
  **none may remain in the new repo**. A `module.properties` at the repo root would also be picked
  up, so the root must not have one.
- **Parsing.** `key=value` lines, split at the first `=`. The first occurrence of a key wins, and
  `#` lines are comments. No quoting, no continuation lines, no spaces around `=` (the space would
  become part of the key).
- **Install.** Only `modules/<slug>/src/<folder>/` is copied, to `build/modules-lib/src/<folder>/` (the
  generated Arduino library `XeWeModules`), with `.git` skipped. `include=src/<Folder>/<Folder>.h`
  becomes `#include "<Folder>/<Folder>.h"` in `XeWeModules.h`, which the project's generated
  `src/Modules.h` includes; the `declare=` line is copied into `src/Modules.h` verbatim, in
  dependency order.
- **Tests.** `Module.tests_dir` is `modules/<slug>/tests/`. `runner.test_roots` adds it for every
  resolved module, or only for the modules named with `--module`. Tests are not copied into the
  firmware.
- **Dependency order.** Depth-first over `[modules] selected`, dependencies first, error on a cycle.
  `depends_modules` is split on commas and/or whitespace.

Per-module `LICENSE.txt`, `.gitignore`, `scripts/validate.sh` and `xewe-os-module-<slug>.ino`
are dropped. `tests/` holds no `conftest.py` and no `__init__.py`. The plugin comes in through the
`pytest11` entry point, and the runner uses `--import-mode=importlib`, which is why each test file
needs a unique basename (`test_<slug>.py`).

`.gitignore`: `__pycache__/`, `*.pyc`, `.pytest_cache/`, `build/`, `.venv/`, `.DS_Store`.

## 2. `module.properties`

Order and key names are fixed. Every key is present, even when empty.

| Key | Meaning | Validation (T = tools `xewe modules validate`, R = repo `tools/validate.py`) |
|---|---|---|
| `name` | Display name. It must equal the `name` argument passed to `xewe::Module` in the `.cpp`, because `status` prints `<name> module enabled` | T: present. R: equals the C++ name string (literal search in the `.cpp`) |
| `slug` | Directory name, `--modules` value, `depends_modules` target | T: `^[a-z0-9][a-z0-9-]*$`, unique, equals the directory name |
| `id` | CLI group (`$<id>`) **and** NVS namespace. It can never change once released | T: `^[a-z][a-z0-9_]*$`, ≤ 15 chars, unique. R: the literal `"<id>"` appears in the `.cpp` |
| `version` | Module semver. Bump MINOR for added commands and MAJOR for removed or changed ones (pre-1.0: MINOR for breaking) | T: `X.Y.Z` |
| `description` | One line, shown in the module checklist and `MODULES.md` | T: non-empty, ≤ 100 chars |
| `repo` | Browse URL: `https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>` | T: accepted (legacy key, not checked). R: equals that URL with this slug |
| `folder` | The one installed folder, named like the class | T: `^[A-Z][A-Za-z0-9]*$`, unique, `src/<folder>/` exists. R: `src/<folder>/<folder>.h` and `.cpp` exist |
| `include` | Header that the generated `XeWeModules.h` includes | T: starts with `src/<folder>/`, file exists. R: equals `src/<folder>/<folder>.h` |
| `declare` | Exact line placed in `src/Modules.h` | T: `^(\w+)\s+(\w+)\s*\((.*)\)\s*;$`, variable unique and ≠ `os`, every identifier argument is `os` or the variable of a transitive dependency; a type ≠ folder is only a warning. R: type == folder (error), first argument is exactly `os` |
| `depends_modules` | Comma-separated slugs, empty for none | T: each exists, no self-dependency, acyclic |
| `depends_libraries` | Comma-separated Arduino library names that must come from the harness `xewe.lock` `[libraries]`. Libraries bundled with the esp32 core (WiFi, WebServer, Wire, ...) and XeWeCore are **not** listed. Empty for all six modules | T: accepted (legacy key). R: each name matches `^[A-Za-z0-9_.\- ]+$`, is not `XeWeCore`/`XeWeOS`, and with `--harness DIR` is a key of `[libraries]` |
| `requires_core` | XeWeCore range | T: `^>=\s*X.Y.Z(\s*,\s*<\s*X.Y.Z)?$`, checked against `[core] ref` when run inside a harness |

**`requires_core` syntax is the tools' syntax, `>=2.0.0,<3.0.0`, comma-separated with full
versions.** The brief's `>=2.0.0 <3` form does not parse (`REQUIRES_RE`), and the contract changes,
not the tools. Every ported module uses `requires_core=>=2.0.0,<3.0.0`. Core is `2.0.0`
(`next/xewe-os-core/library.properties`).

Worked example, `modules/wifi/module.properties`:

```properties
name=Wifi
slug=wifi
id=wifi
version=0.2.0
description=Connects to a local WiFi network and keeps the connection alive
repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/wifi
folder=Wifi
include=src/Wifi/Wifi.h
declare=Wifi wifi(os);
depends_modules=
depends_libraries=
requires_core=>=2.0.0,<3.0.0
```

`version` goes 0.1.0 → 0.2.0 for all six, because the port to XeWeCore 2 breaks the API. The other
current values stay as they are. Ids: `wifi`, `web_interface`, `time`, `schedule` (scheduler's id is
not its slug, which is fine), `buttons`, `pins`. Declares: `Wifi wifi(os);`,
`WebInterface web_interface(os, wifi);`, `Time time_module(os, wifi);`,
`Scheduler scheduler(os, time_module);`, `Buttons buttons(os);`, `Pins pins(os);`.

## 3. C++ contract

**Base and include.** A module is `class <Folder> : public xewe::Module`. Its header has
`#include <XeWeCore.h>` as the only XeWeCore include (umbrella, steering 2026-10-08; sub-header-only
includes do not resolve the library). esp32-core headers (`<WiFi.h>`, `<WebServer.h>`, `<Wire.h>`)
are allowed. A required module's header is included relatively, `#include "../Wifi/Wifi.h"`, because
installed folders sit side by side in `build/modules-lib/src/`.

**Overrides.**

| Hook | Rule |
|---|---|
| `status(bool verbose) const` | **Must** override. Starts from `Module::status(false)` (`"<name> module enabled|disabled"`), appends the module's own state, and prints when `verbose`. The first printed line must still match `<name> module (enabled\|disabled)` |
| `reset(verbose, do_restart, keep_enabled)` | **Must** override when the module holds RAM state or hardware; clears it, then calls `Module::reset(...)`, which wipes the NVS namespace. Otherwise it may be omitted |
| `begin_routines_required/init/regular/common` | Optional; override only what is used (semantics in `doc/os/module.md`) |
| `loop()` | Optional. **Must not block** (see section 7, wifi) |
| `enable/disable` | May override. Call the base |

Every module passes `has_cli_commands = true` so that `$<id> status` exists for `test_status`.
Commands go through `register_command(Command{name, description, sample_usage, arg_count, fn})`,
where `fn` takes `xewe::span<const std::string>` (not `std::span`) and `sample_usage` is the literal
`"$<id> <cmd> ..."`.

**Q2, namespace: decided global.** Module classes and their config structs live in the global
namespace, as all six do today. The generated `declare` lines (`Wifi wifi(os);`) and the template
sketch name them unqualified. Global names are what vibecoders type, they keep `declare` and
`Modules.h` unchanged, and the validator keeps module class and variable names unique across the
repo. The cost is that a module class can still collide with a user's or a third-party library's
global name (for example a user's own `Time`). That fails loudly at compile time, the fix is a
rename, and a namespace would make every module and sketch line longer to prevent it.

**Constructor parameters: never reuse a member name.** `Module::os` is a protected member. A
constructor parameter that is also named `os` hides it in the constructor body. Command lambdas
written there with `[this]` then fail with `'os' is not captured`, and with `[&]` they silently bind
the parameter instead (verified with g++ -std=c++20). Hence:

- The Os parameter is named **`host`**. Bodies and lambdas use the member `os`, never `this->os`.
- A dependency parameter is named **`<var>_ref`**. The member is named like the dependency's
  `declare` variable: `wifi`, `time_module`.
- **No handler registered from a constructor may use `[&]` or `[=]`.** Capture `[this]` only. A
  value parameter such as `config` must be stored in a member first.

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
and calls `add_requirement(dep)` in the constructor body. The relationship is written down in three
places, all checked by the validator: `depends_modules=wifi` gives the order and auto-selection, the
`declare` line passes the dependency's variable (`WebInterface web_interface(os, wifi);`), and the
header uses a relative include. A config struct, if any, comes last and is defaulted (`= {}`), so the
`declare` line can omit it.

**`cli` macro.** arduino-esp32 defines `cli()` as a function-like macro. Never write `cli(` in
module code: no `os.cli(...)`, no member or local named `cli` initialised with parentheses. Use
`os.cli.execute("...")`, and brace-init anything named `cli`. `tools/validate.py` greps
`\bcli\s*\(` (section 5).

**ids.** ≤ 15 chars, `[a-z][a-z0-9_]*`. The id is both the CLI group (`$wifi ...`) and the NVS
namespace (`os.nvs.read<T>(id, key)`); a longer id makes Nvs reject every write silently. NVS keys
are ≤ 15 chars too (`Nvs::MAX_KEY_LEN`).

**Other.** No `std::span`, `xewe::os::`, `ModuleController`, `controller`, `xewe_cli` or `<XeWeOS.h>`
anywhere (old API). `DBG_PRINTF`/`DBG_PRINTLN` remain available from XeWeCore.

## 4. Test contract (resolves Q1)

One file per module, `modules/<slug>/tests/test_<slug>.py`, run by `xewe test` (pytest in-process)
inside a `xewe-os` harness (D21). The fixtures come from `xewe.testing.plugin` and are never
redefined:

| Fixture | Scope | Behaviour, verified in `plugin.py` |
|---|---|---|
| `compiled` | session | `build.build_chip(p, lock, chip)` for the session chip. A failure is `pytest.fail` for every dependent test. Returns the binary `Path` |
| `board` | session | `board(xewe, compiled)`, so **it compiles first**, then detects a board. None → `pytest.skip("compiled, not run: …")`; `--require-board` → fail |
| `firmware` | session | Depends on `board` and `compiled`. Flashes once and waits ≤ 3 s for a boot line |
| `serial` | function | A `Console` with `send`, `expect(regex, timeout=10)` (regex *search* per line), `command(cmd, expect, timeout)`, `collect`, `drain`, `lines`, `reset` |

**Markers.** `host` means pure logic, never needs a board or a build, and is selected by
`--host-only` (`-m host`). `hardware` means it needs the firmware. Any test that uses `compiled`,
`board`, `firmware` or `serial` is auto-marked `hardware` (`HARDWARE_FIXTURES`). Module files still
mark host tests explicitly with `@pytest.mark.host`.

**No-board behaviour (D22), checked against the code.** `firmware` → `board` → `compiled`, so the
build happens before the skip and a compile error fails the test. **No tools change is needed.**

**Required tests, per module:**

| Test | Fixture | No board | Board |
|---|---|---|---|
| `test_compiles` | `compiled` | **runs and passes or fails**: the harness firmware with this module selected builds for the session chip (`--xewe-chip`, or every chip with `--all-chips`) | same |
| `test_status` | `serial` | "compiled, not run" | `$<id> status` → `<name> module (enabled\|disabled)` |
| one behaviour test, `test_<what>` | `serial` (or `host`) | "compiled, not run" | exercises one of the module's own commands |

`test_compiles` takes `compiled` and not `firmware`. Through `firmware` it would be skipped and add
nothing to the other two tests, while through `compiled` it is an assertion that really executes in
no-board mode. The cost is that the tools' summary counts it under "hardware passed" (section 7).

**CLI syntax, from `Cli.h` and `Module::register_generic_commands`:** `$<group> <command> [args]`,
with case-insensitive group ids. Every module with CLI commands gets `$<id> status` and `$<id> reset`,
plus `$<id> enable` and `$<id> disable` when `can_be_disabled`. `$<id> status` calls `status(true)`.
`$help`, `$help <id>` and `$system status` also exist.

Full wifi test file:

```python
# modules/wifi/tests/test_wifi.py
"""Wifi module tests. Run through a harness: `python -m xewe test --module wifi`.

Hardware tests assume a provisioned board: first boot done, WiFi credentials stored, connected.
"""
from pathlib import Path

import pytest

ID = "wifi"
NAME = "Wifi"
MODULE_DIR = Path(__file__).resolve().parents[1]


def test_compiles(compiled):
    # build of the harness firmware (this module selected) for the session chip
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_scan_lists_networks(serial):
    serial.command("$wifi scan", expect=r"Scanning WiFi networks", timeout=5)
    serial.expect(r"^\s*0\. \S", timeout=20)   # numbered, de-duplicated SSIDs


@pytest.mark.host
def test_properties_match_source():
    props = dict(l.split("=", 1) for l in (MODULE_DIR / "module.properties").read_text().splitlines() if "=" in l)
    cpp = (MODULE_DIR / "src" / props["folder"] / f"{props['folder']}.cpp").read_text()
    assert props["id"] == ID and f'"{ID}"' in cpp and f'"{NAME}"' in cpp
```

Template (copy and fill in):

```python
# modules/<slug>/tests/test_<slug>.py
"""<Name> module tests. Run through a harness: `python -m xewe test --module <slug>`.

Hardware preconditions: <what the board must have: provisioned, wiring, ...>.
"""
import pytest

ID = "<id>"
NAME = "<name>"          # module.properties name == C++ name argument


def test_compiles(compiled):
    assert compiled.is_file()


def test_status(serial):
    serial.command(f"${ID} status", expect=rf"{NAME} module (enabled|disabled)", timeout=5)


def test_<behaviour>(serial):
    serial.command(f"${ID} <command> <args>", expect=r"<regex from the module's real output>", timeout=10)


@pytest.mark.host
def test_<pure_logic>():   # optional: Python-side checks that need no build
    ...
```

Rules: regexes are copied from strings the module really prints. No `time.sleep`; use
`expect(timeout=)`. No `conftest.py`. A test that needs credentials or wiring reads them from an env
var (`XEWE_TEST_<ID>_<WHAT>`) and calls `pytest.skip("needs ...")` when it is unset, so it never
hangs.

**Host tests (optional).** Logic that does not touch hardware (effect frames, curve maths,
parsers) can also be tested in C++ on the host, without a board or an Arduino build. `led-modes`
(`tests/host/test_effects.cpp`) and the cooling pad v2 (`tests/host/test_curve_math.cpp`) follow it.

- **Rule.** Effect and maths logic lives in a pure header, `src/<Folder>/<Thing>.h`, which the
  module's `.cpp` calls instead of keeping its own copy. Pure means standard headers only (`<cstdint>`,
  `<cmath>`, `<vector>`, `<string>`, ...): no `<Arduino.h>`, `<XeWeCore.h>` or esp32-core headers.
- **Location.** `modules/<slug>/tests/host/*.cpp`, one `main()` per file, including the header by
  relative path (`#include "../../src/<Folder>/<Thing>.h"`). Only `src/<Folder>/` is installed, so
  these files never reach the firmware.
- **Output.** One line per check (`ok   <expr>` / `FAIL <expr> (line N)`), then the summary
  `PASSED: <n> check(s), 0 failure(s)` or `FAILED: ...`; exit code non-zero on any failure.
- **Driver.** A `@pytest.mark.host` test in `tests/test_<slug>.py` compiles it with
  `g++ -std=c++17 -Wall -Wextra -Werror` into `tmp_path`, runs it, and asserts exit 0, `PASSED` and a
  minimum check count, so a silently emptied test fails. When `shutil.which("g++")` is `None` it calls
  `pytest.skip("g++ not installed")`. A second host test may assert the header's includes stay pure.

```python
@pytest.mark.host
def test_<thing>_host(tmp_path):
    gpp = shutil.which("g++")
    if gpp is None:
        pytest.skip("g++ not installed")
    exe = tmp_path / "test_<thing>"
    src = MODULE_DIR / "tests" / "host" / "test_<thing>.cpp"
    subprocess.run([gpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src)], check=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, run.stdout + run.stderr
    assert int(re.search(r"PASSED: (\d+) check", run.stdout)[1]) >= <n>
```

**Selection through the harness (D21).** The harness is a *copy* of the `xewe-os` template. Never use
`next/xewe-os` itself, because `setup --modules` writes its `xewe.lock`.

```sh
cp -r next/xewe-os "$SCRATCH/harness" && cd "$SCRATCH/harness"
XEWE_TOOLS_SOURCE=…/next/xewe-os-tools XEWE_CORE_SOURCE=…/next/xewe-os-core \
  ./setup.sh --modules-source …/next/xewe-os-modules --modules wifi   # wifi + its deps
build/tools/.venv/bin/python -m xewe test --module wifi              # one chip (lock chip, c3)
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --host-only                # host tests of all selected modules
```

`--module` must name a selected module (exit 2 otherwise). Its dependencies are compiled in but their
tests are not run. Without `--module`, the tests of every resolved module run. The repo gate (A9, and
later CI) has two parts. **Isolation:** for each slug, `xewe modules select <slug>`, then
`xewe test --module <slug> --all-chips`; this proves each module compiles with only its dependencies.
**Together:** `--modules all`, then `xewe build --all-chips` and `xewe test`; this proves there are
no clashes between modules.

## 5. Validator: `tools/validate.py`

**Decision: a thin wrapper, no duplication.** `xewe modules validate` (`modules.validate()`) already
implements slug, id, folder, include, declare, depends_modules, requires_core, version, description
and required keys. `tools/validate.py` imports `xewe.modules`, calls
`validate(Registry.load(repo_root), core_ref)`, and adds only the repo-policy rules below. It needs a
Python that has `xewe-os-tools` installed, which is the harness venv
(`<harness>/build/tools/.venv/bin/python tools/validate.py [--harness DIR]`). Without one it exits 3 with
`xewe-os-tools not importable; run with <harness>/build/tools/.venv/bin/python`.

Rules the wrapper adds (R):

| Rule | Check |
|---|---|
| `class` | `declare` type == `folder`. This is an error, while the tools only warn. Combined with unique folders, it makes class names unique |
| `declare-os` | first `declare` argument is exactly `os` |
| `name` | `module.properties` `name` appears as a string literal in `src/<folder>/<folder>.cpp` |
| `id-source` | `"<id>"` appears as a string literal in the `.cpp` |
| `files` | `src/<folder>/<folder>.h`, `.cpp`, `README.md` and `tests/test_<slug>.py` exist; `include` == `src/<folder>/<folder>.h` |
| `tests` | parsed with `ast`: defines `test_compiles`, `test_status` and at least one other `test_*` function |
| `repo` | equals `https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>` |
| `depends_libraries` | names syntax, not XeWeCore/XeWeOS; warning when a name is missing from `libraries.toml`; with `--harness DIR`, error when a name is in neither `libraries.toml` nor that lock's `[libraries]` |
| `source` | in `src/<folder>/*`: no `\bcli\s*\(`, `<XeWeOS.h>`, `xewe::os::`, `ModuleController`, `controller`, `xewe_cli`, `this->os`, `std::span`; the header contains `#include <XeWeCore.h>` |
| `stray` | no `*.ino`, `scripts/`, `LICENSE.txt` or `module.properties` outside `modules/<slug>/`; no `xewe-os-module-*` directories |
| `index` | `MODULES.md` equals what `--write-index` would generate |

`--harness DIR` reads `DIR/xewe.lock` for the `[core] ref` (passed to the tools' `requires_core`
check) and for `[libraries]`. `--write-index` rewrites `MODULES.md` (a header comment saying it is
generated, then one table row per module: slug, name, id, version, description, depends_modules,
requires_core) and exits.

**Output.** One line per finding, in the tools' `Problem.__str__` format so both sources look the
same: `error: <slug>: <message>` or `warning: <slug>: <message>`. The message starts with the rule
or key name (`id 'x' is 16 chars…`, `tests: test_status missing`). The last line is
`N modules, E errors, W warnings`. **Exit codes:** 0 no errors (warnings allowed), 1 at least one
error, 2 usage error (bad path, bad flag), 3 tools not importable. These match the tools' exit-code
table. Stdlib only, apart from the `xewe` import.

## 6. Porting checklist for A9

Per module (wifi, web-interface, time, scheduler, buttons, pins):

1. Copy `src/<Folder>/` → `modules/<slug>/src/<Folder>/`. Reference clones stay untouched.
2. `#include <XeWeOS.h>` → `#include <XeWeCore.h>`. Keep relative module includes
   (`"../Wifi/Wifi.h"`, `"../Time/Time.h"`).
3. `xewe::os::Module` → `xewe::Module`, `xewe::os::ModuleController& controller` →
   `xewe::Os& host`, and `: Module(controller,` → `: xewe::Module(host,`.
4. `controller.` → `os.` in every body (wifi 30, time 24, pins 21, buttons 15, scheduler 12,
   web-interface 4 occurrences). `controller.xewe_cli.execute` → `os.cli.execute` (buttons,
   web-interface).
5. Dependency parameters → `<var>_ref`, members keep `wifi` / `time_module`. Remove any
   `this->os` / `this->controller`. Make sure no lambda uses `[&]`/`[=]`.
6. `std::span<const std::string>` → `xewe::span<const std::string>` (pins, time, scheduler,
   buttons, wifi). Drop `#include <span>` where it becomes unused.
7. `grep -nE '\bcli\s*\('`. It must find nothing.
8. web-interface: C++ name `"Web_Interface"` and properties `name=WebInterface` disagree. Set both
   to `Web Interface`. Check the C++ name of the other five against `name=` (they match today).
9. `module.properties`: new `repo`, `version=0.2.0`, `depends_libraries=` (empty),
   `requires_core=>=2.0.0,<3.0.0`.
10. Delete the `.ino` validation sketch, `scripts/validate.sh`, the per-module `LICENSE.txt` and
    `.gitignore`. The test replaces them.
11. Write `tests/test_<slug>.py` (section 4). The behaviour test uses one command from the module's
    own `register_command` list and a regex copied from what that command prints. Prefer read-only
    commands: wifi `$wifi scan`, time `$time fetch`, pins `$pins gpio_read <pin>` or `adc_read`.
    Scheduler (`$schedule add/remove`) and buttons (`$buttons add/remove`) have only mutating
    commands, so their test runs `add` and then `remove`, leaving the board as it found it. For web-interface, check the serial output of a command; no HTTP
    from tests in phase 1.
12. README trim: keep the title line, Overview, Highlights, "How it works", Commands, Requirements
    (Modules / Boards; Libraries → "XeWeCore >=2.0.0,<3.0.0"), and a Tests section (preconditions +
    the `xewe test --module` line). Remove the Layout table, `validate.sh` mentions and framework
    links to `xewe-library-os`.

Repo level: write `tools/validate.py` (section 5), `MODULES.md` via `--write-index`, `README.md`
(contract summary + harness commands; link this file), `AGENTS.md` (hard rules: section 3
bullets, test rules, "run `tools/validate.py` and `xewe test --module <slug> --all-chips` before
done"), `LICENSE.txt` (copy one module's), `.gitignore` (section 1). Gate for A9: validator exit 0;
each module in isolation compiles for c3/c6/s3 through the harness; `--modules all` compiles for
c3/c6/s3; `xewe test` exits 0 with every hardware test "compiled, not run" and every
`test_compiles` passed.

## 7. Open points (each decided)

1. **Wifi `loop()` blocks** (`while (WiFi.status() != WL_CONNECTED)` with a 5 s `get_yn`), which
   breaks the "loop must not block" rule and stalls the CLI while it is disconnected. *Decision:* A9
   ports it unchanged and records it under "Known issues" in the wifi README. The fix (a
   non-blocking reconnect state machine) waits for step 5, when a board can verify it.
2. **First-boot prompts block hardware tests.** `get_yn` defaults to `timeout_ms = 0`, so a freshly
   erased board waits forever at "Would you like to enable …?". *Decision:* hardware tests assume a
   provisioned board, and each test file states its preconditions in the docstring. A
   `provision` helper (answering first-boot prompts over serial) is deferred to step 5, when tests
   first run on a board.
3. **The "hardware passed" label for `test_compiles`.** In no-board mode the plugin's summary counts it as
   `1 hardware passed`. *Decision:* accept. Optional, cosmetic tools change: count passed tests whose
   only hardware fixture is `compiled` as "compiled". Not required for A9.
4. **Module C++ host-native tests.** None of the six phase 1 modules has logic separable from
   Arduino APIs (scheduler's day/time parsing is the closest). *Decision:* phase 1 module tests are
   Python only. Superseded for pure headers by "Host tests (optional)" in section 4; tests that need
   core's `extras/host` shim still wait until it is reusable from outside core.
5. **Per-module `version` vs repo tag (D12).** *Decision:* both are kept. The repo tag (`v0.2.0`) is
   what `xewe.lock` pins, and the per-module `version` is informational plus the semver promise for
   that module's commands. The six ports ship as `0.2.0` under repo tag `v0.2.0`.
6. **Time's class name `Time`** is a generic global, which is the risk Q2 accepts. *Decision:* keep it
   (renaming the folder changes the install path for existing firmware). The validator's uniqueness
   rules cover clashes between modules, and a clash with user code fails at compile time.
