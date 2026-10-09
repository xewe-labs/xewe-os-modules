# led-modes — LED effects for led-strip

XeWe OS module · created 2026-10-09 (ported from xewe-led-os 2.3.x `ModeController` + `Modes`) · Solo: Max Dokukin · Status: Draft (0.1.0)

## Overview

The effect catalogue of xewe-led-os: seven modes, each with clamped parameters, a 900 ms cross-fade
on every change, and persistence of the selected mode and every parameter. It registers itself as
the frame source of [led-strip](../led-strip); brightness, on/off, length and output stay there.
A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- Effects are pure functions in `src/LedModes/Effects.h` over an RGB buffer, with a small local colour maths (no Arduino, no FastLED), so they compile and run on the host (`tests/host/test_effects.cpp`)
- Parameter tables (key, range, default, step, basic/advanced) are data; the host test parses them and checks ranges, unique keys and NVS key lengths
- NVS namespace `led_modes`: `mode_id` and `m:<mode id>:<key>` (uint16, ≤ 15 chars), the 2.3.x key shape
- Mode ids, names, parameter keys, ranges and defaults are those of 2.3.x

## How it works

```
$led_modes set 5 → load params (defaults ← NVS, clamped) → swap under the strip's render mutex
                   (old mode → previous, new → current, fade armed) → NVS mode_id
render (strip task, 50 fps): fading? render previous + current, blend by elapsed/900 ms : render current
```

- **`LedModes` class** (`src/LedModes/`) — a `xewe::Module` with id `led_modes` and a `LedFrameSource`; takes `LedStrip&` (`declare=LedModes led_modes(os, led_strip);`, `depends_modules=led-strip`); cannot be disabled, no prompts.
- **Colour** — `color <rrggbb>` converts to HSV and sets the current mode's `hue`/`sat` (2.3.x behaviour); Rainbow and Christmas Lights have no colour.
- **Speed** — `speed <n>` sets the current mode's `speed` parameter (clamped to its range).
- Frames are close to, not bit-identical with, 2.3.x: noise is value noise instead of FastLED's Perlin `inoise8/16`.
- Hue → colour is the 2.3.x mapping per mode: Color Fade, Brightness Fade and Rainbow use `hsv_rainbow()`, a local re-implementation of FastLED 3.10.3 `hsv2rgb_rainbow` (2.3.x `CHSV`/`fill_rainbow`, bit-exact for all 2²⁴ inputs); Solid, Pulse and the status colour use `hsv_spectrum()`, the same float code as core `xewe::color::hsv_to_rgb` (bit-exact); Color Fade Two Zone keeps its six-sector `hsv()`. As in 2.3.x, `color <rrggbb>` is converted with spectrum HSV, so in the three rainbow modes the drawn hue is FastLED's rainbow hue for that value.
- Christmas Lights' flicker offsets are seeded from `esp_random() ^ millis()` at every mode start (2.3.x `random16()`); `led_fx::prepare(mode, state, n, seed)` takes the seed explicitly, and seed 0 keeps a fixed default so the host test stays deterministic.

### Modes

| id | Name | Parameters `key[min..max]=default` |
|---|---|---|
| 0 | Solid | `hue[0..255]=0 sat[0..255]=255` |
| 1 | Color Fade | `hue=195 sat[0..245]=245 speed[1..50]=4 fire_step[1..255]=20 h_gap[0..65535]=15000 min_bright=150` |
| 2 | Color Fade Two Zone | `hue=81 hue_b=225 blend[2..255]=150 speed[1..50]=3 fire_step=10 min_bright=245 min_sat=215` |
| 3 | Brightness Fade | `hue=0 sat=255 speed[1..50]=5 noise_step[1..255]=10 min_bright=10` |
| 4 | Pulse | `hue=0 sat=255 speed[1..255]=30` (beats per minute) |
| 5 | Rainbow | `speed[1..20]=5 density[1..30]=10` |
| 6 | Christmas Lights | `density[1..10]=1 speed[0..20]=5` |

`hue` wraps around (256 → 0); every other parameter is clamped to its range.

### Commands

**Prefix:** `$led_modes` · requires led-strip

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | Current mode, its colour, fade state and parameter values with ranges. | `$led_modes status` |
| **`reset`** | Clear the `led_modes` namespace (defaults, mode 0) and restart. | `$led_modes reset` |
| **`list`** | All modes with their parameter keys; `*` marks the current one. | `$led_modes list` |
| **`set`** | Select a mode by id or name (`rainbow`, `color_fade_two_zone`). | `$led_modes set 5` |
| **`param`** | Set a parameter of any mode: `<mode> <key> <value>` (clamped, persisted). | `$led_modes param 5 speed 7` |
| **`color`** | Set the current mode's colour `rrggbb`; with no argument print it (`Led Modes: color 00ff00`, the status `Color:` value). | `$led_modes color ff8000` |
| **`reset_params`** | Reset a mode's parameters to their table defaults (2.3.x `reset_current_mode`); no argument: the current mode. One NVS write per parameter, one cross-fade. | `$led_modes reset_params 5` |
| **`speed`** | Set the current mode's speed. | `$led_modes speed 10` |

C++ API: `set_mode(id, origin)`, `set_param(id, key, value, origin)`, `set_color(rgb, origin)`,
`set_speed(value, origin)`, `reset_params(id, origin)` (`origin` defaults to `nullptr`), `get_mode()`,
`get_param(id, key)`, `get_color()` (rrggbb as `uint32_t`, the mode's base colour as 2.3.x
`Mode::get_rgb`: white for Rainbow, fixed amber for Christmas Lights). Each setter reports the change
through led-strip's listeners (see led-strip "Listeners"): `on_mode` + `on_color` on a mode switch,
`on_param` per changed value, `on_color` when the current mode's colour moved. Called in the caller's
task (main loop), never from the render task.

In 2.3.x these were `$led set_mode`, `adj_mode`, `set_mode_param`, `set_rgb`, ...; stored schedules
or button bindings with those strings must be rewritten (see the xewe-led-os v2 README).

### Requirements

| | |
|---|---|
| Modules | [led-strip](../led-strip) |
| Libraries | XeWeCore >=2.0.0,<3.0.0 (FastLED comes in through led-strip) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/test_led-modes.py`, run through an xewe-os harness whose lock lists FastLED (see the repo
[README](../../README.md)). Host tests (`--host-only`) parse the parameter tables and build
`tests/host/test_effects.cpp` with g++ (skipped without g++). Board tests read modes and parameters
back, `color` with no argument against status, `reset_params` (both forms; **unverified**, written without a
board), compare `$led checksum` against the expected CRC of a pure-red frame, check that it changes with
the mode, and check persistence across `$system restart`; the visual check is skipped (`requires hardware`).

```sh
build/.venv/bin/python -m xewe test --module led-modes              # lock chip
build/.venv/bin/python -m xewe test --module led-modes --all-chips  # c3, c6, s3
build/.venv/bin/python -m xewe test --host-only
```

## Known gaps

- 2.3.x `adj_*` relative commands and `get_mode_params` JSON are not ported (`param` and `status` cover the use; `reset_current_mode` is `reset_params`); 2.3.x stored mode params under namespace `mc` (and `mode_id` under `led`), which v2 does not read.
- The cross-fade restarts from the newer mode when a change arrives mid-fade (2.3.x froze the half-blended frame).
- Visual equivalence with 2.3.x on real strips is unverified.
