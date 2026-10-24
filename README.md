# xewe-os-modules

Ready-made modules for [XeWe OS](https://github.com/xewe-labs/xewe-os) firmware on ESP32-C3, -C6 and -S3. Each module is one C++ class built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core) that adds a `$<id> ...` command group, its own settings in NVS and status lines to a device. A project picks the modules it needs, and the build tools ([xewe-os-tools](https://github.com/xewe-labs/xewe-os-tools)) install them, declare them in dependency order and compile them in.

## Problem

- Every ESP32 project writes its own WiFi join, NTP sync, LED driver and web console, and each copy drifts from the last one.
- Two features that drive the same GPIO fight over it without a word, and settings stored under clashing NVS keys overwrite each other.
- Code copied between projects arrives without its tests, so nobody knows whether it still works with the current core.

## Solution

This repository holds the shared features as modules with one contract, one validator and tests per module. A project lists the modules it wants in `xewe.toml`, and the tools copy them in, add their dependencies and wire them up. Three ideas carry it:

- A module's `id` is its command group (`$wifi ...`) and its NVS namespace, so it never changes once released. Settings are declared once in a table, and the core provides `$<id> set|get|schema` for them.
- Every module claims the GPIOs it drives in the core's pin registry, so two modules cannot drive the same pin. Modules that announce changes offer listeners.
- `module.properties` names the class, its dependencies and the exact line that declares it, and `tools/validate.py` checks every module against [doc/CONTRACT.md](doc/CONTRACT.md).

## See it

Select `time` in a project:

```sh
./setup.sh --modules time
```

The tools add `wifi`, which `time` needs, and write `src/Modules.h` with dependencies first:

```cpp
Wifi wifi(os);
Time time_module(os, wifi);
```

After a build the commands are on the serial console, and the pin registry refuses a pin that another module holds:

```text
$pins claims
GPIO 3: buttons
$pins gpio_write 3 1
! GPIO 3 already claimed by buttons, refused for pins
```

## Why not write the module yourself

XeWeCore has three levels of use, and this repository is level 3.

| Level | Who | What |
|---|---|---|
| 1 | Arduino IDE user | XeWeCore alone: the console, `$help`, `$system status`, a device name |
| 2 | Arduino IDE user | the same plus one module of your own in the sketch folder |
| 3 | project builder | the xewe-os template and its tools, with the modules of this repository |

A module you write at level 2 lives in one sketch and you test it by hand. The modules here are not an Arduino library: the tools copy the selected modules into a project's build, and every module carries board tests and unit tests that run through `xewe test`. To write a module of your own inside a sketch, start at level 2 with XeWeCore's `examples/02_MyModule`. XeWeCore's [`doc/README.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/README.md) describes the levels.

## Install

A project is a copy of the [xewe-os](https://github.com/xewe-labs/xewe-os) template. Its manifest, `xewe.toml`, pins this repository and lists the selected modules:

```toml
[modules]
repo = "https://github.com/xewe-labs/xewe-os-modules"
ref = "3.1.0"   # the set tag
selected = ["wifi", "time", "scheduler"]
```

`ref` is the set tag `X.Y.Z`, a module tag `<module>/X.Y.Z` ([scripts/README.md](scripts/README.md#tags) explains both), or `latest` for the newest `main`. Then install the selection:

```sh
./setup.sh --modules wifi,time,scheduler
```

## Usage

### Select modules

Run `./setup.sh --modules wifi,time` or `xewe modules select wifi,time`; `all` and `none` work too. Dependencies are added automatically. The tools then:

- copy each module's `src/<Folder>/` into the generated library `build/modules/` (`XeWeModules`);
- write `src/Modules.h`, which declares one object per module, dependencies first (`Wifi wifi(os);`, then `Time time_module(os, wifi);`);
- install the Arduino libraries the modules list in `depends_libraries` from [libraries.toml](libraries.toml), unless the project's `xewe.toml` pins them itself.

### Use the commands

After a build, `$help` lists the modules' commands, `$<id> status` shows a module's state, and `$system schema` prints every module's settings as JSON lines. Each module's README lists its commands, settings, NVS keys, build defines and tests.

### Set build-time values

Build-time values live in the project's `Config.h`, which is a catalogue. `./setup.sh` and `xewe modules select` append each selected module's `src/<Folder>/Config.h` there once, as one marked block. Every entry in it is its `/* ... */` comment line followed by the definition commented out (`// #define XEWE_LED_NUM_LED 600`). Nothing in the project file is active until you uncomment a line, which is then a bare `#define`.

Every name is `XEWE_<SLUG>_<PARAM>`, with the slug in upper case and `-` written as `_` (`XEWE_LED_PIN_DATA`, `XEWE_WIFI_SSID`). A block holds two sections, each framed by a start and an end marker: `/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */` and `/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */`.

Uncommenting the line of a default runtime value marked "asked at first boot" pre-answers that question. `INJECTED_FROM_DOTENV` as its value takes it from the dotenv file at build time, and `xewe build --define NAME=VALUE` wins over both. The module's own `Config.h` keeps the fallbacks, so a line left commented out changes nothing. A later generate never rewrites a block. XeWeCore's [`doc/CONFIG.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/CONFIG.md) specifies the scheme, and [doc/CONTRACT.md](doc/CONTRACT.md) section 3.2 says how the modules here follow it.

## Status

These eight modules build for c3, c6 and s3 against XeWeCore `>=3.1.0,<4.0.0`:

| Module | What it does | Needs |
|---|---|---|
| [buttons](modules/buttons/README.md) | Binds CLI commands to physical buttons, with software debouncing | |
| [led](modules/led/README.md) | Addressable LED strip: 46 chipsets, 7 effects, brightness and colour order | |
| [led-web](modules/led-web/README.md) | LED control page in the browser, with live sync over a WebSocket | led, web-interface |
| [pins](modules/pins/README.md) | GPIO, ADC, PWM and I2C access from the command line | |
| [scheduler](modules/scheduler/README.md) | Runs stored commands on a weekly schedule | time |
| [time](modules/time/README.md) | NTP time sync and automatic timezone detection | wifi |
| [web-interface](modules/web-interface/README.md) | An HTTP page and command endpoint for other devices on the network | wifi |
| [wifi](modules/wifi/README.md) | Joins a WiFi network and keeps the connection alive | |

[doc/modules.md](doc/modules.md) is the generated index with ids, versions and the XeWeCore range each module needs. The unfinished `fan` and `mlx90614` modules are parked outside the repository until they are ready.

---

## Development

Nothing here compiles on its own. Modules build and test inside an xewe-os project used as the harness, with `XEWE_MODULES_SOURCE` pointing at this checkout:

```sh
<harness>/build/tools/.venv/bin/python tools/validate.py --harness <harness>
cd <harness> && build/tools/.venv/bin/python -m xewe test --unit-only
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips
```

[doc/CONTRACT.md](doc/CONTRACT.md) is the specification every module follows, and its section 4 has the full test gate. CI runs from [.github/workflows/tests.yml](.github/workflows/tests.yml). Maintainers release with `scripts/release.sh`, which [scripts/README.md](scripts/README.md) describes.

## Layout

- [modules/](modules/README.md) holds one folder per module.
- [doc/](doc/CONTRACT.md) holds the module contract and the generated module index.
- [tools/](tools/README.md) holds the validator.
- [scripts/](scripts/README.md) holds the release script.
- `libraries.toml` pins the Arduino libraries that modules may depend on.

## License

GPL-3.0-only, see [LICENSE.txt](LICENSE.txt).
