// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Mode.h
//
// Base of every LED effect. A mode owns its parameters (live values are kept in
// ModeParam::default_value) and renders one frame per loop() call into a CRGB buffer.
#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <XeWeCore.h>

#include "../Config.h"

struct ModeParam {
    std::string key;
    std::string display_name;
    uint16_t min_value;
    uint16_t max_value;
    uint16_t default_value;
    uint16_t step_value;
    char type; // a for additional (hidden on the web UI by default), b for basic
};

struct ModeConfig {
    uint8_t id;
    std::string name;
    std::vector<ModeParam> params;

    ModeConfig(
        uint8_t init_id, std::string init_name, const std::vector<ModeParam>& custom_params = {});
};

class Mode {
public:
    explicit Mode(ModeConfig mode_config, const std::map<std::string, uint16_t>& params = {});

    virtual ~Mode() = default;

    // required implementation
    virtual void loop(CRGB* leds, uint16_t num_leds) = 0;
    virtual std::array<uint8_t, 3> get_rgb() = 0;

    // getters
    uint8_t get_id() const;
    std::string_view get_name() const;
    std::vector<ModeParam> get_params() const;
    const ModeConfig& get_config() const;

    // the live value of `key`, 0 for a key the mode does not have
    uint16_t get_param(std::string_view key) const;

protected:
    ModeConfig config;
};
