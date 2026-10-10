// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Led.h
//
// The led module: an addressable LED strip (chip, pins, length, colour order, brightness with fades,
// 50 fps render task) and its modes (modes/Registry.h: one file per mode, stable ids, clamped and
// persisted parameters, 900 ms cross-fade on every change). One NVS namespace `led`: the strip keys
// (the core 2.1 settings table, Led::settings()), `mode_id` and `m:<mode id>:<key>`. Commands under
// `$led`, mode commands under `$led mode ...`; `$led get|schema` come from the core.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <XeWeCore.h>
// Generated per build by `xewe build` (LED_* values passed with --define). Unconditional on purpose:
// a __has_include() guard makes arduino-cli drop the generated library and the defaults below win.
#include <XeWeBuildInfo.h>

#include "Pixel.h"
#include "Chipsets.h"
#include "Brightness.h"
#include "LedListener.h"
#include "modes/Registry.h"

// ---- Build-time defaults (override per build: xewe build --define LED_PIN_DATA=13 ...) -------------
// The pins are the defaults of the `pin_data`/`pin_clock` settings (`$led set pin_data 5`, after a
// restart). Clockless chips start on any usable GPIO; APA102 always uses these build pins (Led.cpp).
#ifndef LED_PIN_DATA
#  if defined(CONFIG_IDF_TARGET_ESP32S3)
#    define LED_PIN_DATA 48          // S3 SuperMini / DevKitC-1 v1.0 on-board WS2812B
#  else
#    define LED_PIN_DATA 8           // C6 SuperMini on-board WS2812B; C3: any free GPIO
#  endif
#endif
#ifndef LED_PIN_CLOCK
#  if defined(CONFIG_IDF_TARGET_ESP32S3)
#    define LED_PIN_CLOCK 12
#  elif defined(CONFIG_IDF_TARGET_ESP32C6)
#    define LED_PIN_CLOCK 21
#  else
#    define LED_PIN_CLOCK 4
#  endif
#endif
#ifndef LED_STRIP_NUM_LEDS_MAX
#define LED_STRIP_NUM_LEDS_MAX 2000      // buffer size; `$led set num_led` accepts 1..this
#endif
// Defaults of the settings table, used until `$led set <key> <value>` stores a value in NVS (namespace `led`).
#ifndef LED_COUNT
#define LED_COUNT 60
#endif
#ifndef LED_CHIPSET
#define LED_CHIPSET "WS2812B"            // a name from Chipsets.h, with quotes: --define 'LED_CHIPSET="SK6812"'
#endif
#ifndef LED_COLOR_ORDER
#define LED_COLOR_ORDER "GRB"            // RGB RBG GRB GBR BRG BGR, with quotes
#endif
#ifndef LED_VOLTAGE
#define LED_VOLTAGE 5                    // only used for the power estimate in status
#endif

struct LedConfig {
    uint16_t brightness_fade_ms     = 500;
    uint8_t  frame_delay_ms         = 20;    // 50 fps
    uint8_t  fps_window_s           = 1;
    uint16_t render_task_stack_size = 4096;
    uint8_t  render_task_priority   = 3;     // above loopTask (1), below WiFi
    uint8_t  render_task_core       = 0;
};

class Led : public xewe::Module {
public:
    explicit           Led                     (xewe::Os& host,
                                                LedConfig config = {});

    void               begin_routines_regular  ()                                  override;
    void               reset                   (const bool verbose      = false,
                                                const bool do_restart   = true,
                                                const bool keep_enabled = true)    override;
    std::string        status                  (const bool verbose = false) const  override;
    xewe::Settings     settings                () const                            override;
    void               schema_extra            (xewe::SchemaOut& out) const        override;

    // ---- strip --------------------------------------------------------------------------------------
    // brightness and state; `origin` is passed to the listeners (echo suppression). persist = false
    // changes the strip only (no NVS write): e.g. a transient off that a restart does not keep
    void               set_brightness          (uint8_t value, const void* origin = nullptr, bool persist = true);
    uint8_t            get_brightness          () const;
    void               set_state               (bool on, const void* origin = nullptr, bool persist = true);
    bool               get_state               () const;

    // `$led set`: the table path (apply_setting) after translating names (chip WS2812B, colorder GRB)
    // and the 2.3.x keys length/color_order; prints the result; false + message on a bad value
    bool               set_setting             (const std::string& key,
                                                const std::string& value);
    uint16_t           get_length              () const;
    uint16_t           get_max_length          () const;
    uint16_t           get_fps                 () const;

    // static colour over the mode (not persisted), cross-faded from the current frame over fade_ms
    // (0: at once); clear_fill() or any mode change resumes the mode
    void               fill                    (LedRgb color, uint16_t fade_ms = 0);
    void               clear_fill              ();
    uint32_t           get_frame_checksum      () const;                          // CRC-32 of the last frame (pre-brightness)

