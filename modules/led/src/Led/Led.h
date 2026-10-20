// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Led.h
//
// Addressable LED strip: 46 FastLED chipsets on compile-time pins, 7 effects with a 900 ms
// cross-fade, software brightness with a 500 ms fade, 6 software colour orders, a first-boot
// wizard, 30 `$led` commands and change events for other modules. Frames are rendered by a
// FreeRTOS task every XEWE_LED_FRAME_MS; the main loop only changes state.
#pragma once

#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <XeWeCore.h>

#include "Chipsets.h"
#include "Config.h"
#include "LedCommands.h"
#include "LedEngine.h"
#include "LedListener.h"
#include "ModeController.h"

// One stored mode parameter.
struct LedModeParam : xewe::FlexData<LedModeParam> {
    uint8_t mode = 0;
    std::string key;
    uint16_t value = 0;

    static constexpr auto fields() {
        return std::make_tuple(fld("mode", &LedModeParam::mode), fld("key", &LedModeParam::key),
            fld("value", &LedModeParam::value));
    }
};

// NVS `led/mode_params`: every parameter of every mode that has been active. Accepted only with
// schema == SCHEMA.
struct LedModeParams : xewe::FlexData<LedModeParams> {
    static constexpr uint8_t SCHEMA = 1;
    uint8_t schema = SCHEMA;
    std::vector<LedModeParam> params;

    static constexpr auto fields() {
        return std::make_tuple(
            fld("schema", &LedModeParams::schema), fld("params", &LedModeParams::params));
    }
};

class Led : public xewe::Module {
public:
    explicit Led(xewe::Os& host);

    void begin_routines_required() override;
    void begin_routines_init() override;
    void begin_routines_regular() override;
    void begin_routines_common() override;

    // `$led reset`: green, brightness 50, on, mode 0, current length (announced to the listeners),
    // then the whole `led` namespace (mode parameters included) is wiped and the device restarts;
    // the next boot runs the setup wizard again
    void reset(const bool verbose = false, const bool do_restart = true,
        const bool keep_enabled = true) override;

    std::string status(const bool verbose = false) const override;
    // use_clk, chip (RESTART), voltage (5/12/24), lines, num_led, colorder
    xewe::Settings settings() const override;
    // l_<i>_cnt, the live colour/brightness/state/mode and the current mode's parameters
    void schema_extra(xewe::SchemaOut& out) const override;

    // ---- API for other modules: apply, then tell every listener (origin: the caller, `this`)
    void sync_color(const std::array<uint8_t, 3> rgb, const void* origin);
    void sync_brightness(const uint8_t brightness, const void* origin);
    void sync_state(const bool state, const void* origin);
    void sync_mode(const uint8_t mode_id, const void* origin);
    void sync_length(const uint16_t length, const void* origin);
    void sync_all(const std::array<uint8_t, 3> rgb, const uint8_t brightness, const bool state,
        const uint8_t mode_id, const uint16_t length, const void* origin);

    // unknown key: nothing happens; a change announces the mode's colour (origin: the Led)
    void set_mode_param(std::string_view key, const uint16_t value);
    // rebuilds the current mode from its defaults; not announced
    void reset_current_mode();

    std::array<uint8_t, 3> get_rgb() const;
    std::array<uint8_t, 3> get_hsv() const;
    uint8_t get_brightness() const; // last non-zero brightness
    bool get_state() const;
    uint8_t get_current_mode_id() const;
    std::string_view get_current_mode_name() const;
    uint16_t get_current_mode_param(std::string_view key) const;
    // [{"id":0,"name":"Solid","params":[{"key":..,"display_name":..,"min":..,"max":..,"step":..,
    // "value":..,"type":"a|b"}]},...]; value is live for the running mode, the default otherwise
    std::string get_all_modes_json() const;
    uint16_t get_length() const;

protected:
    void on_setting_changed(const xewe::SettingDef& def) override;

private:
    // NVS, console, calibration and the render lock (a recursive mutex) for the engine
    class Io : public LedEngineIo {
    public:
        explicit Io(Led& owner) : led(owner) {}
        void save_u8(const char* key, uint8_t value) override;
        void save_u16(const char* key, uint16_t value) override;
        void save_bool(const char* key, bool value) override;
        void save_rgb(const std::array<uint8_t, 3>& rgb) override;
        void print(std::string_view text) override;
        void calibrate_color_order() override;
        void lock() override;
        void unlock() override;

    private:
        Led& led;
    };

    // mode parameters in RAM, written to `led/mode_params` once per change
    class ParamStore : public ModeParamStore {
    public:
        explicit ParamStore(Led& owner) : led(owner) {}
        uint16_t read(uint8_t mode_id, std::string_view key, uint16_t default_value) override;
        void write(uint8_t mode_id, std::string_view key, uint16_t value) override;
        void commit() override;

    private:
        Led& led;
        bool dirty = false;
    };

    // One per `$led` command row. Its handler lambda captures only the slot (`this`), which
    // knows the row; all rows share one handler type.
    struct CommandSlot {
        Led* led = nullptr;
        std::size_t index = 0;
        void attach();
    };

    // the colour-order calibration: the wizard's last step and `$led set_color_order ""`
    void calibrate_color_order();
    // claims the pins and registers the stored chip with FastLED; false when a pin is taken
    bool start_strip();
    bool set_leds_chipset(const LEDChipset chipset);
    void release_pins();

    void start_render_task();
    // asks the task to end after its current frame and waits for it (reset)
    void stop_render_task();
    static void render_task_entry(void* self);
    void render_task();
    void count_frame();

    // a wait that leaves the render task running, with a dot every 200 ms
    void wait_with_dots(uint32_t duration_ms);

    void load_mode_params();
    std::vector<uint16_t> read_line_lengths() const;

    std::array<CommandSlot, LED_COMMAND_COUNT> command_slots;
    Io io;
    ParamStore store;
    LedEngine engine;
    LedModeParams mode_params;

    // settings table rows
    bool use_clk{false};
    uint8_t chip{0};
    uint8_t voltage{5};
    uint16_t lines{0};
    uint16_t num_led{XEWE_LED_NUM_LED};
    uint8_t colorder{0};

    uint8_t active_voltage{5}; // last accepted voltage
    bool wizard_active{false};
    bool data_pin_claimed{false};
    bool clock_pin_claimed{false};

    SemaphoreHandle_t render_mutex{nullptr}; // recursive
    TaskHandle_t render_task_handle{nullptr};
    SemaphoreHandle_t render_task_ended{nullptr};
    std::atomic<bool> render_task_stop{false};
    uint16_t fps_counter{0};
    uint32_t fps_window_start_ms{0};
    std::atomic<uint16_t> fps_calculated{0};

public:
    // the change events (LedListener); declared after `engine`, which owns the set
    xewe::ListenerSet<LedListener, XEWE_LED_LISTENERS>& listeners;
};
