# led-strip — drive an addressable LED strip

XeWe OS module · created 2026-10-09 (ported from xewe-led-os 2.3.x `LedStrip` + `Brightness`) · Solo: Max Dokukin · Status: Draft (0.1.0)

## Overview

Owns the strip: chipset, pins, length, colour order, a pixel buffer, brightness with on/off fades and
a FreeRTOS render task that pushes frames through [FastLED](https://github.com/FastLED/FastLED) at
50 fps. It has no effects of its own; a frame source (the [led-modes](../led-modes) module, or your
own code) fills each frame. Without a source the strip shows black, or a static colour from
`$led fill`. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- No first-boot questions: defaults come from build defines, `$led set <key> <value>` changes them and stores them in NVS (namespace `led`, the 2.3.x key names `chip`, `num_led`, `colorder`, `voltage`, `brightness`, `state`), so a 2.3.x device keeps its strip settings
- Render task `led_render` (stack 4096, priority 3, core 0, 20 ms period) renders under a mutex and calls `FastLED.show()` outside it; brightness fades over 500 ms
- `$led checksum` prints a CRC-32 of the current frame (logical RGB, before brightness and colour order): frame generation is testable without LEDs
- Status reports chip, pins, length, colour order, brightness, state, measured fps and a power estimate

## How it works

```
render task (50 fps): lock → fill override | source->render(frame, n, millis()) | black → CRC-32
                      → brightness scale + colour order into the CRGB output buffer → unlock → FastLED.show()
$led set num_led 30   → clamp 1..LED_STRIP_NUM_LEDS_MAX → NVS → applied on the next frame (old tail sent black once)
```

- **`LedStrip` class** (`src/LedStrip/`) — a `xewe::Module` with id `led`; cannot be disabled and needs no init setup, so it begins without prompts.
- **API for other modules** — `set_frame_source(LedFrameSource*)`, `fill(LedRgb)`, `clear_fill()`, `set_brightness()`, `set_state()`, `set_setting(key, value)`, `get_length()`, `get_fps()`, `get_frame_checksum()`, `get_render_mutex()`. A frame source implements `render(LedRgb* frame, uint16_t count, uint32_t now_ms)`; it runs in the render task with the mutex held. Change source state from the main loop only while holding `get_render_mutex()`.
- **Pins are build-time.** FastLED takes them as template arguments, so `LED_PIN_DATA`/`LED_PIN_CLOCK` are fixed per firmware image (one image per pin pair, as the 2.3.x release matrix). `chip` changes are stored at once and applied after a restart; `num_led` and `colorder` apply live.

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

### Chipsets

Compiled in (2.3.x ids kept): APA102 (0, clocked), SK6812 (19), WS2811 (38), WS2812 (40), WS2812B (41).
2.3.x offered 46; each one instantiates a FastLED driver, so the rest are left out until a device
needs them: APA102HD, APA104, APA106, DOTSTAR, DOTSTARHD, GE8822, GS1903, GW6205, GW6205_400KHZ, HD107,
HD107HD, LPD1886, LPD1886_8BIT, LPD6803, LPD8806, NEOPIXEL, P9813, PL9823, SK6822, SK9822, SK9822HD,
SM16703, SM16716, SM16824E, TM1803, TM1804, TM1809, TM1812, TM1829, UCS1903, UCS1903B, UCS1904,
UCS1912, UCS2903, WS2801, WS2803, WS2811_400KHZ, WS2813, WS2815, WS2816, WS2852. Adding one is a row in
`Chipsets.h` and a `case` in `LedStrip::add_leds()`; the host test checks the two agree.

### Commands

**Prefix:** `$led`

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | Chip, pins, length, colour order, voltage, brightness, state, fps, power, source. | `$led status` |
| **`reset`** | Clear the `led` namespace (back to build defaults) and restart. | `$led reset` |
| **`on`** / **`off`** | Fade in / out (state persisted). | `$led on` |
| **`brightness`** | Set brightness 0–255 (persisted). `0` darkens the strip with State on and keeps the last non-zero value in NVS (2.3.x); status shows `0/255 (stored N)`, and a restart or off → on comes back at N. | `$led brightness 128` |
| **`set`** | Set `chip` (name or id), `num_led` (1–max), `colorder` (RGB…BGR) or `voltage`. | `$led set num_led 60` |
| **`fill`** | Show a static colour `rrggbb` over the frame source; `off` resumes the source. | `$led fill ff0000` |
| **`checksum`** | CRC-32 of the current frame (pre-brightness RGB, `length` pixels). | `$led checksum` |
| 2.3.x names | `set_brightness N`, `set_state 0\|1`, `toggle_state`, `turn_on`, `turn_off`, `set_length N`, `set_color_order XYZ`: same handlers as above (LM2). Not ported: `adj_brightness`, the `set_/adj_` colour commands (colour belongs to `$led_modes color`). | `$led turn_on` |

### Listeners

Other modules learn about changes through `LedListener` (`src/LedStrip/LedListener.h`, standard
library only), the hook from MIGRATION-SURVEY 2.4:

```cpp
struct MyListener : LedListener {
    void on_brightness(uint8_t value, const void* origin) override { if (origin != this) dirty = true; }
    void on_state(bool on, const void* origin) override             { if (origin != this) dirty = true; }
    // led-modes forwards these through the same listeners (default bodies are no-ops):
    // on_mode(uint8_t, origin), on_color(uint32_t rrggbb, origin), on_param(mode, key, value, origin)
};
led_strip.add_listener(&my_listener);              // false when all LED_LISTENERS_MAX (4) slots are taken
led_strip.set_brightness(200, &my_listener);       // origin: my_listener skips this echo
```

- Fixed array of `LED_LISTENERS_MAX` (4) pointers, no heap; `remove_listener()` frees a slot.
- `set_brightness(value, origin = nullptr)` and `set_state(on, origin = nullptr)` call the listeners
  only when the value changed (`on` from off also reports the restored brightness), after NVS and
  outside the render mutex, in the task that called the setter (the main loop: CLI, web handlers,
  buttons, scheduler). The render task never calls a listener.
- `notify_listeners(fn)` is the fan-out led-modes uses for its own callbacks.
- Keep callbacks short: set a flag and do the work in your `loop()` (led-web does).

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.0.0,<3.0.0; FastLED 3.10.3 (must be in the project's `xewe.lock` `[libraries]`: `FastLED = { repo = "https://github.com/FastLED/FastLED", ref = "3.10.3" }`) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.3.12) |

FastLED 3.10.3 compiles on all three chips with core 3.3.12; on the S3 its I2S parallel driver adds 6
`-Wdeprecated-declarations` warnings from FastLED's own sources. 3.10.4–3.10.6 add ~100–230 KB of
flash and other warnings, so 3.10.3 stays pinned.

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/test_led-strip.py`, run through an xewe-os harness whose lock lists FastLED (see the repo
[README](../../README.md)). No strip is needed: settings, fps and the frame checksum are read back over
serial; the visual check is skipped (`requires hardware`). Host tests (`--host-only`) build
`tests/host/test_listeners.cpp` with g++ (listener set: capacity 4, no duplicates, order, echo
suppression, default no-ops) and check that the setters notify outside the lock and never from the
render task.

```sh
build/tools/.venv/bin/python -m xewe test --module led-strip              # lock chip
build/tools/.venv/bin/python -m xewe test --module led-strip --all-chips  # c3, c6, s3
```

## Known gaps

- 2.3.x "parallel lines" (`lines`, `l_<i>_cnt`) were only used for the power report and are not ported; the stored keys are ignored.
- The 2.3.x sync fan-out to integrations is now the `LedListener` hook; led-web uses it. HomeKit, Alexa and Home Assistant modules are not ported yet.
- `$led set num_led` and `fill` do not notify listeners (no 2.3.x integration reported them).
- Chip timing, colour order on real strips, flicker with WiFi active and the power estimate are unverified (no strip attached during the port).
