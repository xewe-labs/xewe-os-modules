# xewe-os-modules

Ready-made modules for [XeWe OS](https://github.com/xewe-labs/xewe-os) firmware. Each module is one
C++ class built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core) that adds a `$<id> ...`
command group, its own settings in NVS, and status lines to a device. A project picks the modules it
needs; the build tools ([xewe-os-tools](https://github.com/xewe-labs/xewe-os-tools)) install them,
declare them in dependency order and compile them in.

License: GPL-3.0-only, see [LICENSE.txt](LICENSE.txt).

## Where this fits

XeWeCore has three levels of use:

| Level | Who | What |
|---|---|---|
| 1 | Arduino IDE user | XeWeCore alone: the console, `$help`, `$system status`, a device name |
| 2 | Arduino IDE user | the same plus one module of your own in the sketch folder |
| 3 | project builder | the xewe-os template and its tools, with the modules of **this repository** |

This repository is level 3. It is not an Arduino library: the tools copy the selected modules into a
project's build. To write a module of your own inside a sketch, start at level 2 with XeWeCore's
`examples/02_MyModule`. The levels are described in XeWeCore's
[`doc/README.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/README.md).

## The modules

| Module | What it does | Needs |
|---|---|---|
| [buttons](modules/buttons) | Binds CLI commands to physical buttons, with software debouncing | |
| [led](modules/led) | Addressable LED strip: 46 chipsets, 7 effects, brightness and colour order | |
| [led-web](modules/led-web) | LED control page in the browser, with live sync over a WebSocket | led, web-interface |
| [pins](modules/pins) | GPIO, ADC, PWM and I2C access from the command line | |
| [scheduler](modules/scheduler) | Runs stored commands on a weekly schedule | time |
| [time](modules/time) | NTP time sync and automatic timezone detection | wifi |
| [web-interface](modules/web-interface) | An HTTP page and command endpoint for other devices on the network | wifi |
| [wifi](modules/wifi) | Joins a WiFi network and keeps the connection alive | |

The unfinished `fan` and `mlx90614` modules are parked outside the repository until they are ready.

[doc/modules.md](doc/modules.md) is the generated index with ids, versions and the XeWeCore range each module
needs. Each module's README lists its commands, settings, NVS keys, build defines and tests.

## Using modules in a project

A project is a copy of the [xewe-os](https://github.com/xewe-labs/xewe-os) template. Its manifest,
`xewe.toml`, pins this repository and lists the selected modules:

```toml
[modules]
repo = "https://github.com/xewe-labs/xewe-os-modules"
ref = "3.0.0"   # the set tag
selected = ["wifi", "time", "scheduler"]
```

`ref` is the set tag `X.Y.Z` (see [Releasing](#releasing)), or `latest` for the newest `main`.

Select modules with `./setup.sh --modules wifi,time` or `xewe modules select wifi,time` (`all` and
`none` work too). Dependencies are added automatically. The tools then:

- copy each module's `src/<Folder>/` into the generated library `build/modules/` (`XeWeModules`);
- write `src/Modules.h`, which declares one object per module, dependencies first
  (`Wifi wifi(os);`, then `Time time_module(os, wifi);`);
- install the Arduino libraries the modules list in `depends_libraries` from
  [libraries.toml](libraries.toml), unless the project's `xewe.toml` pins them itself.

After a build the modules' commands are on the console: `$help` lists them, `$<id> status` shows a
module's state, and `$system schema` prints every module's settings as JSON lines.

Compile-time values (a pin a driver needs as a constant, a buffer size, listener slots) live in
the project's `Config.h`: `./setup.sh` and `xewe modules select` append each selected module's
`src/<Folder>/Config.h` there once, as one marked block (`XEWE_MODULE_<SLUG>_<VAR>` defines), and
the user edits the numbers. First-boot defaults that end up in NVS stay
`--define` values: `xewe build --define KEY=VALUE`. Each module README lists both.

## How a module is built

```
modules/<slug>/
├── module.properties          # name, id, version, dependencies, the declare line
├── src/<Folder>/<Folder>.h    # class <Folder> : public xewe::Module
├── src/<Folder>/<Folder>.cpp
├── tests/board/test_<slug>.py # tests on a board, through the xewe-os tools
├── tests/unit/                # optional tests on the developer machine
├── releases/<version>/        # notes.txt and testcard.md of the module's release
└── README.md
```

A module's `id` is its command group (`$wifi ...`) and its NVS namespace, so it never changes once
released. Settings are declared once in a table and the core provides `$<id> set|get|schema` for
them. Modules that announce changes offer listeners, and every module claims the GPIOs it drives in
the core's pin registry, so two modules cannot drive the same pin.

[doc/contract.md](doc/contract.md) is the full specification every module follows. Contributors and agents
start at [`.agents/AGENTS.md`](.agents/AGENTS.md), which covers adding a module, the validator and
the tests.

## Releasing

Maintainers only. Each module is released on its own, as tag `<module>/X.Y.Z` (`wifi/3.0.0`); the
set tag `X.Y.Z` (`3.0.0`) then marks a commit where every module is released at that version, and
is what projects pin (`[modules] ref`) and what the tools fetch. No `v` prefix (the old `v0.2.0`
stays as it is). First prepare, which makes no git change:

```sh
scripts/release.sh --prepare 3.0.1 wifi   # version=3.0.1 in modules/wifi/module.properties, releases/3.0.1/ skeletons
scripts/release.sh --prepare 3.0.1 --set  # releases/3.0.1/notes.txt skeleton for the set
```

Fill `modules/<module>/releases/<version>/notes.txt` (the GitHub Release text) and `testcard.md`
(what was tested on which commits, attached to the release), and the set's short
`releases/<version>/notes.txt`; commit them as `prepare <version>` and push `main`. Then, from that
clean `main`, level with `origin/main`, one module at a time and the set last:

```sh
scripts/release.sh 3.0.1 wifi --dry-run   # prints every step and HEAD's CI state, changes nothing
scripts/release.sh 3.0.1 wifi
scripts/release.sh 3.0.1 --set
```

A module release runs five numbered stages: 1 preflight (version `X.Y.Z`; the module exists; on
`main`, clean and level with `origin/main` after a fetch; tag `<module>/<version>` free here and on
origin; its `releases/<version>/` files committed; `module.properties` says the version; `gh` logged
in); 2 waits for the `tests` workflow on HEAD to pass; 3 runs `tools/validate.py` and the module's
unit tests (with a Python that has xewe-os-tools: `XEWE_PYTHON`, else `python3`, else the one behind
`xewe`); 4 tags HEAD `<module>/<version>` (annotated), pushes the tag and creates the GitHub Release
(not marked latest) with `testcard.md` attached; 5 says how projects get it: through the set tag.

`--set` refuses unless every module in `modules/` says `version=<version>` and has its tag
`<module>/<version>` on HEAD or an ancestor, with `modules/<module>/` unchanged since that tag and
its GitHub Release present. It then waits for CI on HEAD, tags HEAD `<version>` and creates the
set's GitHub Release: `releases/<version>/notes.txt` plus a link to each module's release.

Options: `--ci-timeout <min>` (default 30) bounds the CI wait; `--skip-ci` skips it, for
emergencies only, with a loud warning. If a tag exists but its GitHub Release does not, the same
command only creates the release (pushing the tag first if origin lacks it), provided the tagged
commit's `module.properties` says the version (for the set: the checks above hold there); the
branch, clean-tree and origin checks are skipped then. Tag and release both present: already
released.
