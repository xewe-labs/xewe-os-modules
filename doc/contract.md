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
│       ├── src/<Folder>/Config.h        # optional: the module's build-time values (section 3.2)
│       ├── tests/board/test_<slug>.py   # pytest on the ESP32 through `xewe test` (required)
│       ├── tests/unit/                  # optional: developer-machine tests (`unit` pytest, C++ for g++)
│       ├── releases/<version>/          # notes.txt and the generated release_card.md of the module's release (README "Releasing")
│       └── README.md
├── doc/
│   ├── contract.md         # this file
│   └── modules.md          # generated index: `tools/validate.py --write-index`
├── libraries.toml          # library catalogue: repo and ref of every depends_libraries name
├── tools/validate.py       # the tools' validator plus the repository rules (section 5)
├── scripts/release.sh      # prepares every module in one card run; releases one module (tag <slug>/X.Y.Z) or the set (tag X.Y.Z)
├── releases/<version>/     # notes.txt and release_card.md of the set release; logs/ of the card run (gitignored)
├── README.md               # what the modules are and how a project uses them
├── .github/workflows/      # CI: tests.yml
├── .agents/                # agent workspace: AGENTS.md (how to work here), RULES.md (hard rules)
├── LICENSE.txt             # GPL-3.0-only, one file for the repository
└── .gitignore              # __pycache__/, *.pyc, .pytest_cache/, build/, .venv/, .DS_Store, releases/*/logs/
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
  `.git` and `__pycache__` skipped. `include=src/<Folder>/<Folder>.h` becomes `#include "<Folder>/<Folder>.h"` in
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
| `description` | One line, shown in `xewe modules list` and `doc/modules.md` | T: non-empty, ≤ 100 characters |
| `repo` | Browse URL | R `repo`: exactly `https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>` |
| `folder` | The one installed folder, named like the class | T: `^[A-Z][A-Za-z0-9]*$`, unique, `src/<folder>/` exists. R `files`: `src/<folder>/<folder>.h` and `.cpp` exist |
| `include` | Header that the generated `XeWeModules.h` includes | T: starts with `src/<folder>/`, the file exists. R `files`: exactly `src/<folder>/<folder>.h` |
| `declare` | The line placed in `src/Modules.h` | T: `^(\w+)\s+(\w+)\s*\((.*)\)\s*;$`; variable unique and not `os`; every identifier argument is `os` or the variable of a transitive dependency; a type other than the folder is a warning. R `class`: type == folder (error). R `declare-os`: the first argument is exactly `os` |
| `depends_modules` | Comma-separated slugs, empty for none | T: each exists, no self-dependency, no cycle |
| `depends_libraries` | Comma-separated Arduino library names. Libraries bundled with the esp32 core (WiFi, WebServer, Wire, ...) and XeWeCore are **not** listed | R `depends_libraries`: see section 5 |
| `requires_core` | XeWeCore range, `>=X.Y.Z,<X.Y.Z` | T: `^>=\s*X.Y.Z(\s*,\s*<\s*X.Y.Z)?$`, checked against the harness `[core] ref` when that is a version. R `core21`: see section 5 |

`requires_core` uses the comma form with full versions, `>=3.1.0,<4.0.0`; `>=3.1.0 <4` does not
parse. A module that uses a settings table, `xewe::ListenerSet`, `xewe::SchemaOut` or the pin registry
needs core 2.1 or later (R `core21`). `scripts/release.sh --prepare <X.Y.Z> --all` sets every module's
`requires_core` to `>=X.Y.0,<(X+1).0.0` (3.1.0: `>=3.1.0,<4.0.0`).

Each module is released on its own under the tag `<slug>/X.Y.Z` (for example `wifi/3.1.0`), with
`version=X.Y.Z` in its `module.properties`. The repository-wide set tag `X.Y.Z` marks a commit where
every module is released at that version; it is what a project's `xewe.toml` `[modules] ref` pins.
A module's `version` is the promise for its commands. Every release publishes the module's generated
`release_card.md` (README "Releasing"), rendered from one shared check run of the whole repository.
Renaming or removing a define of `Config.h` is a breaking change too (section 3.2); the 3.1.0 define
rename is an announced exception, decided for the 3.1.0 set.

