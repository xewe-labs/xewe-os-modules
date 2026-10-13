// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/ModeController.h
//
// Owns the running mode, builds a new instance on every mode, colour or parameter change and
// cross-fades from the old output to the new one over the transition time (FastLED blend, linear
// in AsyncTimer progress). Mode parameters are read from and written to a ModeParamStore.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <XeWeCore.h>

#include "Config.h"
#include "ModeRegistry.h"
#include "modes/Mode.h"

// Persistent mode parameters, one uint16_t per (mode id, parameter key).
class ModeParamStore {
public:
    virtual ~ModeParamStore() = default;
    // the stored value, or default_value when there is none
    virtual uint16_t read(uint8_t mode_id, std::string_view key, uint16_t default_value) = 0;
    virtual void write(uint8_t mode_id, std::string_view key, uint16_t value) = 0;
    // after the writes of one set_mode: a store that batches writes persists them here
    virtual void commit() {}
};

class ModeController {
public:
    ModeController(CRGB* output_buffer, uint16_t num_leds, uint16_t transition_delay_ms,
        ModeParamStore& store);

    void loop();

    // Mode Management
    void set_mode(const uint8_t mode_id, const std::map<std::string, uint16_t>& params = {});
    bool set_mode_param(std::string_view key, uint16_t value);
    void set_rgb(const std::array<uint8_t, 3> new_rgb);
    void set_hsv(const std::array<uint8_t, 3> new_hsv);

    bool adj_mode_param(std::string_view key, int32_t value_delta);

    void reset_current_mode();

    // Getters
    std::array<uint8_t, 3> get_rgb() const;
    uint8_t get_current_mode_id() const;
    std::string_view get_current_mode_name() const;
    std::vector<ModeParam> get_current_mode_params() const;
    const ModeConfig& get_current_mode_config() const;
    uint16_t get_current_mode_param(std::string_view key) const;

    const ModeConfig& get_mode_config(uint8_t mode_id) const;
    std::string get_all_modes_json() const;

    uint16_t get_mode_transition_delay() const;

    // Setters
    void set_length(const uint16_t new_num_leds);

private:
    void update_interpolate_buffers(CRGB* output_buffer_ref);
    std::map<std::string, uint16_t> get_params_as_map() const;

    std::map<std::string, uint16_t> get_default_params_for_mode(uint8_t mode_id) const;
    std::map<std::string, uint16_t> load_mode_params_from_nvs(uint8_t mode_id) const;
    void persist_mode_params_to_nvs(uint8_t mode_id) const;
    uint16_t normalize_mode_param_value(std::string_view key, int32_t value) const;

    uint16_t num_leds;
    std::unique_ptr<xewe::AsyncTimer<uint8_t>> transition_timer;

    std::unique_ptr<Mode> current_mode;
    std::unique_ptr<Mode> old_mode;

    CRGB* output_buffer;

    std::array<CRGB, XEWE_MODULE_LED_NUM_LEDS_MAX> buffer_current;
    std::array<CRGB, XEWE_MODULE_LED_NUM_LEDS_MAX> buffer_old;

    bool buffer_old_static_flag;

    ModeParamStore& store;
};
