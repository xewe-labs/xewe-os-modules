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
├── releases/<version>/        # notes.txt and release_card.md of the module's release
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
set tag `X.Y.Z` (`3.1.0`) then marks a commit where every module is released at that version, and
is what projects pin (`[modules] ref`) and what the tools fetch. No `v` prefix (the old `v0.2.0`
stays as it is). Every module is prepared together, in one run that makes no git change:

```sh
scripts/release.sh --prepare 3.1.0 --all --dry-run                 # prints every step, changes nothing
scripts/release.sh --prepare 3.1.0 --all --hardware c3,c6,s3       # boards attached; without --hardware: compiled, not run
```

It refuses a tree with changes outside the version lines and the release folders, sets
`version=3.1.0` and `requires_core=>=3.1.0,<4.0.0` in every `module.properties`, regenerates
`doc/modules.md`, and stops with exit 2 until `modules/<module>/releases/3.1.0/notes.txt` (each
module's GitHub Release text) and the set's `releases/3.1.0/notes.txt` are filled (a missing one
gets a skeleton; delete its marker line). Then it runs the checks once for the whole repository in a
copy of the harness project (`--harness <dir>`, else `XEWE_RELEASE_HARNESS`, else `../xewe-os`)
with every module selected and this tree as the modules source, each step recorded by `xewe
release-card` into `releases/3.1.0/logs/` (gitignored, never published): `tools/validate.py
--harness`, the unit tests, `xewe format --check modules tools`, `xewe brand-lint`, every module
built alone for c3, c6 and s3, the all-modules image and the board tests per chip (a `--hardware`
chip is flashed and tested in one session with every gate; `XEWE_RELEASE_DEFINES_<CHIP>` gives its
defines). From that one run it renders nine generated cards: `modules/<module>/releases/3.1.0/
release_card.md` with that module's tests, and `releases/3.1.0/release_card.md` with every module's.
A failed step does not stop the run; the cards refuse it, and `--resume` re-runs only the steps
that are not green. Commit what it lists as `prepare 3.1.0` and push `main`; the `tests` workflow's
card job checks every card against its own unit test run. Then, from that clean `main`, level with
`origin/main`, one module at a time and the set last:

```sh
scripts/release.sh 3.1.0 wifi --dry-run   # prints every step and HEAD's CI state, changes nothing
scripts/release.sh 3.1.0 wifi
scripts/release.sh 3.1.0 --set
```

A module release runs five numbered stages: 1 preflight (version `X.Y.Z`; the module exists; on
`main`, clean and level with `origin/main` after a fetch; tag `<module>/<version>` free here and on
origin; its `notes.txt` and `release_card.md` committed; no `testcard.md`; `module.properties` says
the version; `gh` logged in); 2 `xewe release-card verify` on the module's card (its tested tree is
HEAD's, the shared run's logs are here); 3 waits for the `tests` workflow on HEAD to pass with its
card check run; 4 tags HEAD `<module>/<version>` (annotated), pushes the tag and creates the GitHub
Release (not marked latest) with `release_card.md` as its only asset; 5 says how projects get it:
through the set tag.

`--set` refuses unless every module in `modules/` says `version=<version>` and has its tag
`<module>/<version>` on HEAD or an ancestor, with `modules/<module>/` unchanged since that tag and
its GitHub Release present. It then verifies the set's card, waits for CI on HEAD, tags HEAD
`<version>` and creates the set's GitHub Release: `releases/<version>/notes.txt` plus a link to each
module's release, with the set's `release_card.md` as its only asset.

Options: `--ci-timeout <min>` (default 30) bounds the CI wait; `--skip-ci` skips it, for
emergencies only, with a loud warning. If a tag exists but its GitHub Release does not, the same
command only creates the release (pushing the tag first if origin lacks it) from the tagged
commit's notes and card, provided the tagged commit's `module.properties` says the version (for the
set: the checks above hold there); the branch, clean-tree and origin checks are skipped then. Tag
and release both present: already released.
