# led

Drives an addressable LED strip with FastLED. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os),
built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). 46 chipsets (one-wire and clocked),
seven effects with a 900 ms cross-fade between them, software brightness with a 500 ms fade and six
software colour orders. A first-boot wizard asks for the wiring, chip, voltage, length and colour
order; 30 `$led` commands change colour, brightness, state, mode, mode parameters, length and colour
order, and other modules follow every change through a listener interface.

## Highlights

- 46 FastLED chipsets compiled in with the compile-time pins; the stored one is registered once per boot (`Led::set_leds_chipset`)
- Seven effects on FastLED primitives (`inoise8`/`inoise16`, `fill_rainbow`, rainbow `CHSV`, `beatsin8`, `blend`); a mode, colour or parameter change cross-fades for 900 ms
- Frames come from a FreeRTOS render task every 20 ms (`XEWE_LED_FRAME_MS`), the only writer of the pixel buffer; every command and API call changes the state as one step under the render lock (no frame in the middle of it), and NVS writes, console output, listener callbacks and `FastLED.show()` run outside the lock
- Brightness is applied in software (linear 500 ms fade), then the colour order (RGB, RBG, GRB, GBR, BRG, BGR), in the output buffer; FastLED gets RGB order, `TypicalLEDStrip` correction and global brightness 255
- Mode parameters for every mode used are kept in one FlexData blob (`led/mode_params`)
- The 30 command names, arguments and texts are fixed: button and schedule command strings stored on devices keep working

## How it works

```
boot:  settings table (chip, num_led, colorder, ...) → mode 0 built (fades in from black)
       first boot: wizard → strip registered → render task → colour-order calibration
       later:      pins claimed → strip registered → rgb, brightness, mode_id, state applied → render task
task:  every 20 ms: [lock] mode frame → dim → colour order [unlock] → FastLED.show()
cmd:   [lock] $led set_mode 5 → mode built + cross-fade → its colour applied → [unlock]
       → NVS writes → colour announced → mode announced
```

