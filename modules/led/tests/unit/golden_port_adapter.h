// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/golden_port_adapter.h
//
// Host adapter that gives the 2.3.0 golden oracle driver (golden_driver.cpp, a copy of the
// oracle's harness/oracle.cpp) the names it expects, implemented on the led module's engine:
//   ModuleController  -> holds the host Nvs, the mode parameter store and one LedStrip
//   LedStrip          -> LedEngine plus what Led does around it on a device: the settings table
//                        load (num_led, colorder), the regular boot and a render clock
//   Nvs               -> typed values and blobs over the oracle's in-memory ESP-IDF NVS shim
//   ModeController    -> the module's own class, with a store that keeps the 2.3.0 key layout
//                        (namespace "mc", "m:<id>:<key>", u16) so the NVS dumps are comparable
// Compiled only by tests/unit/test_led.py (golden test), never into firmware.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <Arduino.h>
#include <FastLED.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "../../src/Led/LedEngine.h"
#include "../../src/Led/ModeController.h"

#define LED_STRIP_NUM_LEDS_MAX XEWE_LED_NUM_LEDS_MAX
#define LED_PIN_DATA XEWE_LED_PIN_DATA
#define LED_PIN_CLOCK XEWE_LED_PIN_CLOCK

template <typename T> using AsyncTimer = xewe::AsyncTimer<T>;

class Nvs {
public:
    Nvs() {
        nvs_flash_init();
    }

    template <typename T> bool write(std::string_view ns, std::string_view key, const T& value) {
        nvs_handle_t h;
        if (nvs_open(std::string(ns).c_str(), NVS_READWRITE, &h) != ESP_OK)
            return false;
        const std::string k(key);
        esp_err_t e;
        if constexpr (sizeof(T) == 1)
            e = nvs_set_u8(h, k.c_str(), static_cast<uint8_t>(value));
        else
            e = nvs_set_u16(h, k.c_str(), static_cast<uint16_t>(value));
        nvs_commit(h);
        nvs_close(h);
        return e == ESP_OK;
    }

    template <typename T> T read(std::string_view ns, std::string_view key, T default_value = T()) {
        nvs_handle_t h;
        if (nvs_open(std::string(ns).c_str(), NVS_READONLY, &h) != ESP_OK)
            return default_value;
        const std::string k(key);
        T out = default_value;
        if constexpr (sizeof(T) == 1) {
            uint8_t v;
            if (nvs_get_u8(h, k.c_str(), &v) == ESP_OK)
                out = static_cast<T>(v);
        } else {
            uint16_t v;
            if (nvs_get_u16(h, k.c_str(), &v) == ESP_OK)
                out = static_cast<T>(v);
        }
        nvs_close(h);
        return out;
    }

    bool write_blob(std::string_view ns, std::string_view key, const std::vector<uint8_t>& data) {
        nvs_handle_t h;
        if (nvs_open(std::string(ns).c_str(), NVS_READWRITE, &h) != ESP_OK)
            return false;
        const esp_err_t e = nvs_set_blob(h, std::string(key).c_str(), data.data(), data.size());
        nvs_commit(h);
        nvs_close(h);
        return e == ESP_OK;
    }

    std::vector<uint8_t> read_blob(std::string_view ns, std::string_view key) {
        std::vector<uint8_t> out;
        nvs_handle_t h;
        if (nvs_open(std::string(ns).c_str(), NVS_READONLY, &h) != ESP_OK)
            return out;
        const std::string k(key);
        size_t n = 0;
        if (nvs_get_blob(h, k.c_str(), nullptr, &n) == ESP_OK && n > 0) {
            out.resize(n);
            if (nvs_get_blob(h, k.c_str(), out.data(), &n) != ESP_OK)
                out.clear();
        }
        nvs_close(h);
        return out;
    }
};

class HostModeStore : public ModeParamStore {
public:
    explicit HostModeStore(Nvs& nvs) : nvs(nvs) {}
    uint16_t read(uint8_t mode_id, std::string_view key, uint16_t default_value) override {
        return nvs.read<uint16_t>("mc", name(mode_id, key), default_value);
    }
    void write(uint8_t mode_id, std::string_view key, uint16_t value) override {
        nvs.write<uint16_t>("mc", name(mode_id, key), value);
    }

private:
    static std::string name(uint8_t mode_id, std::string_view key) {
        return "m:" + std::to_string(mode_id) + ":" + std::string(key);
    }
    Nvs& nvs;
};

struct LedStripConfig {
    uint16_t mode_transition_delay = XEWE_LED_MODE_FADE_MS;
    uint16_t brightness_transition_delay = XEWE_LED_BRIGHTNESS_FADE_MS;
    uint8_t frame_delay = XEWE_LED_FRAME_MS;
    uint8_t fps_calc_window_s = XEWE_LED_FPS_WINDOW_S;
};