    // ---- modes (modes/Registry.h; ids are the stable ids of the mode files) -------------------------
    // Setters notify the listeners (on_mode/on_color/on_param) with `origin`, from the calling task
    // (main loop), after the change is applied. false: unknown mode id or parameter.
    bool               set_mode                (int mode_id, const void* origin = nullptr);
    bool               set_param               (int mode_id, const std::string& param, int32_t value,
                                                const void* origin = nullptr);
    bool               set_color               (LedRgb color, const void* origin = nullptr);    // hue/sat of the current mode
    bool               set_speed               (int32_t value, const void* origin = nullptr);   // `speed` of the current mode
    bool               reset_params            (int mode_id, const void* origin = nullptr);     // table defaults, one cross-fade
    uint8_t            get_mode                () const;
    uint16_t           get_param               (int mode_id, const std::string& param) const;
    uint32_t           get_color               () const;                          // rrggbb of the current mode (status `Color:`)

    // ---- change listeners (LedListener.h, xewe::ListenerSet): up to LED_LISTENERS_MAX (6), no heap ----
    bool               add_listener            (LedListener* listener);           // false when full
    bool               remove_listener         (LedListener* listener);
    template <typename F>
    void               notify_listeners        (F&& call) const { listeners.notify(call); }

protected:
    void               on_setting_changed      (const xewe::SettingDef& def)       override;

private:
    struct Slot {
        const led_fx::ModeDef* def                          = &led_fx::default_mode();
        uint16_t               params[led_fx::MAX_PARAMS]   = {};
        led_fx::ModeState      state;
    };

    LedConfig          config;
    LedBrightness      brightness;

    // Buffers are allocated in Led.cpp, so the class layout does not depend on LED_* macros
    // (a sketch TU that saw a different LED_STRIP_NUM_LEDS_MAX would otherwise break the ODR).
    LedRgb*            frame                   = nullptr;   // LED_STRIP_NUM_LEDS_MAX pixels, logical RGB
    uint8_t*           out                     = nullptr;   // LED_STRIP_NUM_LEDS_MAX * 3, CRGB-compatible output

    const LedChipset*  chipset                 = nullptr;   // active (registered with FastLED)
    int8_t             active_data_pin         = -1;        // claimed GPIOs (xewe::pins), -1 none
    int8_t             active_clock_pin        = -1;
    uint16_t           shown_led               = 0;         // length FastLED currently sends (render task)
    uint16_t           frame_len               = 0;         // length of the last rendered frame
    uint8_t            brightness_setting      = 0;         // current target, 0 after `brightness 0`

    // settings table rows (Led::settings(), loaded by the core before begin; NVS keys = row keys)
    uint8_t            stored_chip_id          = 0;         // chip, applies after restart
    uint16_t           num_led                 = 1;
    uint8_t            color_order             = 0;
    uint8_t            voltage                 = 5;
    uint8_t            brightness_saved        = 128;       // last non-zero brightness
    bool               state_saved             = true;
    uint8_t            pin_data                = 0;         // apply after restart
    uint8_t            pin_clock               = 0;

    LedListeners       listeners;

    bool               fill_active             = false;
    LedRgb             fill_color              = {0, 0, 0};
    uint16_t           fill_fade_ms            = 0;         // > 0 while a fill cross-fade runs (from buffer_old)
    uint32_t           fill_start_ms           = 0;

    // mode state machine, guarded by render_mutex
    Slot               current;
    Slot               previous;
    bool               transitioning           = false;
    uint32_t           transition_start_ms     = 0;
    std::vector<led_fx::Rgb> buffer_old;
    std::vector<led_fx::Rgb> buffer_new;

    SemaphoreHandle_t  render_mutex            = nullptr;
    TaskHandle_t       render_task_handle      = nullptr;
    void*              led_driver              = nullptr;   // CLEDController*, kept opaque in the header

    uint16_t           fps_counter             = 0;
    uint32_t           fps_window_start_ms     = 0;
    std::atomic<uint16_t> fps                  {0};
    std::atomic<uint32_t> frame_crc            {0};

    // strip
    bool               add_leds                (uint8_t chip_id);
    void               claim_pins              (const LedChipset& chip);           // sets active_*_pin
    void               release_pins            ();
    static void        render_task_entry       (void* self);
    void               render_task             ();
    void               render_frame            ();                                // caller holds render_mutex
    void               render_modes            (LedRgb* dest, uint16_t count, uint32_t now_ms);   // ditto
    void               count_frame             ();
    void               start_render_task       ();

    // modes
    void               load_params             (const led_fx::ModeDef& mode, uint16_t* values) const;
    void               persist_params          (const led_fx::ModeDef& mode, const uint16_t* values);
    void               activate                (const led_fx::ModeDef& mode, const uint16_t* values);   // with cross-fade
    void               notify_params           (const led_fx::ModeDef& mode, const uint16_t* old_values,
                                                const uint16_t* new_values, const void* origin);
    const led_fx::ModeDef* parse_mode          (const std::string& text) const;
    std::string        nvs_param_name          (uint8_t mode_id, const char* param) const;

    // CLI
    void               cli_brightness          (xewe::span<const std::string> args);
    void               cli_set                 (xewe::span<const std::string> args);
    void               cli_fill                (xewe::span<const std::string> args);
    void               cli_mode                (xewe::span<const std::string> args);   // `$led mode ...`, every arg count
    void               mode_list               ();
    void               mode_set                (const std::string& mode);
    void               mode_param              (const std::string& mode, const std::string& key,
                                                const std::string& value);
    void               mode_color              (const std::string& rrggbb);
    void               mode_reset_params       (const std::string* mode);              // nullptr: current mode
    void               mode_speed              (const std::string& value);
};
