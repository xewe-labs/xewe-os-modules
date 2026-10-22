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
ref = "3.1.0"   # the set tag
selected = ["wifi", "time", "scheduler"]
```

`ref` is the set tag `X.Y.Z`, a module tag `<module>/X.Y.Z` (see [Releasing](#releasing)), or
`latest` for the newest `main`.

Select modules with `./setup.sh --modules wifi,time` or `xewe modules select wifi,time` (`all` and
`none` work too). Dependencies are added automatically. The tools then:

- copy each module's `src/<Folder>/` into the generated library `build/modules/` (`XeWeModules`);
- write `src/Modules.h`, which declares one object per module, dependencies first
  (`Wifi wifi(os);`, then `Time time_module(os, wifi);`);
- install the Arduino libraries the modules list in `depends_libraries` from
  [libraries.toml](libraries.toml), unless the project's `xewe.toml` pins them itself.

After a build the modules' commands are on the console: `$help` lists them, `$<id> status` shows a
module's state, and `$system schema` prints every module's settings as JSON lines.

Build-time values live in the project's `Config.h`, which is a catalogue: `./setup.sh` and `xewe
modules select` append each selected module's `src/<Folder>/Config.h` there once, as one marked
block, and every entry in it is its `/* ... */` comment line followed by the definition commented out
(`// #define XEWE_LED_NUM_LED 600`). Nothing in the project file is active until the user uncomments a
line, which is a bare `#define`. Every name is `XEWE_<SLUG>_<PARAM>` (`XEWE_LED_PIN_DATA`,
`XEWE_WIFI_SSID`; the slug in upper case, `-` as `_`). A block holds two sections, each framed by a start and an end marker
(`/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */` and
`/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */`). A default runtime value marked "asked at first boot"
is pre-answered by uncommenting its line; `INJECTED_FROM_DOTENV` as its value takes it from the dotenv file at build time;
`xewe build --define NAME=VALUE` wins over both. The module's own `Config.h` keeps the fallbacks, so
a line left commented out changes nothing. A block is never rewritten by a later generate. Each
module README lists its values; the scheme is specified in XeWeCore's
[`doc/config.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/config.md) and
[doc/contract.md](doc/contract.md) section 3.2.

## How a module is built

```
modules/<slug>/
├── module.properties          # name, id, version, dependencies, the declare line
├── src/<Folder>/<Folder>.h    # class <Folder> : public xewe::Module
├── src/<Folder>/<Folder>.cpp
├── tests/board/test_<slug>.py # tests on a board, through the xewe-os tools
├── tests/unit/                # optional tests on the developer machine
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

Maintainers only. Each module is released on its own, as tag `<module>/X.Y.Z` (`wifi/3.1.0`); the
set tag `X.Y.Z` (`3.1.0`) marks a commit where every module is released at that version. Projects
pin either (`[modules] ref`): a module tag's commit holds every module at its then-released
version. No `v` prefix. One command does the whole release:

```sh
scripts/release.sh 3.1.0 wifi --notes "What changed, in one line."
scripts/release.sh 3.1.0 --all --hardware c3 --notes "First line." --notes "Second line."
```

`<version>` is `X.Y.Z` or `X.Y.Z-suffix` (a pre-release). Name the modules to release, or `--all`
for every module and the set tag. The notes are 1 to 3 lines (`--notes` once per line, and/or
`--notes-file FILE`) and apply to every tag of the run, so name together only the modules that share
one reason; otherwise run the script once per module. The run writes under `build/release/<version>/`
(gitignored), in 6 stages:

1. Preflight. It refuses, and lists every problem, unless: on `main`; the work tree has no change at
   all; `main` is level with `origin/main` after a fetch; every tag of the run is free here and on
   origin; `tests.yml` calls the tools workflow at its `tools_ref`; the notes pass `xewe
   release-notes check`; `gh` is logged in.
2. Tests. It sets `version=` and `requires_core=` (`>=X.Y.0,<X+1.0.0`) in the named modules'
   `module.properties` and regenerates `doc/modules.md`, then runs, recorded by `xewe
   release-card`, in a copy of the harness project (`XEWE_RELEASE_HARNESS`, else `../xewe-os`) with
   every module selected and this tree as the modules source: `tools/validate.py --harness`, the
   unit tests, the all-modules build and the board tests. With `--hardware c3` (one chip, `c3`,
   `c6` or `s3`) that board is flashed and tested with every gate; without it the board tests are
   compiled, not run (chip `c3`). `XEWE_RELEASE_DEFINES_<CHIP>` gives the chip's defines. A failed
   step does not stop the run; the cards refuse it.
3. Cards. One `release_card.md` per named module with that module's tests and, with `--all`, the
   set's with every module's, under `build/release/<version>/`.
4. Commit. Only the version-line files, as `release <version>` (`--all`) or `release <version>
   <module>...`; it refuses when anything else changed.
5. Tags. Each tag's message is the notes, one blank line and that tag's card
   (`git tag -a --cleanup=verbatim`). It pushes `main`, checks that `origin/main` is `HEAD`, then
   pushes the tags one by one.
6. Releases. For each tag it waits for the `tests` workflow run (`--ci-timeout <min>`, default 60,
   for all tags), requires its `xewe release-card verify --ci` step to have passed, then creates the
   GitHub Release (notes as the text, the card as the asset `release_card.md`, `--prerelease` for a
   suffix) and prints its URL. The `tests` workflow runs on a tag only when `tests.yml` triggers on
   tags; without that the script reports that no run exists for the tag.

`--resume` continues a run that stopped: tags that exist are not made again (the missing ones are
made on the same commit), the results under `build/release/<version>/` are reused when their cards
exist, and a missing GitHub Release is created from its tag's own message. Exit codes: 0 done, 1 a
refusal or a failed check.