Example, `modules/wifi/module.properties`:

```properties
name=Wifi
slug=wifi
id=wifi
version=3.1.0
description=Connects to a local WiFi network and keeps the connection alive
repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/wifi
folder=Wifi
include=src/Wifi/Wifi.h
declare=Wifi wifi(os);
depends_modules=
depends_libraries=
requires_core=>=3.1.0,<4.0.0
```

The scheduler's id (`schedule`) differs from its slug; that is allowed. The declares in this
repository: `Buttons buttons(os);`, `Led led(os);`, `LedWeb led_web(os, led, web_interface);`,
`Pins pins(os);`, `Scheduler scheduler(os, time_module);`, `Time time_module(os, wifi);`,
`WebInterface web_interface(os, wifi);`, `Wifi wifi(os);`.

## 3. C++ contract

**Base and include.** A module is `class <Folder> : public xewe::Module`. Its header has
`#include <XeWeCore.h>` as the only XeWeCore include (R `source`): arduino-cli finds the library only
through its top-level header. Host-includable core headers (`XeWeCore/Utils/Color.h`, home
of the hex colour helpers, and `Listeners.h`) may be included by pure headers that host tests compile. esp32-core headers
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
| `begin_routines_required/init/regular/common` | Optional; override only what is used (XeWeCore `src/XeWeCore/Module/README.md`) |
| `loop()` | Optional. **Must not block**: no waiting loops, no prompts (exception: wifi, section 6) |
| `enable/disable` | May override; call the base |
| `settings()` | Override when the module has plain persistent settings (section 3.1) |
| `on_setting_changed(def)` | Override to apply a changed row (section 3.1) |
| `schema_extra(out)` | Override to report values that are not table rows (section 3.1) |

Every module passes `has_cli_commands = true`, so `$<id> status` exists for `test_status`. Commands
go through `register_command(Command{name, description, sample_usage, arg_count, fn})`, where `fn`
takes `xewe::span<const std::string>` (never `std::span`) and `sample_usage` is the literal
`"$<id> <cmd> ..."`. A command registered once per argument count dispatches on its first argument;
the core picks the registration whose count matches.

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
  setter that persists a value the user changed by other means.
- **Keeping existing commands.** A module that already owns `set` keeps it and forwards a
  non-numeric first argument to `apply_setting(key, value, true)`. Commands that set rows under
  their own name (time `set_zone`) call `apply_setting`.
- **`status`** starts from `Module::status(false)`, which prints one `key: value` line per row, and
  adds only what a row cannot show (connection state, a hex address, live readings).
- **Values that are not rows** (schedule blocks, button mappings) are reported by overriding
  `schema_extra`: one row per value with a `"group"` and a `"set"` command hint. The core registers
  `schema` only for modules with a table; a module with only extra rows (scheduler, buttons)
  registers its own `schema` command that prints them (`print_schema`) and the
  `{"end":"<id>","count":N}` line.
- **Stored FlexData blobs.** A blob carries a `schema` field and is accepted only when
  `has("schema")` is true and the value matches; a missing field is not the struct default. A blob
  from another layout is never reinterpreted and never overwritten at boot. When table rows replace
  an older blob, the module reads it once, copies the values with `apply_setting`, then removes it
  with `os.nvs.remove`.

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

**Hex colours** use the core's `xewe::color::parse_hex_color` / `to_hex_color` from
`XeWeCore/Utils/Color.h` (also reachable as `xewe::str::` through `String.h`); no module keeps its own
copy.

### 3.2 Configuration

Every build-time value of a module lives in `src/<Folder>/Config.h`, in the form specified by
XeWeCore's [`doc/config.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/config.md);
this section is what a module here follows and what R `config` checks. A module without values has
no `Config.h`.

