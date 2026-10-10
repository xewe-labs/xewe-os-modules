# fan — 4-wire PWM fans, tachometer RPM and a temperature curve

XeWe OS module · extracted 2026-10-09 from the XeWe laptop cooling pad (its `Fan` and `FanCurve` modules, consolidated; MIGRATION.md CP1/CP10) · Solo: Max Dokukin · Status: Draft (0.2.0)

## Overview

Drives any number of 4-wire PC fans with 25 kHz, 8-bit PWM (LEDC) and, when a tachometer pin is
given, measures their RPM. A temperature → speed curve (up to 16 points) sets every fan once a second
while something feeds it a temperature: `fan.set_temperature(celsius, origin)` from C++ (a sensor
module's listener, wired in the project) or `$fan temp <°C>` from the CLI. The module depends on no
sensor. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- PWM on any output-capable GPIO (`ledcAttach(pin, 25000, 8)`), speed 0-255
- Tachometer on a FALLING-edge interrupt, 2 pulses per revolution, ≥ 1 s windows, 64-bit pulse maths
  (no wrap on a ringing line), 3-sample median, EMA (alpha 0.3), rounded to 10 RPM, readings above
  10 000 RPM dropped as noise
- A pin is never shared: `add` refuses a PWM pin that is another fan's tach and a tach pin that is
  another fan's PWM or tach (shared ISRs and re-routed LEDC outputs otherwise)
- Curve maths in `src/Fan/Curve.h`, pure C++ (no Arduino), host-tested with g++ (≥ 72 checks):
  interpolation, clamps, validation (sorted, no duplicates, -40..200 °C, ≤ 100 %), schema check,
  `curve set` parsing, and colour helpers for projects that light LEDs along the curve (hex colours:
  the core's `xewe::str::parse_hex_color` / `to_hex_color`)
- Core 2.1: settings table (`curve_ms` u16 100-60000, default 1000; `stale_ms` u32 1000-600000,
  default 10000) with `$fan get|schema`; fans and curve points appear in `$fan schema` as extra rows
  (`"group":"fans"|"curve"`, `"set"` hint); PWM and tach pins are claimed in the core pin registry
  (`xewe::pins`, released on `remove`); curve events via `fan.listeners` (`FanListener`)
- Fail-safe: an unreadable temperature (NaN) or a source silent for `stale_ms` (10 s) counts as hot → every fan at
  the last point's speed
- NVS: one FlexData blob per item, one write per change (`fan/data` the fans, `fan/curve` the curve);
  the curve loop writes RAM only. A blob with another `schema` is never reinterpreted and never
  overwritten at boot (defaults from RAM until the next change)
- First boot (no stored settings): the fans from the `FAN*` build defines and the default curve
  22 °C → 0 %, 27 °C → 100 %, 32 °C → 100 %

## How it works

```
$fan set 3 128            → ledcWrite(3, 128) → NVS fan/data → "Speed updated."
tach ISR → pulse_count++ ; loop(): every ≥ 1 s per fan → rpm → median → EMA → displayed_rpm
set_temperature(31.2, src) → stored with a timestamp; loop(): every 1 s →
                             curve_math::target_speed(points, t | NaN if stale) → set_all(pwm, persist=false)
```

- **`Fan` class** (`src/Fan/`): a `xewe::Module` with id `fan` (`declare=Fan fan(os);`); no
  first-boot questions (only the generic "enable?" prompt); can be disabled.
- The curve is idle until the first temperature arrives, so a project without a sensor keeps full
  manual control. Once active, `set`/`set_all` are momentary (the next curve tick overwrites them);
  `$fan curve set none` empties the curve and hands control back.

### Build defines

Pass with `xewe build --define KEY=VALUE`. Used on the first boot only (or after `$fan reset`); then
the fans live in NVS and change with `$fan add/remove`. 255 = no fan / no tachometer.

| Define | C3 / C6 | S3 |
|---|---|---|
| `FAN1_PWM` / `FAN1_TACH` | 3 / 0 | 4 / 5 |
| `FAN2_PWM` / `FAN2_TACH` | 10 / 1 | 6 / 7 |

The C3/C6 values are the cooling pad's wiring; the S3 gets free GPIOs because 0 and 3 are strapping
pins there. No default fans: `--define FAN1_PWM=255 --define FAN2_PWM=255`.

### Commands

**Prefix:** `$fan` · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`add`** | Add a fan without a tachometer. | `$fan add <pwm_pin>` |
| **`add_w_tach`** | Add a fan with a tachometer. | `$fan add_w_tach <pwm_pin> <tach_pin>` |
| **`set`** | Set one fan's speed (0-255); persisted. A non-numeric first argument sets a table row. | `$fan set <pwm_pin> <speed>`, `$fan set curve_ms 500` |
| **`get`** / **`schema`** | A table setting; every row as JSON Lines (core 2.1). | `$fan get stale_ms`, `$fan schema` |
| **`set_all`** | Set every fan's speed (0-255); persisted. | `$fan set_all <speed>` |
| **`remove`** | Remove a fan (PWM off, pin detached). | `$fan remove <pwm_pin>` |
| **`temp`** | Feed a temperature to the curve (as a sensor would) and apply it now. | `$fan temp 30.5` |
| **`curve list`** | Curve points, current temperature and target. | `$fan curve list` |
| **`curve add`** | Add or replace the point at a temperature (speed 0-100 %). | `$fan curve add 40.5 50` |
| **`curve remove`** | Remove the point at a temperature. | `$fan curve remove 40.5` |
| **`curve set`** | Replace the whole curve: `T:P` pairs, comma-separated, any order; `none` empties it. | `$fan curve set 22:0,27:100,32:100` |
| **`print_json`** | `{"fans":[{"pin_pwm","has_tach","speed" (0-100 %),"pin_tach","displayed_rpm","ema_rpm"}],"curve":[{"temp","speed"}],"curve_target"}` | `$fan print_json` |
| `status` / `reset` / `enable` / `disable` | generic module commands; `status` lists fans and the curve | `$fan status` |

`curve` is registered once per argument count (1, 2, 3) and dispatches on its first argument; any
other combination prints `Usage: $fan curve list | add ... | remove ... | set ...`.

### C++ API

```cpp
fan.add(pwm, tach /* 255 = none */);  fan.remove(pwm);  fan.set(pwm, 0..255, persist);  fan.set_all(0..255, persist);
fan.get_rpm(pwm);  fan.count();  fan.get_json();
fan.set_temperature(31.2f, origin);    // NaN = source offline (fail-safe hot); origin shown in status
fan.curve_active();  fan.get_curve_target();  fan.get_curve();  fan.get_curve_json();
fan.curve_add(40.5f, 50, origin);  fan.curve_remove(40.5f, origin);
fan.set_curve(points, /* persist */ false, origin);  fan.save_curve();   // several changes, one NVS write
fan.apply_setting("curve_ms", "500");                                // a table row without the CLI
fan.listeners.add(&l);   // FanListener: on_curve_target(pct, celsius, origin), on_curve_changed(origin)
```

Wiring a sensor (project code, e.g. the cooling pad's `PadWeb`):

```cpp
struct Link : Mlx90614Listener {
    void on_temperature(float object_c, float, bool online) override { fan.set_temperature(online ? object_c : NAN, this); }
} link;
mlx90614.listeners.add(&link);
```

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.1.0,<3.0.0; ArduinoJson (`depends_libraries`, pinned in `libraries.toml`; a project lists it in its `xewe.toml` `[libraries]`) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`python -m xewe test --module fan` in a harness (repo [README](../../README.md)).
Unit (`tests/unit/test_fan.py`, `--unit-only`): builds `test_curve.cpp` with g++ (≥ 72 checks), checks
`Curve.h` stays pure, the properties against the source, and that the R1 fixes stay in place.
Board (`tests/board/test_fan.py`): `test_compiles` runs without a board; on a provisioned bare board
`test_status`, `test_set_rejects_out_of_range`, `test_argc_error`, `test_curve_add_remove`,
`test_curve_set_rejects_bad_specs`, `test_temperature_drives_curve`, `test_settings_survive_restart`
(`$system restart`); free pins `XEWE_TEST_FAN_PIN` (`test_add_set_remove`) and
`XEWE_TEST_FAN_PIN2` (`test_shared_pin_refused`); `test_rpm_reading` requires a real fan
(`XEWE_TEST_FAN_PWM_PIN`, `XEWE_TEST_FAN_TACH_PIN`).

## Known issues

- The tach input has no glitch filter (internal ~45 kΩ pull-up, 25 kHz crosstalk): counts above
  `absolute_max_rpm` are dropped, so a noisy line freezes the shown RPM rather than reading wrong. An
  external 10 kΩ pull-up helps; a ≥ 1 ms edge filter needs a scope on hardware first.
- Disabling `fan` detaches LEDC: most 4-wire fans then run at 100 % (safe direction).