- `src/Led/Led.h` holds the `Led` class, a `xewe::Module` with id `led`; first-boot wizard; cannot be disabled.
- `LedEngine.h` holds `LedEngine`, the strip logic without console, NVS or FreeRTOS (host-tested). `ModeController`, `Brightness` and `modes/` are the effects, `LedCommands` is the command table, and `Chipsets.h` holds the chipset and colour-order tables.
- Other modules call `sync_color`, `sync_brightness`, `sync_state`, `sync_mode`, `sync_length`, `sync_all` (apply, then notify every listener with the caller's `origin`), `set_mode_param`, `reset_current_mode`, and the getters `get_rgb`, `get_hsv`, `get_brightness` (last non-zero), `get_state`, `get_current_mode_id`, `get_current_mode_name`, `get_current_mode_param`, `get_all_modes_json`, `get_length`.

## Commands

Commands start with `$led`. The module runs a setup wizard on first boot and cannot be disabled. Numbers are read like Arduino `toInt()` (leading digits, `abc` is 0) and narrowed to the argument type (`300` → 44 for a 0..255 value); no command reports a bad number.

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`set_rgb`** | Set RGB color | `$led set_rgb 255 0 0` |
| **`set_r`** / **`set_g`** / **`set_b`** | Set one channel | `$led set_r 127` |
| **`adj_rgb`** | Adjust RGB color by deltas (each channel clamps at 0..255) | `$led adj_rgb 10 -20 5` |
| **`adj_r`** / **`adj_g`** / **`adj_b`** | Adjust one channel by a delta | `$led adj_r -10` |
| **`set_hsv`** | Set HSV color (V only scales the colour that becomes hue/saturation, so V=0 gives white) | `$led set_hsv 75 255 0` |
| **`set_hue`** / **`set_sat`** / **`set_val`** | Set one HSV channel | `$led set_hue 255` |
| **`adj_hsv`** | Adjust HSV by deltas (hue wraps, S/V clamp) | `$led adj_hsv 10 -20 5` |
| **`adj_hue`** / **`adj_sat`** / **`adj_val`** | Adjust one HSV channel (their help shows `adj_h`/`adj_s`/`adj_v`) | `$led adj_hue -10` |
| **`set_brightness`** | Set global brightness; 0 fades to black and keeps the state on | `$led set_brightness 255` |
| **`adj_brightness`** | Adjust brightness by a delta, from the fade target | `$led adj_brightness -10` |
| **`set_state`** / **`toggle_state`** / **`turn_on`** / **`turn_off`** | On/off with a 500 ms fade | `$led set_state 0` |
| **`set_mode`** | Set the mode 0..6; an unknown id runs mode 0 (the requested id is stored and announced) | `$led set_mode 0` |
| **`adj_mode`** | Next/previous mode (past 6 back to 0, no wrap below 0) | `$led adj_mode 1` |
| **`set_mode_param`** | Set a parameter of the current mode (`hue` wraps, others clamp; unknown key: nothing) | `$led set_mode_param hue 128` |
| **`adj_mode_param`** | Adjust a parameter of the current mode by a delta | `$led adj_mode_param hue -15` |
| **`get_mode_params`** | Print the current mode's parameters | `$led get_mode_params` |
| **`reset_current_mode`** | Reset the current mode's parameters to defaults | `$led reset_current_mode` |
| **`set_length`** | Set the number of LEDs; more than the maximum prints `That's too many. Max supported: 600 LEDs` | `$led set_length 500` |
| **`set_color_order`** | Set the colour order by name (any case); prints `Color order set to <ORDER>` or `Invalid color order. Use RGB, RBG, GRB, GBR, BRG, or BGR`. Without a name (`$led set_color_order ""`) it runs the wizard's colour-order calibration (waits for answers) | `$led set_color_order GRB` |
| **`set`** / **`get`** / **`schema`** | Settings table (below); `schema` also lists the line lengths, the live colour, brightness, state and mode, and the current mode's parameters | `$led get num_led` |
| **`status`** | `Led module enabled`, one line per setting, then hardware, live state, power estimate and mode | `$led status` |
| **`reset`** | Green, brightness 50, on, mode 0 (announced), stops the output and releases the pins, then wipes the `led` namespace (mode parameters included) and restarts into the wizard | `$led reset` |

Modes (id, name, parameters): 0 Solid (hue, sat) · 1 Color Fade (hue, sat 0..245, speed, fire_step, h_gap, min_bright) · 2 Color Fade Two Zone (hue, hue_b, blend 2..255, speed, fire_step, min_bright, min_sat) · 3 Brightness Fade (hue, sat, speed, noise_step, min_bright) · 4 Pulse (hue, sat, speed 1..255) · 5 Rainbow (speed, density) · 6 Christmas Lights (density, speed 0..20). `$led get_mode_params` shows ranges and values.

## Setup wizard

On the first boot and after `$led reset` (prompts wait for an answer) the wizard asks six questions: clock line (and its pin), chipset (menu of 46), voltage (5/12/24 V), parallel strips (count and the length of each, the longest becomes `num_led`) or one strip length, and the colour order, found by a calibration (the strip shows green, then red; answer what you see; the order found is used from that answer on, so the fade-out already shows the right colours). Before them it confirms the data pin.

Each question is asked only while its `_PROMPT` define exists (see Build defines). A value set in the project's `Config.h` is stored without a question: `XEWE_LED_USE_CLK` answers the clock line, `XEWE_LED_CHIP` the chipset, `XEWE_LED_VOLTAGE` the voltage, `XEWE_LED_LINES` the parallel strips and `XEWE_LED_NUM_LED` the length (the length of every parallel line when `XEWE_LED_LINES` is set, too). The data-pin and clock-pin confirmations appear only while some question still prompts. When `XEWE_LED_COLORDER` is set the calibration is skipped: the module stores that order, shows green at brightness 50 and prints `Color order set to <ORDER>`; `$led set_color_order ""` still runs it. A "no" to a pin question prints how to change pins (`XEWE_LED_PIN_*`) and the setup continues; with wrong pins the calibration shows nothing, and its "Other" answer restarts the device into the wizard.

## Listeners

`LedListener` (`src/Led/LedListener.h`): `on_color(rgb)`, `on_brightness(value)`, `on_state(on)`, `on_mode(id)`, `on_length(n)` and `on_all(rgb, brightness, on, mode, length)` (default: the five in the order state, length, mode, brightness, colour). Add one with `led.listeners.add(&l)` (up to `XEWE_LED_LISTENERS`). Events carry the setter's `origin` (`nullptr` from the CLI; the `Led` object for changes the strip makes itself, such as the colour of a newly selected mode, which is announced before the mode). Callbacks run in the caller's task, outside the render lock, after the whole change has been applied; a callback may call the `Led` API again.

## Requirements

| | |
|---|---|
| Modules | none |
| Libraries | FastLED 3.10.3 (`libraries.toml`), XeWeCore >=3.1.0,<4.0.0 |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

## Settings and NVS keys

| Key (namespace `led`) | Type | Default | Meaning |
|---|---|---|---|
| `use_clk` | bool | false | the strip has a clock line (status only) |
| `chip` | u8 0..45 | 0 (APA102) | chipset id (wizard menu order); applies after a restart |
| `voltage` | u8, 5/12/24 | 5 | strip voltage (power estimate); `$led set` refuses other values before storing anything |
| `lines` | u16 0..max | 0 | parallel strips on the data line (0 = one strip) |
| `num_led` | u16 0..max | max | number of LEDs (`set` behaves like `$led set_length`) |
| `colorder` | u8 0..5 | 0 (RGB) | colour order RGB, RBG, GRB, GBR, BRG, BGR |
| `l_<i>_cnt` | u16 | – | length of parallel line i (wizard) |
| `rgb` | blob, 3 bytes | – | last colour applied |
| `brightness` | u8 | 0 | last non-zero brightness |
| `mode_id` | u8 | 0 | requested mode id |
| `state` | u8 (bool) | false | on/off |
| `mode_params` | FlexData blob | – | `{schema: 1, params: [{mode, key, value}]}` for every mode used |

## Build defines (`src/Led/Config.h`)

All names start with `XEWE_LED_` and hold a fallback in the module's `Config.h`: an uncommented line in the project's `Config.h`, a dotenv value behind `INJECTED_FROM_DOTENV` or `xewe build --define NAME=VALUE` wins (see [build-time values in a project](../../doc/CONTRACT.md)). The file has the two sections of the scheme (default runtime values, compile time defined constants) in [XeWeCore's `doc/CONFIG.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/CONFIG.md).

The file frames its default runtime values with `/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */` and its compile time defined constants with `/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */`.

**Default runtime values**, all asked at first boot. Each pre-answers one wizard question and defines `XEWE_LED_<KEY>_PROMPT` only while the value is not set; the settings row takes the value as its default either way.

| Define | Default | Setting | Meaning |
|---|---|---|---|
| `XEWE_LED_USE_CLK` | 0 | `use_clk` | 1 = the strip has a clock line |
| `XEWE_LED_CHIP` | 0 | `chip` | chipset id 0..45 (wizard menu order) |
| `XEWE_LED_VOLTAGE` | 5 | `voltage` | 5, 12 or 24 V |
| `XEWE_LED_LINES` | 0 | `lines` | parallel strips on the data line, 0 = one strip |
| `XEWE_LED_NUM_LED` | 600 | `num_led` | number of LEDs, per line when `XEWE_LED_LINES` > 0 |
| `XEWE_LED_COLORDER` | 0 | `colorder` | 0..5 = RGB, RBG, GRB, GBR, BRG, BGR; set = no calibration |

In the project's `Config.h`, `xewe modules generate` writes these six lines commented out (`// #define XEWE_LED_NUM_LED 600`), so a fresh project asks everything. Uncomment a line (a bare `#define`) and set the value to pre-answer that question, for example a 144-LED strip with GRB order:

```c
#define XEWE_LED_NUM_LED 144
#define XEWE_LED_COLORDER 2
```

**Compile time defined constants** (the project's `Config.h` lists them commented out; uncomment a line to override):

| Define | Default | Meaning |
|---|---|---|
| `XEWE_LED_PIN_DATA` | 4 | data GPIO |
| `XEWE_LED_PIN_CLOCK` | 3 | clock GPIO (clocked chipsets) |
| `XEWE_LED_NUM_LEDS_MAX` | 600 | buffer size; every frame sends this many pixels |
| `XEWE_LED_MODE_FADE_MS` / `_BRIGHTNESS_FADE_MS` / `_FRAME_MS` | 900 / 500 / 20 | cross-fade, brightness fade, frame period |
| `XEWE_LED_FPS_WINDOW_S` | 3 | FPS window of `$led status` |
| `XEWE_LED_LISTENERS` | 4 | listener capacity |
| `XEWE_LED_TASK_STACK` / `_PRIORITY` / `_CORE` | 4096 / 3 / 0 | render task |

Led Dock V2 pins per chip (the module default is 4 and 3; a release image sets its pins with
`xewe build --define`, a project in its `Config.h`):

| Chip | `XEWE_LED_PIN_DATA` | `XEWE_LED_PIN_CLOCK` |
|---|---|---|
| c3 | 4 | 3 |
| c6 | 20 | 19 |
| s3 | 13 | 12 |

Other pins: uncomment `XEWE_LED_PIN_DATA` and `XEWE_LED_PIN_CLOCK` in the project's `Config.h` and set them, or `xewe build --define XEWE_LED_PIN_DATA=<gpio> --define XEWE_LED_PIN_CLOCK=<gpio>`. The data pin, and the clock pin of clocked chipsets, are claimed in the core pin registry at boot; a pin held by another module leaves the strip without output (reported). Strapping pins are claimed with the core's warning.

## Behaviour kept for stored commands and devices in use

These are deliberate, so that stored button and schedule strings, stored values and the web page behave the same as on devices already in use:

- Every colour or parameter change builds the mode twice (the second time from the colour converted to hue/saturation), so hue or saturation may move by one step (inside the mode's range, see below). The `adj_*` colour commands apply their result twice (two cross-fade restarts, two `rgb` writes).
- `set_mode` announces the new mode's colour (origin: the `Led`) before the mode itself.
- An unknown mode id runs mode 0, but the requested id is stored and announced; `adj_mode` therefore goes past 6 to an unknown id (mode 0 runs) and does not wrap below 0.
- `set_hsv` and `set_val` with V=0 give white: V only scales the colour that is converted back to hue/saturation, and black converts to hue 0, saturation 0.
- At boot the stored colour is applied to mode 0 before the stored mode, so mode 0 takes the colour of the mode that was active last (white after Rainbow).
- `set_mode_param hue` wraps modulo 256 when the mode has a `hue` parameter; every other key, `hue_b` included, clamps to its range. Stored parameter values are used as they are (not clamped on load).
- `set_brightness 0` fades to black but keeps the state on; `adj_brightness` counts from the fade target, and reaching 0 goes back to the last non-zero brightness.
- `set_length` above the maximum prints the error and is still announced to listeners; `reset_current_mode` is not announced.
- The help sample usages of `adj_hue`, `adj_sat`, `adj_val` show `adj_h`, `adj_s`, `adj_v`.

## Corrected behaviour

- Color Fade Two Zone reports Hue A at full saturation as its colour (`get_rgb`), so entering mode 2 gives a defined colour.
- LEDs past `num_led` stay dark from boot on; only `num_led` pixels are rendered.
- The parallel-lines setup stores the longest line as `num_led`.
- `adj_mode_param` with a key the current mode does not have changes nothing (like `set_mode_param`).
- A missing or short stored `rgb` leaves mode 0 at its defaults; a stored `colorder` or `chip` out of range falls back to 0 (RGB, APA102).
- A "no" to a wizard pin question does not stop the setup (see Setup wizard).
- Hue and saturation taken from a colour (`set_rgb`, `set_hsv` and every command that applies a colour, `set_mode` included) stay inside the mode's own range, with the rule of `set_mode_param`: Color Fade's "Min Saturation" (0..245) stops at 245. Earlier versions stored 246..255 there (`$led set_mode 1` gave 246, `$led set_rgb 255 0 0` 255).
- Running the colour-order calibration again on a calibrated strip (`$led set_color_order ""`) finds the strip's order: the test colours are shown in plain RGB order. Earlier versions showed them through the order already set, so a GRB strip answered truthfully came out as RGB.
- `$led set voltage` with a value other than 5, 12 or 24 prints only `! Led: voltage must be 5, 12 or 24; kept <v>` and stores nothing. Earlier versions printed `voltage=<value>` first and wrote NVS twice (the value, then the kept one).

## Known issues

- Every chipset template is linked: about +330 KB flash on s3 and +360 KB on c3/c6 compared with no LED module.
- `FastLED.show()` always sends `XEWE_LED_NUM_LEDS_MAX` pixels (about 18 ms for 600 WS2812); a smaller maximum raises the frame rate.
- Effect speeds of the noise and rainbow modes are per frame, so they follow the frame rate; Pulse follows the clock.
- Christmas Lights draws its flicker offsets from FastLED's unseeded `random16()`; the pattern depends on how many instances were built since boot.
- The ESP32-S3 compiler fuses two float operations of the HSV conversion, so a few hue/saturation pairs give a colour one step apart from C3/C6.

## Tests

`tests/board/test_led.py` (board tests) and `tests/unit/test_led.py` (unit tests, developer machine), run through an xewe-os harness (see the repo [README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module led              # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module led --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module led --unit-only  # unit tests only, no build
```

- Unit: source checks (properties, GPL headers, comments, one file pair per mode, registry ids 0..6, the 46-case chipset switch, the pinned command table, the wizard's prompt sequence, `reset` order), `test_led_engine.cpp` (g++ with FastLED's stub platform: every command's effect, NVS writes, output and listener events; the pinned mode parameter tables and `/modes` JSON; parameter clamping; values taken from a colour kept inside the mode's ranges; the colour-order calibration run twice on a GRB strip; the voltage refusal of `$led set`; tables; status text; no frame inside a command and nothing slow under the render lock), and, with `XEWE_LED_GOLDEN_DIR` set to the reference oracle folder, a golden-frame comparison (frames, cross-fades, brightness curves, NVS) that otherwise skips with that hint. The C++ tests need FastLED and XeWeCore sources (the harness's `build/libraries`, or `XEWE_FASTLED_SOURCE` / `XEWE_CORE_SOURCE`).
- Board: `test_compiles` runs without a board; `test_status`, `test_settings_table`, `test_not_disableable`, `test_cli_errors_are_the_core_texts`, `test_length_and_color_order_messages`, `test_mode_and_params_round_trip`, `test_brightness_round_trip`, `test_render_task_runs` need a board (each restores what it changes); `test_reset_runs_wizard` (last) resets the module and answers the wizard, only with `XEWE_TEST_LED_RESET=1`.
- Hardware precondition: a provisioned board whose led wizard has been answered (its menus need numbers, which `xewe provision` does not give).