class ModuleController;

// Led's boot and render task around a LedEngine, without the console and FreeRTOS.
class LedStrip : private LedEngineIo {
public:
    LedStrip(Nvs& nvs, ModeParamStore& store) : nvs(nvs), store(store), engine(*this) {}

    // Module::begin step 0 loads the settings table; Led::begin_routines_required builds the engine
    void begin_routines_required(const LedStripConfig& cfg) {
        engine.begin(nvs.read<uint16_t>("led", "num_led", XEWE_LED_NUM_LEDS_MAX), store,
            cfg.brightness_transition_delay, cfg.mode_transition_delay);
    }
    // Led::begin_routines_regular (the strip driver registration is the oracle's counting one);
    // the render task starts here, its first frame comes one period later
    void begin_routines_regular(const LedStripConfig& cfg) {
        engine.boot(nvs.read<uint8_t>("led", "colorder", 0), nvs.read_blob("led", "rgb"),
            nvs.read<uint8_t>("led", "brightness"), nvs.read<uint8_t>("led", "mode_id"),
            nvs.read<uint8_t>("led", "state") != 0);
        frame_ms = cfg.frame_delay;
        last_frame_ms = millis();
    }
    // one pass of Led::render_task when its frame is due (one frame per frame_ms of fake time):
    // the black frame of a length change (filled here, as the task does), then the mode frame
    void loop() {
        if (static_cast<uint32_t>(millis() - last_frame_ms) < frame_ms)
            return;
        last_frame_ms += frame_ms;
        if (engine.take_blank_frame())
            FastLED.show();
        engine.render_frame();
        FastLED.show();
    }

    // the 2.3.0 LedStrip methods the driver calls
    void set_brightness(uint8_t b) {
        engine.set_brightness(b);
    }
    void adj_brightness(int d) {
        engine.adj_brightness(d);
    }
    void turn_on() {
        engine.turn_on();
    }
    void turn_off() {
        engine.turn_off();
    }
    void toggle_state() {
        engine.toggle_state();
    }
    void set_mode(uint8_t m) {
        engine.set_mode(m);
    }
    void set_rgb(uint8_t r, uint8_t g, uint8_t b) {
        engine.set_rgb(r, g, b);
    }
    void set_hsv(std::array<uint8_t, 3> hsv) {
        engine.set_hsv(hsv);
    }
    void set_h(uint8_t h) {
        engine.set_h(h);
    }
    void adj_h(int d) {
        engine.adj_h(d);
    }
    void adj_s(int d) {
        engine.adj_s(d);
    }
    void set_mode_param(std::string_view k, uint16_t v) {
        engine.set_mode_param(k, v);
    }
    void adj_mode_param(std::string_view k, long d) {
        engine.adj_mode_param(k, static_cast<int32_t>(d));
    }
    void reset_current_mode() {
        engine.reset_current_mode();
    }
    void set_length(uint16_t n) {
        engine.set_length(n);
    }
    void set_color_order(std::string_view o) {
        engine.set_color_order(o);
    }

    uint8_t get_current_mode_id() const {
        return engine.get_current_mode_id();
    }
    std::string_view get_current_mode_name() const {
        return engine.get_current_mode_name();
    }
    std::array<uint8_t, 3> get_rgb() const {
        return engine.get_rgb();
    }
    uint8_t get_brightness() const {
        return engine.get_brightness();
    }
    bool get_state() const {
        return engine.get_state();
    }
    uint16_t get_length() const {
        return engine.get_length();
    }

    const CRGB* port_leds() {
        return engine.pixels();
    }
    const ModeController& port_mode_controller() const {
        return engine.get_mode_controller();
    }
    LedEngine& port_engine() {
        return engine;
    }

private:
    void save_u8(const char* key, uint8_t value) override {
        nvs.write<uint8_t>("led", key, value);
    }
    void save_u16(const char* key, uint16_t value) override {
        nvs.write<uint16_t>("led", key, value);
    }
    void save_bool(const char* key, bool value) override {
        nvs.write<bool>("led", key, value);
    }
    void save_rgb(const std::array<uint8_t, 3>& rgb) override {
        nvs.write_blob("led", "rgb", std::vector<uint8_t>(rgb.begin(), rgb.end()));
    }
    void print(std::string_view) override {}

    Nvs& nvs;
    ModeParamStore& store;
    LedEngine engine;
    uint32_t frame_ms = XEWE_LED_FRAME_MS;
    uint32_t last_frame_ms = 0;
};

class ModuleController {
public:
    Nvs nvs;
    HostModeStore mc_store{nvs};
    LedStrip led_strip{nvs, mc_store};
};
