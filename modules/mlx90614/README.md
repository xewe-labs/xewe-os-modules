# mlx90614 — MLX90614 contactless I2C temperature sensor

XeWe OS module · extracted 2026-10-09 from the XeWe laptop cooling pad (MIGRATION.md CP1/CP10) · Solo: Max Dokukin · Status: Draft (0.1.0)

## Overview

Reads an MLX90614 infrared thermometer over I2C (`Wire`): object and ambient temperature, every
500 ms while it answers and every 5 s while it does not. Other code subscribes with a listener
(`Mlx90614Listener::on_temperature`), e.g. a project that feeds the `fan` module's curve. A module
for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- Error replies are errors, not temperatures: a short read is drained, and a word with the sensor's
  error flag (bit 15; `0xFFFF` from a glitched bus would read as 1037.5 °C) counts as a failed read;
  the sensor is then reported offline (NaN, JSON `null`, `Read errors: N` in status)
- Address validated (`0x01`–`0x7F`, hex with or without `0x`, the whole token must parse)
- `scan` is bounded: 10 ms per probe, 1.5 s in total, and it stops after 3 bus errors/timeouts in a row
  (a bare bus without pull-ups used to block the console for tens of seconds)
- Value mapping in `src/Mlx90614/Convert.h`, pure C++ (no Arduino), host-tested with g++
- Settings in one FlexData blob (`mlx90614/data`, schema 1); a blob with another schema is never
  overwritten at boot
- First boot: pins and address from the `MLX90614_*` build defines

## How it works

```
loop(): every 500 ms (5 s offline) → read RAM 0x07 (object), 0x06 (ambient) → raw_to_celsius → listeners
$mlx90614 set_pins 4 5 → validate → NVS → Wire.end/begin → poll → "Pins updated to SDA=4 SCL=5."
```

- **`Mlx90614` class** (`src/Mlx90614/`): a `xewe::Module` with id `mlx90614`, name `MLX90614`
  (`declare=Mlx90614 mlx90614(os);`); no first-boot questions (only the generic "enable?" prompt);
  can be disabled (`get_temp()` is then NaN).
- `Wire` is owned by this module: another module that also calls `Wire.begin` on other pins would
  move the bus.

### Build defines

Pass with `xewe build --define KEY=VALUE`; used on the first boot only (or after `$mlx90614 reset`).

| Define | C3 / C6 | S3 |
|---|---|---|
| `MLX90614_SDA` / `MLX90614_SCL` | 4 / 5 | 8 / 9 |
| `MLX90614_ADDR` | 0x5A | 0x5A |
| `MLX90614_LISTENERS_MAX` | 4 | 4 |

The C3/C6 pins are the cooling pad's wiring (on the C6, 4/5 are MTMS/MTDI: harmless with I2C
pull-ups unless a JTAG-select eFuse is burnt). 255 = not configured.

### Commands

**Prefix:** `$mlx90614` · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`read`** | Read the sensor now. | `$mlx90614 read` |
| **`scan`** | Scan the I2C bus (bounded, see above). | `$mlx90614 scan` |
| **`set_addr`** | Set the sensor address (hex, 0x01-0x7F); persisted. | `$mlx90614 set_addr 0x5A` |
| **`set_pins`** | Set SDA/SCL (two different output-capable GPIOs), restart the bus; persisted. | `$mlx90614 set_pins 4 5` |
| **`print_json`** | `{"module","online","object_temp","ambient_temp","i2c_address","sda_pin","scl_pin","read_errors"}` (temperatures `null` while offline) | `$mlx90614 print_json` |
| `status` / `reset` / `enable` / `disable` | generic module commands | `$mlx90614 status` |

### C++ API

```cpp
mlx90614.get_temp();  mlx90614.get_ambient_temp();   // °C, NaN while offline or disabled
mlx90614.is_online();  mlx90614.get_error_count();  mlx90614.set_i2c_address(0x5A);  mlx90614.set_pins(4, 5);

struct MyListener : Mlx90614Listener {
    void on_temperature(float object_c, float ambient_c, bool online) override { /* main loop; keep it short */ }
} my_listener;
mlx90614.add_listener(&my_listener);   // false when all MLX90614_LISTENERS_MAX (4) slots are taken
```

Listeners are called after every poll (also offline: `online = false`, both temperatures NaN), from
the main loop.

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.0.0,<3.0.0; ArduinoJson (`depends_libraries`, pinned in `libraries.toml`); Wire (esp32 core) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`python -m xewe test --module mlx90614` in a harness (repo [README](../../README.md)).
Unit (`tests/unit/test_mlx90614.py`, `--unit-only`): value mapping (Python mirror), `test_convert.cpp`
with g++ (raw → °C with the error flag, address parsing, scan error streak), header purity.
Board (`tests/board/test_mlx90614.py`): `test_compiles` without a board; on a provisioned bare board
`test_status`, `test_set_addr_validation`, `test_set_pins_rejects_same_pin`,
`test_settings_survive_restart` (address across `$system restart`, restored); free pins
`XEWE_TEST_MLX_SDA`/`XEWE_TEST_MLX_SCL` for `test_scan_bare_bus_is_bounded` (10 s limit);
`test_read_temperature` requires a sensor (`XEWE_TEST_MLX_PRESENT=1`).

## Known issues

- The PEC byte is read but not checked (the error flag and the 3-byte rule catch the bus glitches
  seen so far).
- `scan` still blocks the console for up to ~1.5 s by design.
