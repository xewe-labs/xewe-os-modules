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
| [fan](modules/fan) | 4-wire PWM fans, tachometer RPM, and a temperature → speed curve that any sensor can feed | |
| [led](modules/led) | An addressable LED strip (FastLED, 50 fps render task) and seven effect modes with cross-fades | |
| [mlx90614](modules/mlx90614) | MLX90614 contactless I2C thermometer (object and ambient), with temperature listeners | |
| [pins](modules/pins) | GPIO, ADC, PWM and I2C access from the command line | |
| [scheduler](modules/scheduler) | Runs stored commands on a weekly schedule | time |
| [time](modules/time) | NTP time sync and automatic timezone detection | wifi |
| [web-interface](modules/web-interface) | An HTTP page and command endpoint for other devices on the network | wifi |
| [wifi](modules/wifi) | Joins a WiFi network and keeps the connection alive | |

[MODULES.md](MODULES.md) is the generated index with ids, versions and the XeWeCore range each module
needs. Each module's README lists its commands, settings, NVS keys, build defines and tests.

## Using modules in a project

A project is a copy of the [xewe-os](https://github.com/xewe-labs/xewe-os) template. Its manifest,
`xewe.toml`, pins this repository and lists the selected modules:

```toml
[modules]
repo = "https://github.com/xewe-labs/xewe-os-modules"
ref = "latest"
selected = ["wifi", "time", "scheduler"]
```

Select modules with `./setup.sh --modules wifi,time` or `xewe modules select wifi,time` (`all` and
`none` work too). Dependencies are added automatically. The tools then:

- copy each module's `src/<Folder>/` into the generated library `build/modules/` (`XeWeModules`);
- write `src/Modules.h`, which declares one object per module, dependencies first
  (`Wifi wifi(os);`, then `Time time_module(os, wifi);`);
- install the Arduino libraries the modules need (FastLED for `led`, ArduinoJson for `fan` and
  `mlx90614`) from [libraries.toml](libraries.toml), unless the project's `xewe.toml` pins them itself.

After a build the modules' commands are on the console: `$help` lists them, `$<id> status` shows a
module's state, and `$system schema` prints every module's settings as JSON lines.

Compile-time values (a pin a driver needs as a constant, a buffer size, listener slots) live in
the project's `Config.h`: `./setup.sh` appends each selected module's `src/<Folder>/Config.h` there
as one marked block (`XEWE_MODULE_<SLUG>_<VAR>` defines), and the user edits the numbers.
First-boot defaults that end up in NVS (a strip length, a fan's pins) stay `--define` values:
`xewe build --define LED_COUNT=30`. Each module README lists both.

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
them. Modules that announce changes (led, fan, mlx90614) offer listeners, and every module claims the
GPIOs it drives in the core's pin registry, so two modules cannot drive the same pin.

[CONTRACT.md](CONTRACT.md) is the full specification every module follows. Contributors and agents
start at [`.agents/AGENTS.md`](.agents/AGENTS.md), which covers adding a module, the validator and
the tests.
