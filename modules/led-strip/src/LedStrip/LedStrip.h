// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-strip/src/LedStrip/LedStrip.h
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

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

// ---- Build-time defaults (override per build: xewe build --define LED_PIN_DATA=13 ...) -------------
// Pins are FastLED template arguments, so they are fixed per firmware image (release matrix columns).
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
// Defaults below are used until `$led set <key> <value>` stores a value in NVS (namespace `led`).
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

struct LedStripConfig {
    uint16_t brightness_fade_ms     = 500;
    uint8_t  frame_delay_ms         = 20;    // 50 fps
    uint8_t  fps_window_s           = 1;
    uint16_t render_task_stack_size = 4096;
    uint8_t  render_task_priority   = 3;     // above loopTask (1), below WiFi
    uint8_t  render_task_core       = 0;
};

class LedStrip : public xewe::Module {
public:
    explicit           LedStrip                (xewe::Os&      host,
                                                LedStripConfig config = {});

    void               begin_routines_regular  ()                                  override;
    void               reset                   (const bool verbose      = false,
                                                const bool do_restart   = true,
                                                const bool keep_enabled = true)    override;
    std::string        status                  (const bool verbose = false) const  override;

    // brightness and state (persisted); `origin` is passed to the listeners (echo suppression)
    void               set_brightness          (uint8_t value, const void* origin = nullptr);
    uint8_t            get_brightness          () const;
    void               set_state               (bool on, const void* origin = nullptr);
    bool               get_state               () const;

    // change listeners (LedListener.h): up to LED_LISTENERS_MAX, main loop only, no heap
    bool               add_listener            (LedListener* listener);           // false when full
    bool               remove_listener         (LedListener* listener);
    template <typename F>
    void               notify_listeners        (F&& call) const { listeners.notify(call); }   // led-modes forwards through this

    // configuration (persisted); false + message on a bad value
    bool               set_setting             (const std::string& key,
                                                const std::string& value);
    uint16_t           get_length              () const;
    uint16_t           get_max_length          () const;
    uint16_t           get_fps                 () const;

    // frames
    void               set_frame_source        (LedFrameSource* source);          // nullptr: black
    void               fill                    (LedRgb color);                    // static colour, overrides the source
    void               clear_fill              ();                                // back to the source
    uint32_t           get_frame_checksum      () const;                          // CRC-32 of the last frame (pre-brightness)

    // for frame sources that change their state from the main loop
    SemaphoreHandle_t  get_render_mutex        () const;

private:
    LedStripConfig     config;
    LedBrightness      brightness;

    // Buffers are allocated in LedStrip.cpp, so the class layout does not depend on LED_* macros
    // (a sketch TU that saw a different LED_STRIP_NUM_LEDS_MAX would otherwise break the ODR).
    LedRgb*            frame                   = nullptr;   // LED_STRIP_NUM_LEDS_MAX pixels, logical RGB
    uint8_t*           out                     = nullptr;   // LED_STRIP_NUM_LEDS_MAX * 3, CRGB-compatible output

    const LedChipset*  chipset                 = nullptr;   // active (registered with FastLED)
    uint8_t            stored_chip_id          = 0;         // NVS value, applies after restart
    uint16_t           num_led                 = 1;         // set from NVS / LED_COUNT at begin
    uint16_t           shown_led               = 0;         // length FastLED currently sends (render task)
    uint16_t           frame_len               = 0;         // length of the last rendered frame
    uint8_t            color_order             = 0;
    uint8_t            voltage                 = 5;
    uint8_t            brightness_setting      = 0;

    LedListeners       listeners;

    LedFrameSource*    source                  = nullptr;
    bool               fill_active             = false;
    LedRgb             fill_color              = {0, 0, 0};

    SemaphoreHandle_t  render_mutex            = nullptr;
    TaskHandle_t       render_task_handle      = nullptr;
    void*              led_driver              = nullptr;   // CLEDController*, kept opaque in the header

    uint16_t           fps_counter             = 0;
    uint32_t           fps_window_start_ms     = 0;
    std::atomic<uint16_t> fps                  {0};
    std::atomic<uint32_t> frame_crc            {0};

    bool               add_leds                (uint8_t chip_id);
    static void        render_task_entry       (void* self);
    void               render_task             ();
    void               render_frame            ();                                // caller holds render_mutex
    void               count_frame             ();
    void               start_render_task       ();

    void               cli_brightness          (xewe::span<const std::string> args);
    void               cli_set                 (xewe::span<const std::string> args);
    void               cli_fill                (xewe::span<const std::string> args);
};
