// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedEngine.h
//
// The strip logic of the led module without the console, NVS or FreeRTOS: colour, brightness,
// state, mode and length setters, the dimmed and colour-ordered output buffer, the status text and
// the change events. Led (the xewe::Module) owns one, persists through LedEngineIo and renders it
// from its task; host tests drive it directly.
//
// Lock discipline (LedEngineIo::lock/unlock is Led's render mutex, recursive per task):
// - The pixel buffer leds[] is written only by the render task. Its two writers, render_frame()
//   and take_blank_frame(), are called by that task (or by a host test standing in for it) with the
//   lock held, and FastLED.show() then reads the buffer outside the lock. A length change only asks
//   for the black frame; the task fills it.
// - Every public setter, sync_* call and whole `$led` command (led_run_command) is one atomic
//   section: the lock is taken on entry and released when the outermost section ends. Sections
//   nest, so a compound operation never has a frame in the middle: set_mode's two mode builds,
//   the double apply of adj_*, set_mode_param with its re-applied colour, and the five steps of
//   sync_all all finish before the next frame is drawn.
// - Nothing slow runs under the lock. NVS writes (LedEngineIo::save_*, ModeParamStore::commit),
//   console lines and listener callbacks are queued inside a section and run in their original
//   order right after the outermost section releases the lock. A callback may therefore call back
//   into the engine; that starts a new section.
// - Getters read without the lock. The state they read is changed only inside sections, by the
//   task that runs the commands (the main loop); read it from that task or from a callback.
#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <XeWeCore.h>

#include "Brightness.h"
#include "Chipsets.h"
#include "Config.h"
#include "LedListener.h"
#include "ModeController.h"

class LedEngineIo {
public:
    virtual ~LedEngineIo() = default;
    // the module's NVS namespace; the types are part of the stored layout
    virtual void save_u8(const char* key, uint8_t value) = 0;
    virtual void save_u16(const char* key, uint16_t value) = 0;
    virtual void save_bool(const char* key, bool value) = 0;
    virtual void save_rgb(const std::array<uint8_t, 3>& rgb) = 0; // `rgb`, 3-byte blob
    // one console line (the messages of set_length and set_color_order)
    virtual void print(std::string_view text) = 0;
    // `$led set_color_order` without a name: the interactive calibration (console prompts while
    // the render task shows the test colours); called outside any section
    virtual void calibrate_color_order() {}
    // the render lock; must be recursive within one task (sections nest)
    virtual void lock() {}
    virtual void unlock() {}
};

// what `$led status` shows that the engine does not hold itself
struct LedStatusInput {
    bool use_clk = false;
    uint8_t chip = 0;
    uint8_t voltage = 5;
    uint16_t lines = 0;
    std::vector<uint16_t> line_lengths; // l_<i>_cnt, one per line
    uint16_t fps = 0;
    int pin_data = XEWE_MODULE_LED_PIN_DATA;
    int pin_clock = XEWE_MODULE_LED_PIN_CLOCK;
};

class LedEngine {
public:
    explicit LedEngine(LedEngineIo& io);

    // builds Brightness(off, 0) and the ModeController (which starts mode 0, fading in from black)
    void begin(uint16_t num_led, ModeParamStore& store,
        uint16_t brightness_transition_ms = XEWE_MODULE_LED_BRIGHTNESS_FADE_MS,
        uint16_t mode_transition_ms = XEWE_MODULE_LED_MODE_FADE_MS);
    bool begun() const;
    // regular boot: the stored values, applied in the order colour order, rgb, brightness, mode,
    // state; each setter writes its key back
    void boot(uint8_t colorder, const std::vector<uint8_t>& rgb_blob, uint8_t brightness,
        uint8_t mode_id, bool state);

    // origin of the events the strip raises itself (Led passes itself)
    void set_self_origin(const void* origin);
    const void* self_origin() const;

    // ---- apply locally, then notify the listeners
    void sync_color(const std::array<uint8_t, 3> color, const void* origin);
    void sync_brightness(const uint8_t brightness, const void* origin);
    void sync_state(const bool state, const void* origin);
    void sync_mode(const uint8_t mode, const void* origin);
    void sync_length(const uint16_t length, const void* origin);
    void sync_all(const std::array<uint8_t, 3> color, const uint8_t brightness, const bool state,
        const uint8_t mode, const uint16_t length, const void* origin);

    // ---- colour
    void set_rgb(const std::array<uint8_t, 3> new_rgb);
    void set_rgb(const uint8_t r, const uint8_t g, const uint8_t b);
    void set_r(const uint8_t r);
    void set_g(const uint8_t g);
    void set_b(const uint8_t b);

    void set_hsv(const std::array<uint8_t, 3> new_hsv);
    void set_h(const uint8_t h);
    void set_s(const uint8_t s);
    void set_v(const uint8_t v);

    void adj_rgb(const std::array<int, 3> rgb_delta);
    void adj_r(const int r_delta);
    void adj_g(const int g_delta);
    void adj_b(const int b_delta);

    void adj_hsv(const std::array<int, 3> hsv_delta);
    void adj_h(const int h_delta);
    void adj_s(const int s_delta);
    void adj_v(const int v_delta);