**File.** The file is pure preprocessor: comments, `#pragma once`, `#ifndef` groups and `#define`
lines, no `#include` and no code. The module's main header and every pure sub-header include it
relatively (`#include "Config.h"`, `"../Config.h"` from a subdirectory); a pure sub-header stays
host-compilable because the file includes nothing. The only `//` lines are the three header lines
(the two SPDX lines and the path); every other text line is a single-line C block comment
`/* ... */`, and the header paragraph after them is one multi-line `/* ... */` block. Two
sections follow, each framed by a start and an end marker line exactly as written; the four lines
come in this order, each once, outside any conditional, and every entry sits between a start and
its end. Between an end and the next start, and after the last end, there are blank lines only.
Layout: the start line, a blank line, the entries separated by blank lines, a blank line, the end
line, and a blank line between the sections; an empty section is its start line, a blank line and
its end line.

| Section | Start and end marker | Holds |
|---|---|---|
| default runtime values | `/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */` | the default of a settings row; some of them are asked at first boot |
| compile time defined constants | `/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */` | pins, sizes, task parameters, debug switches |

A single marker line (`/* ---- default runtime values ---- */`) or a `//` marker is an error.

**Entries.** Every value is one `/* ... */` comment line (what it is, unit, range) followed by an
`#ifndef` group of its own name that holds the `#define` (a value asked at first boot adds its
`_PROMPT` define; a chip-conditional default nests `#if`/`#elif`/`#else`/`#endif` inside the group). The `#ifndef` makes
the value a fallback. A chip-conditional default tests `XEWE_CHIP_C3`, `XEWE_CHIP_C6` or
`XEWE_CHIP_S3`, which `xewe build` defines in `XeWeBuildInfo.h` before any other file is read; the
core's `CONFIG_IDF_TARGET_*` comes from `sdkconfig.h` and is not visible there.

**Names.** A define is named `XEWE_<SLUG>_<PARAM>`: the slug in upper case with `-` written as `_`
(`wifi` gives `XEWE_WIFI_`, `led-web` gives `XEWE_LED_WEB_`, `web-interface` gives
`XEWE_WEB_INTERFACE_`), and `<PARAM>` matching `[A-Z0-9_]+`. The `<PARAM>` of a default runtime value
is the settings key in upper case (`tz_gmt_str` gives `XEWE_TIME_TZ_GMT_STR`), so
it is at most 15 characters and never renamed once stored; a compile-time constant takes any
upper-case name. `_PROMPT` is reserved: no `<PARAM>` ends in it. The prefixes `XEWE_CORE_`
(the core), `XEWE_CHIP_` (the build) and `XEWE_TEST_` (the tools) are reserved and no module slug
may produce one. Ownership goes by the longest prefix, so `XEWE_LED_WEB_WS_PORT` is led-web's and
`led` defines no `XEWE_LED_WEB_*` name. A module defines its prefix only in its `Config.h`.

**Asked at first boot and `_PROMPT`.** Asked at first boot is a property of a default runtime value,
not a section: its comment says "asked at first boot" and its `#ifndef` group also defines
`<NAME>_PROMPT 1`. The validator infers the property from that line, which belongs only in the
default-runtime-values section:

```c
/* Timezone offset (time/tz_gmt_str), string literal GMT+HH:MM; detected, asked at first boot */
#ifndef XEWE_TIME_TZ_GMT_STR
#define XEWE_TIME_TZ_GMT_STR "GMT+00:00"
#define XEWE_TIME_TZ_GMT_STR_PROMPT 1
#endif
```

`<NAME>_PROMPT` exists only while nothing earlier in the build defined `<NAME>`. The module's
first-boot routine asks only under `#ifdef <NAME>_PROMPT`; without it the value counts as
pre-answered and is used without a question. The settings row takes `<NAME>` as its default either
way, written exactly as `XEWE_<SLUG>_<KEY>` in the table.

**Sources.** No other file under `src/` defines or undefines a name with a module's prefix or
`XEWE_CORE_` (R `config`).

