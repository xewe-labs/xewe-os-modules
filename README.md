# xewe-os-modules

Ready-made modules for [XeWe OS](https://github.com/xewe-labs/xewe-os) firmware on ESP32-C3, -C6 and -S3.

## Problem

- Every ESP32 project rewrites its WiFi join, NTP sync and LED driver, and each copy drifts.
- Two features that drive the same GPIO fight over it, and clashing NVS keys overwrite each other.
- Copied code arrives without its tests, so nobody knows whether it works with the current core.

## Solution

Each module is one C++ class built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core), with one contract, one validator and tests per module. A project lists the modules it wants in `xewe.toml`, and the [build tools](https://github.com/xewe-labs/xewe-os-tools) copy them in with their dependencies. Three ideas carry it:

- A module's `id` is its command group (`$wifi ...`) and its NVS namespace, so it never changes once released.
- Every module claims its GPIOs in the core's pin registry, so two modules cannot drive one pin.
- `tools/validate.py` checks every module against [doc/CONTRACT.md](doc/CONTRACT.md).

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

The pin registry refuses a pin another module holds:

```text
$pins claims
GPIO 3: buttons
$pins gpio_write 3 1
! GPIO 3 already claimed by buttons, refused for pins
```

## Why not write the module yourself

A module written in a sketch lives in one project and is tested by hand. These carry unit and board tests that run through `xewe test`. To write your own, start from XeWeCore's `examples/02_MyModule`.

## Install

A project is a copy of the [xewe-os](https://github.com/xewe-labs/xewe-os) template. Its `xewe.toml` pins this repository and lists the selection:

```toml
[modules]
repo = "https://github.com/xewe-labs/xewe-os-modules"
ref = "3.1.0"
selected = ["wifi", "time", "scheduler"]
```

`ref` is a set tag, a module tag `<module>/X.Y.Z` ([scripts/README.md](scripts/README.md#tags)) or `latest`. Install the selection:

```sh
./setup.sh --modules wifi,time,scheduler
```

## Usage

Select modules with `./setup.sh --modules wifi,time` or `xewe modules select wifi,time`. Dependencies are added automatically.

On the serial console, `$help` lists the commands and `$<id> status` shows a module's state. Each module's README lists its commands and settings.

Set build-time values by uncommenting a line in the project's `Config.h`:

```c
// #define XEWE_LED_NUM_LED 600
```

[doc/CONTRACT.md](doc/CONTRACT.md) section 3.2 describes the scheme.

## Status

Eight modules build for c3, c6 and s3 against XeWeCore `>=3.1.0,<4.0.0`:

| Module | What it does | Needs |
|---|---|---|
| [buttons](modules/buttons/README.md) | Binds commands to buttons | |
| [led](modules/led/README.md) | Addressable LED strips, 46 chipsets | |
| [led-web](modules/led-web/README.md) | LED control page in the browser | led, web-interface |
| [pins](modules/pins/README.md) | GPIO, ADC, PWM and I2C commands | |
| [scheduler](modules/scheduler/README.md) | Weekly command schedule | time |
| [time](modules/time/README.md) | NTP sync and timezone detection | wifi |
| [web-interface](modules/web-interface/README.md) | HTTP page and command endpoint | wifi |
| [wifi](modules/wifi/README.md) | Joins and keeps a WiFi connection | |

[doc/modules.md](doc/modules.md) lists versions. `fan` and `mlx90614` are unfinished and parked outside this repository.

---

## Development

Modules build and test inside an xewe-os project used as the harness, with `XEWE_MODULES_SOURCE` set to this checkout:

```sh
<harness>/build/tools/.venv/bin/python tools/validate.py --harness <harness>
cd <harness> && build/tools/.venv/bin/python -m xewe test --unit-only
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips
```

CI runs from [.github/workflows/tests.yml](.github/workflows/tests.yml). Releases are described in [scripts/README.md](scripts/README.md).

## Layout

- [modules/](modules/README.md) holds one folder per module.
- [doc/](doc/CONTRACT.md) holds the module contract and the generated module index.
- [tools/](tools/README.md) holds the validator.
- [scripts/](scripts/README.md) holds the release script.
- `libraries.toml` pins the Arduino libraries that modules may depend on.

## License

GPL-3.0-only, see [LICENSE.txt](LICENSE.txt).
