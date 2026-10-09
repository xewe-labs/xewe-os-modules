# led — addressable LED strip and its modes

XeWe OS module · created 2026-10-09 (led-strip + led-modes merged, LM12–LM19; ported from xewe-led-os 2.3.x `LedStrip`, `Brightness`, `ModeController` + `Modes`) · Solo: Max Dokukin · Status: Draft (0.2.0)

## Overview

Owns the strip and what it shows: chipset, pins, length, colour order, a pixel buffer, brightness
with on/off fades, a FreeRTOS render task that pushes frames through
[FastLED](https://github.com/FastLED/FastLED) at 50 fps, and the effect catalogue of xewe-led-os:
seven modes with clamped, persisted parameters and a 900 ms cross-fade on every change. Each mode is
one header in `src/Led/modes/`; `modes/Registry.h` is the one list, so a mode is added or removed with
one file and one line. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- No first-boot questions: defaults come from build defines, `$led set <key> <value>` changes them; one NVS namespace `led` holds the strip settings (2.3.x key names), `mode_id` and every mode parameter (`m:<mode id>:<key>`), so a 2.3.x device keeps its strip settings
- Modes are pure functions over an RGB buffer (`modes/*.h`, shared maths in `fx/Math.h`): no Arduino, no FastLED, so they compile and run on the developer machine (`tests/unit/test_effects.cpp`, a pinned CRC per mode)
- Modes are found by a **stable id** declared in the mode file, never by position: removing a mode never renumbers the others or their stored parameters
- Render task `led_render` (stack 4096, priority 3, core 0, 20 ms period) renders under a mutex and calls `FastLED.show()` outside it; brightness fades over 500 ms
- `$led checksum` prints a CRC-32 of the current frame (logical RGB, before brightness and colour order): frame generation is testable without LEDs
- Change listeners (`LedListener`) for integrations such as the xewe-led-os web page: brightness, state, mode, colour, parameter

## How it works

```
render task (50 fps): lock → fill override | current mode (fading? previous + current, blended by elapsed/900 ms)
                      → CRC-32 → brightness scale + colour order into the CRGB output buffer → unlock → FastLED.show()
$led mode set 5       → find_mode(5) → load params (defaults ← NVS, clamped) → prepare state (main loop)
                      → swap under the render mutex (old → previous, new → current, fade armed) → NVS mode_id → listeners
$led set num_led 30   → clamp 1..LED_STRIP_NUM_LEDS_MAX → NVS → applied on the next frame (old tail sent black once)
```

- **`Led` class** (`src/Led/`) — a `xewe::Module` with id `led` (`declare=Led led(os);`); cannot be disabled and needs no init setup, so it begins without prompts.
- **Pins are build-time.** FastLED takes them as template arguments, so `LED_PIN_DATA`/`LED_PIN_CLOCK` are fixed per firmware image (one image per pin pair, as the 2.3.x release matrix). `chip` changes are stored at once and applied after a restart; `num_led` and `colorder` apply live.
- **Allocation rule.** A mode's state and the two fade buffers are sized on the main loop (`prepare` in `set_mode`/`set_param`/...); the render task only reallocates when the strip length changed since.
- **Colour** — `mode color <rrggbb>` converts to HSV and sets the current mode's `hue`/`sat` (2.3.x behaviour); Rainbow and Christmas Lights have no colour. Hue → colour is the 2.3.x mapping per mode: Color Fade, Brightness Fade and Rainbow use `hsv_rainbow()`, a local re-implementation of FastLED 3.10.3 `hsv2rgb_rainbow` (bit-exact for all 2²⁴ inputs); Solid, Pulse and the status colour use `hsv_spectrum()`, the same float code as core `xewe::color::hsv_to_rgb`; Color Fade Two Zone keeps its six-sector `hsv()`.
- Frames are close to, not bit-identical with, 2.3.x: noise is value noise instead of FastLED's Perlin `inoise8/16`. Christmas Lights' flicker offsets are seeded from `esp_random() ^ millis()` at every mode start.

### Build defines

Pass with `xewe build --define KEY=VALUE` (release-matrix columns use the same names). Strings carry their quotes.

| Define | Default | Meaning |
|---|---|---|
| `LED_PIN_DATA` | S3: 48, C3/C6: 8 | data pin |
| `LED_PIN_CLOCK` | S3: 12, C6: 21, C3: 4 | clock pin (APA102 only) |
| `LED_STRIP_NUM_LEDS_MAX` | 2000 | buffer size (6 B RAM per LED: frame + output) |
| `LED_COUNT` | 60 | default length |
| `LED_CHIPSET` | `"WS2812B"` | default chip, a name from the table below |
| `LED_COLOR_ORDER` | `"GRB"` | default colour order |
| `LED_VOLTAGE` | 5 | supply voltage, for the power estimate only |
| `LED_LISTENERS_MAX` | 4 | listener slots |

### Chipsets

Compiled in (2.3.x ids kept): APA102 (0, clocked), SK6812 (19), WS2811 (38), WS2812 (40), WS2812B (41).
2.3.x offered 46; each one instantiates a FastLED driver, so the rest are left out until a device
needs them: APA102HD, APA104, APA106, DOTSTAR, DOTSTARHD, GE8822, GS1903, GW6205, GW6205_400KHZ, HD107,
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

Ids, names, keys, ranges and defaults are those of 2.3.x. `hue` wraps around (256 → 0); every other
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
| **`status`** | Chip, pins, length, colour order, voltage, brightness, state, fps, power, source (`fill` or `mode`), current mode, its colour, fade state and parameter values with ranges. | `$led status` |
| **`reset`** | Clear the whole `led` namespace (strip settings, mode and every mode parameter; back to build defaults and mode 0) and restart. | `$led reset` |
| **`on`** / **`off`** | Fade in / out (state persisted). | `$led on` |
| **`brightness`** | Set brightness 0–255 (persisted). `0` darkens the strip with State on and keeps the last non-zero value in NVS (2.3.x); status shows `0/255 (stored N)`, and a restart or off → on comes back at N. | `$led brightness 128` |
| **`set`** | Set `chip` (name or id), `num_led` (1–max), `colorder` (RGB…BGR) or `voltage`. | `$led set num_led 60` |
| **`fill`** | Show a static colour `rrggbb` over the mode; `off` (or any mode change) resumes the mode. | `$led fill ff0000` |
| **`checksum`** | CRC-32 of the current frame (pre-brightness RGB, `length` pixels). | `$led checksum` |
| **`mode list`** | All modes with their parameter keys; `*` marks the current one. | `$led mode list` |
| **`mode set`** | Select a mode by id or name (`rainbow`, `color_fade_two_zone`). | `$led mode set 5` |
| **`mode param`** | Set a parameter of any mode: `<mode> <key> <value>` (clamped, persisted). | `$led mode param 5 speed 7` |
| **`mode color`** | Set the current mode's colour `rrggbb`; with no argument print it (`Led: color 00ff00`, the status `Color:` value). | `$led mode color ff8000` |
| **`mode reset_params`** | Reset a mode's parameters to their table defaults (2.3.x `reset_current_mode`); no argument: the current mode. One NVS write per parameter, one cross-fade. | `$led mode reset_params 5` |
| **`mode speed`** | Set the current mode's speed. | `$led mode speed 10` |
| 2.3.x names | `set_brightness N`, `set_state 0\|1`, `toggle_state`, `turn_on`, `turn_off`, `set_length N`, `set_color_order XYZ`, `set_mode <m>`, `set_mode_param <m> <key> <value>`: same handlers as above (LM2, LM15). Not ported: `adj_brightness`, `adj_mode`, the `set_/adj_` colour commands, `get_mode_params`. | `$led set_mode 5` |

`mode` is registered once per argument count (1, 2 and 4) and dispatches on its first argument; any
other combination prints `Led: usage: $led mode list | set <m> | ...`. Replies start with `Led:`.

### NVS keys (namespace `led`)

| Key | Type | Meaning |
|---|---|---|
| `chip` | u8 | chip id (applies after restart) |
| `num_led` | u16 | length |
| `colorder` | u8 | colour order index (RGB=0 … BGR=5) |
| `voltage` | u8 | supply voltage |
| `brightness` | u8 | last non-zero brightness |
| `state` | bool | on/off |
| `mode_id` | u8 | current mode id |
| `m:<id>:<key>` | u16 | one per mode parameter (27 today; longest `m:2:min_bright`, 14 chars) |

Dev builds of v2 that had the separate `led_modes` namespace keep those entries unused (LM16).

### C++ API

```cpp
led.set_brightness(200, origin);  led.get_brightness();   led.set_state(true, origin);  led.get_state();
led.set_mode(5, origin);          led.get_mode();         led.set_param(5, "speed", 7, origin);
led.get_param(5, "speed");        led.set_color({255, 128, 0}, origin);   led.get_color();   // rrggbb
led.set_speed(7, origin);         led.reset_params(5, origin);
led.get_length();  led.get_max_length();  led.get_fps();  led.fill({255, 0, 0});  led.clear_fill();
led.get_frame_checksum();  led.set_setting("num_led", "30");
// the registry, e.g. for a JSON list of modes (modes/Registry.h, namespace led_fx):
for (const led_fx::ModeDef& m : led_fx::MODES) { m.id; m.name; m.params[i].key; m.param_count; }
const led_fx::ModeDef* m = led_fx::find_mode(id);   // nullptr when not compiled in
```

`origin` defaults to `nullptr` everywhere. The registry namespace is `led_fx`, not `led`: the sketch
declares the object `led`, and a namespace of the same name would clash with it.

### Listeners

Other modules learn about changes through `LedListener` (`src/Led/LedListener.h`, standard library
only), the hook from MIGRATION-SURVEY 2.4:

```cpp
struct MyListener : LedListener {
    void on_brightness(uint8_t value, const void* origin) override { if (origin != this) dirty = true; }
    void on_state(bool on, const void* origin) override             { if (origin != this) dirty = true; }
    void on_mode(uint8_t mode_id, const void* origin) override      { if (origin != this) dirty = true; }
    // also on_color(uint32_t rrggbb, origin), on_param(mode_id, key, value, origin); default bodies are no-ops
};
led.add_listener(&my_listener);              // false when all LED_LISTENERS_MAX (4) slots are taken
led.set_brightness(200, &my_listener);       // origin: my_listener skips this echo
```

- Fixed array of `LED_LISTENERS_MAX` (4) pointers, no heap; `remove_listener()` frees a slot.
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
| Libraries | XeWeCore >=2.0.0,<3.0.0; FastLED 3.10.3 (pinned in `libraries.toml`; a project lists it in its `xewe.toml` `[libraries]`: `FastLED = { repo = "https://github.com/FastLED/FastLED", ref = "3.10.3" }`) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

FastLED 3.10.3 compiles on all three chips with core 3.3.12; on the S3 its I2S parallel driver adds 6
`-Wdeprecated-declarations` warnings from FastLED's own sources. 3.10.4–3.10.6 add ~100–230 KB of
flash and other warnings, so 3.10.3 stays pinned.

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/board/test_led.py` (board tests) and `tests/unit/` (unit tests), run through an xewe-os
harness whose lock lists FastLED (see the repo [README](../../README.md)). No strip is needed:
settings, modes, parameters, fps and the frame checksum are read back over serial (`$led checksum` of
Solid red is compared with the expected CRC); the visual checks are skipped (`requires hardware`).
Unit tests (`tests/unit/test_led.py`, `--unit-only`) build `test_effects.cpp` (registry lookups, a
pinned CRC per mode, maths, cross-fade, seeds) and `test_listeners.cpp` (capacity 4, no duplicates,
order, echo suppression, default no-ops) with g++, check the registry against the mode files, the
command table, that the mode headers stay pure, and that setters notify outside the lock and never
from the render task.

```sh
build/tools/.venv/bin/python -m xewe test --module led              # lock chip
build/tools/.venv/bin/python -m xewe test --module led --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module led --unit-only  # unit tests only, no build
```

## Known gaps

- The merged board suite (`$led mode ...`, merged status) is unverified on a board (written 2026-10-09 with the board offline).
- 2.3.x "parallel lines" (`lines`, `l_<i>_cnt`) were only used for the power report and are not ported; the stored keys are ignored. 2.3.x stored mode params under namespace `mc`, which v2 does not read.
- `$led set num_led` and `fill` do not notify listeners (no 2.3.x integration reported them).
- The cross-fade restarts from the newer mode when a change arrives mid-fade (2.3.x froze the half-blended frame).
- Strapping pin 8 as the default data pin on C3/C6 (night-run follow-up #3) is unchanged.
- Chip timing, colour order on real strips, flicker with WiFi active, the power estimate and visual equivalence of the modes with 2.3.x are unverified (no strip attached during the port).