**How a value reaches the project.** The project's `Config.h` is a catalogue. `xewe modules generate`
(run by `./setup.sh` and `xewe modules select`) appends each selected module's `Config.h`, without its
header, as one marked block to it, once: a `/* ---- start <slug> ---- */` line and a `/* ---- end <slug> ---- */`
line, each framed above and below by a bar of `*` between `/` and `/` as long as the line. The four
section markers are copied; every entry becomes its `/* ... */` comment
line followed by its definition commented out, `// #define <NAME> <value>`, without `#ifndef` and without
`<NAME>_PROMPT`; a chip-conditional entry keeps its `#if`/`#elif`/`#else`/`#endif` lines, each
commented. Nothing in the project file is active until the user uncomments a line. An uncommented
line is a bare `#define` with no `#ifndef`, because the generated `XeWeOverrides.h` is read after the
project's `Config.h`. Uncommenting the line of a value asked at first boot pre-answers its question; its value may be the
sentinel `INJECTED_FROM_DOTENV` to take it from the dotenv file at build time
(`#define XEWE_WIFI_PSW INJECTED_FROM_DOTENV`). A block is never rewritten, so the user's edits
survive every generate. The module's own `Config.h` keeps its `#ifndef` groups as the fallbacks.

**Override ladder.** For one name the first of these wins: `xewe build --define NAME=VALUE`; the
dotenv value behind `INJECTED_FROM_DOTENV`; the project's `Config.h`; the module's `Config.h`
fallback. The tools put three headers in front of every translation unit (`-include`, in this
order): `XeWeBuildInfo.h` (build information, `XEWE_CHIP_<chip>`), the project's `Config.h`, and
`XeWeOverrides.h` (an `#undef` and `#define` for every injected value and every `--define`). The
module's `Config.h` is read later, when its header includes it, so its groups yield to all three,
and a value asked at first boot, defined at any rung, leaves `<NAME>_PROMPT` undefined. `Config.h` includes
nothing, neither `<XeWeBuildInfo.h>` nor `XeWeOverrides.h`.

**What qualifies.** A value belongs in `Config.h` when code needs it at build time: a template
argument (a FastLED pin, a `ListenerSet` size), a buffer size, a debug switch (constants), the
default of a settings row (default runtime values, some asked at first boot).

```c
/* modules/wifi/src/Wifi/Config.h: a default runtime value asked at first boot, and a constant */
/* ---- start default runtime values ---- */

/* Network name (wifi/ssid), string literal, up to 32 characters; asked at first boot */
#ifndef XEWE_WIFI_SSID
#define XEWE_WIFI_SSID ""
#define XEWE_WIFI_SSID_PROMPT 1
#endif

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* Debug output of the module, 0 or 1; 1 prints every step, credentials included: bench only */
#ifndef XEWE_WIFI_DEBUG
#define XEWE_WIFI_DEBUG 0
#endif

/* ---- end compile time defined constants ---- */
```

The same entries in a project's `Config.h`, prose in `/* */` and code in `//` (the file header is
one sentence; a block has no header sentence of its own):

```c
// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os/Config.h
/*
 * Uncomment #define line and change the value to override the default.
 */
#pragma once

/************************/
/* ---- start wifi ---- */
/************************/
/* ---- start default runtime values ---- */

/* Network name (wifi/ssid), string literal, up to 32 characters; asked at first boot */
// #define XEWE_WIFI_SSID ""

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* Debug output of the module, 0 or 1; 1 prints every step, credentials included: bench only */
// #define XEWE_WIFI_DEBUG 0

/* ---- end compile time defined constants ---- */
/************************/
/* ---- end wifi ---- */
/************************/
```

**Versioning.** Renaming or removing a define of `Config.h` is a breaking change (MINOR before
1.0). The 3.1.0 rename of `XEWE_MODULE_<SLUG>_<VAR>` to `XEWE_<SLUG>_<VAR>` is an announced exception,
decided for the 3.1.0 set and listed under BREAKING in its release notes: no alias is kept.

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
conversion) is tested in C++ on the developer machine.

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

