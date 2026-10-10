# led — addressable LED strip and its modes

## Overview

Owns the strip and what it shows: chipset, pins, length, colour order, a pixel buffer, brightness
with on/off fades, a FreeRTOS render task that pushes frames through
[FastLED](https://github.com/FastLED/FastLED) at 50 fps, and seven effect modes (the catalogue of xewe-led-os)
with with clamped, persisted parameters and a 900 ms cross-fade on every change. Each mode is
one header in `src/Led/modes/`; `modes/Registry.h` is the one list, so a mode is added or removed with
one file and one line. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- No first-boot questions: defaults come from build defines, `$led set <key> <value>` changes them; the strip settings are a settings table (`Led::settings()`: validated `set`, `get`, `schema`, status lines, loaded before begin); one NVS namespace `led` holds them, `mode_id` and every mode parameter (`m:<mode id>:<key>`). Key names and types match xewe-led-os, so a device moved from it keeps its strip settings
- `$led schema` lists the table rows and every mode parameter (`"group":"mode:<name>"`, `"set":"$led mode param <id> <key> <v>"`); `$system schema` includes them
- Modes are pure functions over an RGB buffer (`modes/*.h`, shared maths in `fx/Math.h`): no Arduino, no FastLED, so they compile and run on the developer machine (`tests/unit/test_effects.cpp`, a pinned CRC per mode)
- Modes are found by a **stable id** declared in the mode file, never by position: removing a mode never renumbers the others or their stored parameters
- Render task `led_render` (stack 4096, priority 3, core 0, 20 ms period) renders under a mutex and calls `FastLED.show()` outside it; brightness fades over 500 ms
- `$led checksum` prints a CRC-32 of the current frame (logical RGB, before brightness and colour order): frame generation is testable without LEDs
- Change listeners (`LedListener`) for integrations such as a web page: brightness, state, mode, colour, parameter

## How it works

```
render task (50 fps): lock → fill override | current mode (fading? previous + current, blended by elapsed/900 ms)
                      → CRC-32 → brightness scale + colour order into the CRGB output buffer → unlock → FastLED.show()
$led mode set 5       → find_mode(5) → load params (defaults ← NVS, clamped) → prepare state (main loop)
                      → swap under the render mutex (old → previous, new → current, fade armed) → NVS mode_id → listeners
$led set num_led 30   → names translated (chip, colorder) → core table: u16 in [1, LED_STRIP_NUM_LEDS_MAX]
                      → member + NVS → on_setting_changed → applied on the next frame (old tail sent black once)
```

- **`Led` class** (`src/Led/`) — a `xewe::Module` with id `led` (`declare=Led led(os);`); cannot be disabled and needs no init setup, so it begins without prompts.
- **Pins are settings.** `pin_data`/`pin_clock` default to `LED_PIN_DATA`/`LED_PIN_CLOCK` and apply after a restart (RESTART rows). Clockless chips (WS281x, SK6812) start on any GPIO FastLED can drive: the build pin uses FastLED's own driver, another pin a small copy of FastLED 3.10.3's RMT5 `ClocklessController` with the pin as a constructor argument (`LedRmtController`, Led.cpp). APA102 takes both pins as FastLED template arguments, so it always runs on the build pins (a different stored pair prints one line at boot); so do all chips when FastLED does not use its RMT5 driver. The pins are claimed in the core's registry (`xewe::pins::claim(pin, "led")`) at begin and released by `$led reset`; a pin held by another module falls back to the build pin, and a strapping pin gets the core's warning. `chip` also applies after a restart; `num_led`, `colorder`, `brightness` and `state` apply at once.
- **Allocation rule.** A mode's state and the two fade buffers are sized on the main loop (`prepare` in `set_mode`/`set_param`/...); the render task only reallocates when the strip length changed since.
- **Colour** — `mode color <rrggbb>` converts to HSV and sets the current mode's `hue`/`sat`; Rainbow and Christmas Lights have no colour. Hue → colour per mode: Color Fade, Brightness Fade and Rainbow use `hsv_rainbow()`, a local re-implementation of FastLED 3.10.3 `hsv2rgb_rainbow` (bit-exact for all 2²⁴ inputs); Solid, Pulse and the status colour use `hsv_spectrum()`, which calls core `xewe::color::hsv_to_rgb` (`<XeWeCore/Utils/Color.h>`, host-includable); Color Fade Two Zone uses a six-sector `hsv()`.
- Frames are close to, not bit-identical with, FastLED-rendered ones: noise is value noise instead of FastLED's Perlin `inoise8/16`. Christmas Lights' flicker offsets are seeded from `esp_random() ^ millis()` at every mode start.

### Build defines

Pass with `xewe build --define KEY=VALUE` (release-matrix columns use the same names). Strings carry their quotes.

| Define | Default | Meaning |
|---|---|---|
| `LED_PIN_DATA` | S3: 48, C3/C6: 8 | data pin; default of `pin_data` (C3/C6 GPIO 8 is a strapping pin: the core warns) |
| `LED_PIN_CLOCK` | S3: 12, C6: 21, C3: 4 | clock pin (APA102 only); default of `pin_clock` |
| `LED_STRIP_NUM_LEDS_MAX` | 2000 | buffer size (6 B RAM per LED: frame + output) |
| `LED_COUNT` | 60 | default length |
| `LED_CHIPSET` | `"WS2812B"` | default chip, a name from the table below |
| `LED_COLOR_ORDER` | `"GRB"` | default colour order |
| `LED_VOLTAGE` | 5 | supply voltage, for the power estimate only |
| `LED_LISTENERS_MAX` | 6 | listener slots |

### Chipsets

Compiled in: APA102 (0, clocked), SK6812 (19), WS2811 (38), WS2812 (40), WS2812B (41). The ids are
stored (`chip`) and match xewe-led-os. Each compiled-in chip instantiates a FastLED driver, so the
other ids of the range 0–45 are not compiled in until a device needs them: APA102HD, APA104, APA106, DOTSTAR, DOTSTARHD, GE8822, GS1903, GW6205, GW6205_400KHZ, HD107,
HD107HD, LPD1886, LPD1886_8BIT, LPD6803, LPD8806, NEOPIXEL, P9813, PL9823, SK6822, SK9822, SK9822HD,
SM16703, SM16716, SM16824E, TM1803, TM1804, TM1809, TM1812, TM1829, UCS1903, UCS1903B, UCS1904,
UCS1912, UCS2903, WS2801, WS2803, WS2811_400KHZ, WS2813, WS2815, WS2816, WS2852. Adding one is a row in
`Chipsets.h` and a `case` in `Led::add_leds()`; the unit test checks the two agree.

### Modes

| id | File | Name | Parameters `key[min..max]=default` |
|---|---|---|---|
| 0 | `Solid.h` | Solid | `hue[0..255]=0 sat[0..255]=255` |
| 1 | `ColorFade.h` | Color Fade | `hue=195 sat[0..245]=245 speed[1..50]=4 fire_step[1..255]=20 h_gap[0..65535]=15000 min_bright=150` |
| 2 | `ColorFadeTwoZone.h` | Color Fade Two Zone | `hue=81 hue_b=225 blend[2..255]=150 speed[1..50]=3 fire_step=10 min_bright=245 min_sat=215` |
| 3 | `BrightnessFade.h` | Brightness Fade | `hue=0 sat=255 speed[1..50]=5 noise_step[1..255]=10 min_bright=10` |
| 4 | `Pulse.h` | Pulse | `hue=0 sat=255 speed[1..255]=30` (beats per minute) |
| 5 | `Rainbow.h` | Rainbow | `speed[1..20]=5 density[1..30]=10` |
| 6 | `ChristmasLights.h` | Christmas Lights | `density[1..10]=1 speed[0..20]=5` |

Ids, names, keys, ranges and defaults match xewe-led-os. `hue` wraps around (256 → 0); every other
parameter is clamped to its range. A stored `mode_id` that is not compiled in falls back to the first
row of `MODES[]`.

### Adding a mode

Add a mode = one file + one line in `Registry.h` (two, counting its `#include`):

1. Copy `src/Led/modes/Solid.h` to `src/Led/modes/<Name>.h`; rename the namespace and the `MODE_<NAME>` constant.
2. Pick an id no other mode uses (0–99, never reuse a removed one: stored `m:<id>:<key>` values would be read by the new mode).
3. Fill `PARAMS[]` (one `ParamDef` per line, at most `MAX_PARAMS` = 8; key ≤ 10 characters so `m:<id>:<key>` fits NVS's 15), `color()` and `render()`; add `prepare()` only if the mode keeps per-pixel state (`ModeState::pixels`/`words`), else pass `nullptr`.
4. In `Registry.h`: `#include "<Name>.h"` and a row `MODE_<NAME>,` in `MODES[]` (row order is the `mode list` and web page order).
5. Add the mode's pinned CRC to `tests/unit/test_effects.cpp` (the test fails until you do; run it once to read the value).

`tests/unit/test_led.py` checks that every file in `modes/` is included and listed once, ids and names
are unique, and every key fits NVS. Removing a mode is the reverse; the other ids do not change.

### Commands

**Prefix:** `$led`

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | The table rows (`chip: 41`, `num_led: 60`, `colorder: 2`, `voltage: 5`, `brightness: 128`, `state: true`, `pin_data: 8`, `pin_clock: 4`; stored values), then the chip and pins in use (`(restart to apply ...)` when the stored ones differ), colour order name, `Output: on, 128/255` (live: a transient off or `brightness 0` shows here), fps, power, source (`fill` or `mode`), current mode, its colour, fade state and parameter values with ranges. | `$led status` |
| **`reset`** | Clear the whole `led` namespace (strip settings, mode and every mode parameter; back to build defaults and mode 0) and restart. | `$led reset` |
| **`on`** / **`off`** | Fade in / out (state persisted). | `$led on` |
| **`brightness`** | Set brightness 0–255 (persisted). `0` darkens the strip with State on and keeps the last non-zero value in NVS; status shows `0/255 (stored N)`, and a restart or off → on comes back at N. | `$led brightness 128` |
| **`set`** | Set a table row (`$led schema`): `chip` (name or id, after restart), `num_led` (1–max), `colorder` (RGB…BGR or 0–5), `voltage` (1–48), `brightness` (1–255), `state` (true/false), `pin_data`/`pin_clock` (GPIO, after restart). Module-owned: translates names and the alias keys `length`/`color_order`, then the core's table path, which replies `num_led=60`, or `! $led set num_led: expected u16 in [1, 2000]`. | `$led set num_led 60` |
| **`get`** | Print one row (core). | `$led get pin_data` |
| **`schema`** | JSON lines: the 8 table rows, then one row per mode parameter (`"group":"mode:rainbow"`, `"set":"$led mode param 5 speed <v>"`), then `{"end":"led","count":N}` (core + `schema_extra`). | `$led schema` |
| **`fill`** | Show a static colour `rrggbb` over the mode, optionally cross-faded from the current frame over `<ms>` (0–60000); `off` (or any mode change) resumes the mode. | `$led fill ff0000 500` |
| **`checksum`** | CRC-32 of the current frame (pre-brightness RGB, `length` pixels). | `$led checksum` |
| **`mode list`** | All modes with their parameter keys; `*` marks the current one. | `$led mode list` |
| **`mode set`** | Select a mode by id or name (`rainbow`, `color_fade_two_zone`). | `$led mode set 5` |
| **`mode param`** | Set a parameter of any mode: `<mode> <key> <value>` (clamped, persisted). | `$led mode param 5 speed 7` |
| **`mode color`** | Set the current mode's colour `rrggbb`; with no argument print it (`Led: color 00ff00`, the status `Color:` value). | `$led mode color ff8000` |
| **`mode reset_params`** | Reset a mode's parameters to their table defaults; no argument: the current mode. One NVS write per parameter, one cross-fade. | `$led mode reset_params 5` |
| **`mode speed`** | Set the current mode's speed. | `$led mode speed 10` |
| xewe-led-os names | `set_brightness N`, `set_state 0\|1`, `toggle_state`, `turn_on`, `turn_off`, `set_length N`, `set_color_order XYZ`, `set_mode <m>`, `set_mode_param <m> <key> <value>`: aliases with the same handlers as above. | `$led set_mode 5` |

`mode` is registered once per argument count (1, 2 and 4) and dispatches on its first argument; any
other combination prints `Led: usage: $led mode list | set <m> | ...`. Replies start with `Led:`.

### NVS keys (namespace `led`)

| Key | Type | Meaning |
|---|---|---|
| `chip` | u8 | chip id 0–45 (applies after restart) |
| `num_led` | u16 | length 1–`LED_STRIP_NUM_LEDS_MAX` |
| `colorder` | u8 | colour order index (RGB=0 … BGR=5) |
| `voltage` | u8 | supply voltage 1–48 |
| `brightness` | u8 | last non-zero brightness 1–255 (a stored 0 is reported once and replaced by 128) |
| `state` | bool | on/off |
| `pin_data` | u8 | data GPIO 0–48 (applies after restart) |
| `pin_clock` | u8 | clock GPIO 0–48, APA102 (applies after restart) |
| `mode_id` | u8 | current mode id |
| `m:<id>:<key>` | u16 | one per mode parameter (27 today; longest `m:2:min_bright`, 14 chars) |

The first eight are the settings table rows (`Led::settings()`): the core loads them before begin
(out of range: default + one `!` line) and `$led reset` reloads the defaults.

### C++ API

```cpp
led.set_brightness(200, origin);  led.get_brightness();   led.set_state(true, origin);  led.get_state();
led.set_state(false, origin, /*persist=*/false);   // off without an NVS write (also set_brightness(v, origin, false))
led.set_mode(5, origin);          led.get_mode();         led.set_param(5, "speed", 7, origin);
led.get_param(5, "speed");        led.set_color({255, 128, 0}, origin);   led.get_color();   // rrggbb
led.set_speed(7, origin);         led.reset_params(5, origin);
led.get_length();  led.get_max_length();  led.get_fps();  led.fill({255, 0, 0}, /*fade_ms=*/500);  led.clear_fill();
led.get_frame_checksum();  led.set_setting("colorder", "GRB");   // `$led set` (prints)
led.apply_setting("num_led", "30");                               // the core's table path, silent; false when refused
// the registry, e.g. for a JSON list of modes (modes/Registry.h, namespace led_fx):
for (const led_fx::ModeDef& m : led_fx::MODES) { m.id; m.name; m.params[i].key; m.param_count; }
const led_fx::ModeDef* m = led_fx::find_mode(id);   // nullptr when not compiled in
```

`origin` defaults to `nullptr` everywhere. The registry namespace is `led_fx`, not `led`: the sketch
declares the object `led`, and a namespace of the same name would clash with it.

### Listeners

Other modules learn about changes through `LedListener` (`src/Led/LedListener.h`, standard library
only):

```cpp
struct MyListener : LedListener {
    void on_brightness(uint8_t value, const void* origin) override { if (origin != this) dirty = true; }
    void on_state(bool on, const void* origin) override             { if (origin != this) dirty = true; }
    void on_mode(uint8_t mode_id, const void* origin) override      { if (origin != this) dirty = true; }
    // also on_color(uint32_t rrggbb, origin), on_param(mode_id, key, value, origin); default bodies are no-ops
};
led.add_listener(&my_listener);              // false when all LED_LISTENERS_MAX (6) slots are taken
led.set_brightness(200, &my_listener);       // origin: my_listener skips this echo
```

- The set is the core's `xewe::ListenerSet<LedListener, LED_LISTENERS_MAX>` (`LedListeners`): 6 pointers, no heap; `remove_listener()` frees a slot.
- Setters call the listeners only when a value changed, after NVS and outside the render mutex, in the
  task that called the setter (the main loop: CLI, web handlers, buttons, scheduler). The render task
  never calls a listener. `set_state(true)` from off also reports the restored brightness; a mode
  switch reports `on_mode` + `on_color`; a parameter change `on_param` per changed value and `on_color`
  when the current mode's colour moved.
- `notify_listeners(fn)` is the fan-out, public for code that adds its own events.
- Keep callbacks short: set a flag and do the work in your `loop()`.

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.1.0,<3.0.0 (settings table, `ListenerSet`, `pins`, `str::parse_hex_color`, host-includable `Color.h`); FastLED 3.10.3 (`depends_libraries`, pinned in `libraries.toml`, installed by `xewe setup`; a project's `xewe.toml` `[libraries]` pin wins) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

FastLED 3.10.3 compiles on all three chips with core 3.3.12; on the S3 its I2S parallel driver adds 6
`-Wdeprecated-declarations` warnings from FastLED's own sources. 3.10.4–3.10.6 add ~100–230 KB of
flash and other warnings, so 3.10.3 stays pinned.

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/board/test_led.py` (board tests) and `tests/unit/` (unit tests), run through an xewe-os
harness (see the repo [README](../../README.md)). No strip is needed:
settings, modes, parameters, fps and the frame checksum are read back over serial (`$led checksum` of
Solid red is compared with the expected CRC); the visual checks are skipped (`requires hardware`).
Unit tests (`tests/unit/test_led.py`, `--unit-only`) build `test_effects.cpp` (registry lookups, a
pinned CRC per mode, maths, cross-fade, seeds) and `test_listeners.cpp` (capacity 6, no duplicates,
order, echo suppression, default no-ops) with g++ `-I <core>/src` (the core checkout next to the
modules repo, a project's `build/libraries/XeWeCore`, or `XEWE_CORE_SOURCE`; skipped when none is
found), check the registry against the mode files, the command table, the settings table (keys,
types, RESTART rows, no hand-written NVS reads), the pin claims, that the mode headers stay pure,
and that setters notify outside the lock and never from the render task.

```sh
build/tools/.venv/bin/python -m xewe test --module led              # lock chip
build/tools/.venv/bin/python -m xewe test --module led --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module led --unit-only  # unit tests only, no build
```

## Known gaps

- xewe-led-os keys that this module does not read: the "parallel lines" keys (`lines`, `l_<i>_cnt`,
  used there only for the power report) and the mode parameters under namespace `mc`. They stay in
  NVS, ignored.
- `$led set num_led` and `fill` do not notify listeners.
- A change that arrives during a cross-fade restarts the fade from the newer mode.
- The default data pin on C3/C6 is GPIO 8, a strapping pin: the core warns at boot, and
  `$led set pin_data <gpio>` moves it.
- `pin_clock` has no effect on APA102 (template pins: build pins only); there is no run-time clocked
  driver.
- Not yet checked on real strips: chip timing, colour order, flicker with WiFi active, and the power
  estimate.
