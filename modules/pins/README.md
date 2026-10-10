# pins — GPIO, ADC, PWM and I2C from the command line

## Overview

GPIO, ADC, PWM and I2C access from the command line. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). It exposes the board's pins as
eleven commands so hardware can be probed and driven without writing or flashing code: read and
write digital levels, set pin modes, read the ADC, attach and drive PWM, and scan an I2C bus.
Combined with the Buttons, Scheduler and WebInterface modules, pin commands become the actions
behind buttons, schedules and HTTP requests.

## Highlights

- Eleven commands covering digital I/O, pin modes with pull resistors, ADC, PWM, I2C scanning and the pin registry
- PWM uses the arduino-esp32 3.x pin-based API (`ledcAttach`, `ledcWrite`, `ledcDetach`): 1 Hz–40 MHz, 1–16 bits
- I2C scan initialises `Wire` on any SDA/SCL pair and probes addresses 0x01–0x77
- Arguments are parsed and validated (`xewe::str::parse_int`) with an error message instead of a crash

## How it works

```
$pins gpio_toggle 8 → parse pin → pinMode(OUTPUT) → digitalWrite(!digitalRead) → print new state
```

- **`Pins` class** (`src/Pins/`) — a `xewe::Module` with id `pins`; no first-boot setup; can be disabled; stateless (nothing stored in NVS).

### Commands

**Prefix:** `$pins` · can be disabled

Direct hardware access without writing code.

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`gpio_read`** | Read the digital level (0 or 1); configures the pin as INPUT. | `$pins gpio_read <pin>` |
| **`gpio_write`** | Set HIGH (1) or LOW (0); configures the pin as OUTPUT. | `$pins gpio_write <pin> <0\|1>` |
| **`gpio_toggle`** | Invert the current state; forces OUTPUT. | `$pins gpio_toggle <pin>` |
| **`gpio_mode`** | Set the mode: `in`, `out`, `in_pullup`, `in_pulldown`. | `$pins gpio_mode <pin> <mode>` |
| **`adc_read`** | Read the raw ADC value (usually 0-4095). | `$pins adc_read <pin>` |
| **`pwm_setup`** | Attach PWM. Frequency 1 Hz-40 MHz, resolution 1-16 bits. | `$pins pwm_setup <pin> <hz> <bits>` |
| **`pwm_write`** | Set the duty cycle (max `2^bits - 1`). | `$pins pwm_write <pin> <duty>` |
| **`pwm_stop`** | Stop PWM (duty 0) and detach the timer. | `$pins pwm_stop <pin>` |
| **`i2c_scan`** | Init I2C on the given pins and scan 0x01-0x77. | `$pins i2c_scan <sda> <scl>` |
| **`claims`** | Every GPIO in the core pin registry with its owner (`GPIO 3: fan`), strapping pins marked. | `$pins claims` |
| **`release`** | Free a GPIO that `$pins` claimed. | `$pins release 9` |

Pin registry: every command claims its pin for `pins` on first use and refuses a pin another module holds (`! GPIO 3 already claimed by fan, refused for pins`), so `$pins` cannot reconfigure a fan, sensor or button pin. `pwm_stop` and `release` free it; `i2c_scan` frees its two pins after the scan.

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.1.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/board/test_pins.py` (board tests) and `tests/unit/test_pins.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module pins              # lock chip
build/tools/.venv/bin/python -m xewe test --module pins --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module pins --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$pins status`), `test_adc_read` (`$pins adc_read <pin>`) and `test_claims_listing` (`$pins claims`) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board. `adc_read` reads `XEWE_TEST_PINS_ADC_PIN` (default 1); nothing is written. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).