`xewe modules validate` (`xewe.modules.registry.validate()`) implements the T rules of section 2;
it also checks the `tests/` layout that R `files` checks.
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
| `config` | the scheme of section 3.2, per module. Prefix: `XEWE_<SLUG>_`, clear of `XEWE_CORE_`, `XEWE_CHIP_` and `XEWE_TEST_`. If `src/<folder>/Config.h` exists: only comment and preprocessor lines, no `#include`, `#pragma once` the only pragma; no `//` line except the three header lines (the SPDX lines and the path), every other text a `/* ... */` comment (the header paragraph one multi-line block, any other comment one line); the four section markers `/* ---- start default runtime values ---- */`, `/* ---- end default runtime values ---- */`, `/* ---- start compile time defined constants ---- */` and `/* ---- end compile time defined constants ---- */`, in this order, each once, outside any conditional; every group sits between a start and its end, and only blank lines stand between an end and the next start or after the last end (a section may be empty); the single-marker form and the old `//` forms are errors whose message names the four expected lines; each `#define` sits inside `#ifndef` of its own name, and each `#ifndef` has exactly one single-line `/* ... */` comment above it; inside a group only nested `#if`/`#ifdef`/`#ifndef` ... `#elif`/`#else` ... `#endif` (chip-conditional defaults) and the `#define`s are allowed, and an `#if`, `#ifdef`, `#else` or `#elif` outside a group is an error, as is any other directive; every name matches `<prefix>[A-Z0-9_]+`, has no other module's prefix (longest prefix owns) and does not end in `_PROMPT`; `<NAME>_PROMPT` is `#define`d as `1` only inside a group of the default-runtime-values section, between its start and end markers (that line is what marks a value as asked at first boot). Outside `Config.h`, no file in `src/<folder>/` defines or undefines a name with any module's prefix or `XEWE_CORE_`. Every settings row's default is exactly `XEWE_<SLUG>_<KEY>`, a define of the default-runtime-values section, and a module with rows has a `Config.h`. Every `NAME` with a `NAME_PROMPT` define is tested as `NAME_PROMPT` in the sources, and every `*_PROMPT` tested there is such a name of the module |
| `stray` (module) | no `*.ino` anywhere in the module, no `scripts/` and no `LICENSE.txt` in its directory |
| `stray` (repository) | no `module.properties` other than `modules/<slug>/module.properties`, no `*.ino` outside `modules/`, no `xewe-os-module-*` directory at the root or under `modules/` |
| `index` | `doc/modules.md` equals what `--write-index` would write |

`--harness DIR` reads `DIR/xewe.toml` for `[core] ref` (passed to the T `requires_core` check) and
`[libraries]`. `--write-index` rewrites `doc/modules.md` (a header comment saying it is generated, then
one row per module: slug, name, id, version, description, dependencies, `requires_core`) and exits.

**Output.** One line per finding, in the tools' format: `error: <slug>: <message>` or
`warning: <slug>: <message>` (`repo` instead of a slug for repository rules); the message starts with
the rule or key name. The last line is `N modules, E errors, W warnings`. **Exit codes:** 0 no errors
(warnings allowed), 1 at least one error, 2 usage error (bad path, unreadable manifest or
`libraries.toml`), 3 tools not importable. Standard library only, apart from the `xewe` import.

## 6. Known limits

1. **wifi waits for the network, by design.** While wifi is enabled and disconnected, its `loop()`
   does not return until WiFi is back: each pass is a 5 s `get_yn` prompt ("Disable and reset WiFi
   module?", default no) and a reconnect with the stored credentials (3 × 10 s, then a 10 s "reset
   credentials?" prompt), up to about 45 s, repeated. A boot with stored credentials waits once the
   same way (up to 30 s plus the prompt). The console and every other module wait meanwhile. This is
   the one sanctioned exception to "`loop()` must not block": the modules built on wifi (time,
   web-interface, and scheduler through time) need the network, so the device holds until it is
   back instead of running them offline. The wifi README describes it under Known issues.
2. **First-boot prompts.** A freshly erased board waits at "Would you like to enable …?" (`get_yn`
   without a timeout). Board tests therefore assume a provisioned board; `xewe provision` answers
   the prompts.
3. **`test_compiles` counts as "board passed"** in the tools' summary, although it needs no board.
4. **The class name `Time`** is a generic global name. It stays, because renaming the folder changes
   the install path for existing firmware; a clash with user code fails at compile time.