    std::array<uint8_t, 3> get_rgb() const;
    uint8_t get_r() const;
    uint8_t get_g() const;
    uint8_t get_b() const;
    std::array<uint8_t, 3> get_hsv() const;
    uint8_t get_h() const;
    uint8_t get_s() const;
    uint8_t get_v() const;

    // ---- brightness
    void set_brightness(const uint8_t new_brightness);
    void adj_brightness(const int brightness_delta);
    uint8_t get_brightness() const; // the last non-zero brightness

    // ---- state
    void set_state(const bool state);
    void toggle_state();
    void turn_on();
    void turn_off();
    bool get_state() const;

    // ---- mode
    void set_mode(const uint8_t new_mode);
    void adj_mode(const int mode_delta);

    void set_mode_param(std::string_view key, const uint16_t value);
    void adj_mode_param(std::string_view key, const int32_t value_delta);

    uint8_t get_current_mode_id() const;
    std::string_view get_current_mode_name() const;
    uint16_t get_current_mode_param(std::string_view key) const;
    std::vector<ModeParam> get_current_mode_params() const;
    void reset_current_mode();
    std::string get_all_modes_json() const;
    std::vector<std::pair<uint8_t, std::string>> get_all_modes() const;
    uint16_t get_mode_transition_delay() const;
    // `$led get_mode_params` text
    std::string mode_params_text() const;
    // one console line through LedEngineIo::print
    void print_text(std::string_view text);

    // ---- length
    void set_length(const uint16_t length);
    uint16_t get_length() const;

    // ---- colour order
    // a name (any case); false and a message when it is not one of the 6. An empty name starts
    // the interactive calibration (LedEngineIo::calibrate_color_order) and returns true
    bool set_color_order(std::string_view order);
    // an index 0..5 (the `colorder` setting); out of range is ignored
    void set_color_order_index(uint8_t index);
    uint8_t get_color_order_index() const;

    // ---- render task only, with the lock held
    // one frame: the mode writes the buffer, then it is dimmed and colour-ordered in place
    void render_frame();
    // after a length change: blacks the buffer over the longest length used since the last call
    // and returns true once; the task shows that black frame before its next mode frame
    bool take_blank_frame();
    CRGB* pixels();
    const ModeController& get_mode_controller() const;

    // ---- status (takes the lock to read the output buffer for the power estimate)
    std::string status_text(const LedStatusInput& in) const;

    // One atomic section (see the lock discipline above). Sections nest; the outermost one runs
    // the queued NVS writes, console lines and listener callbacks after it releases the lock.
    class Section {
    public:
        explicit Section(LedEngine& engine) : engine(engine) {
            engine.enter_section();
        }
        ~Section() {
            engine.leave_section();
        }
        Section(const Section&) = delete;
        Section& operator=(const Section&) = delete;

    private:
        LedEngine& engine;
    };

    xewe::ListenerSet<LedListener, XEWE_MODULE_LED_LISTENERS> listeners;

private:
    // forwards to the real store; commit() (the flash write) is queued like the other effects
    class QueuedStore : public ModeParamStore {
    public:
        explicit QueuedStore(LedEngine& engine) : engine(engine) {}
        uint16_t read(uint8_t mode_id, std::string_view key, uint16_t default_value) override;
        void write(uint8_t mode_id, std::string_view key, uint16_t value) override;
        void commit() override;

        ModeParamStore* inner = nullptr;

    private:
        LedEngine& engine;
    };

    // something slow that runs after the outermost section (or at once outside any section)
    struct Effect {
        enum Kind : uint8_t {
            SAVE_U8,
            SAVE_U16,
            SAVE_BOOL,
            SAVE_RGB,
            PRINT,
            COMMIT,
            ON_COLOR,
            ON_BRIGHTNESS,
            ON_STATE,
            ON_MODE,
            ON_LENGTH,
            ON_ALL
        };
        explicit Effect(Kind k) : kind(k) {}
        Kind kind;
        const char* name = nullptr; // NVS key
        uint16_t value = 0; // stored value, brightness or length
        uint16_t length = 0;
        std::array<uint8_t, 3> rgb{};
        bool state = false;
        uint8_t mode = 0;
        const void* origin = nullptr;
        std::string text;
    };

    void enter_section();
    void leave_section();
    void defer(Effect effect);
    void run_effect(const Effect& effect);
    void save_u8(const char* key, uint8_t value);
    void save_u16(const char* key, uint16_t value);
    void save_bool(const char* key, bool value);
    void save_rgb(const std::array<uint8_t, 3>& rgb);
    void print(std::string text);

    void set_pixel(uint16_t i, std::array<uint8_t, 3> color_rgb);
    void set_all(CRGB* new_leds);
    void set_black();

    LedEngineIo& io;
    const void* self = nullptr;
    QueuedStore queued_store{*this};
    int section_depth = 0;
    std::vector<Effect> pending_effects;

    CRGB leds[XEWE_MODULE_LED_NUM_LEDS_MAX];

    uint16_t num_led{XEWE_MODULE_LED_NUM_LEDS_MAX};
    uint8_t color_order_index = 0;
    bool blank_requested = false;
    uint16_t blank_length = 0;
    std::unique_ptr<ModeController> mode_controller;
    std::unique_ptr<Brightness> brightness;
};
