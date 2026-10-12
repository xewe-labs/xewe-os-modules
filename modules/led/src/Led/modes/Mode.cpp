// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Mode.cpp

#include "Mode.h"

ModeConfig::ModeConfig(
    uint8_t init_id, std::string init_name, const std::vector<ModeParam>& custom_params)
    : id(init_id), name(std::move(init_name)) {
    params.insert(params.end(), custom_params.begin(), custom_params.end());
}

Mode::Mode(ModeConfig mode_config, const std::map<std::string, uint16_t>& params)
    : config(std::move(mode_config)) {
    // a value given for a key overrides the table default and becomes the live value
    for (auto& param : config.params) {
        auto it = params.find(param.key);
        if (it != params.end()) {
            param.default_value = it->second;
        }
    }
}

uint8_t Mode::get_id() const {
    return config.id;
}

std::string_view Mode::get_name() const {
    return config.name;
}

std::vector<ModeParam> Mode::get_params() const {
    return config.params;
}

const ModeConfig& Mode::get_config() const {
    return config;
}

uint16_t Mode::get_param(std::string_view key) const {
    for (const auto& p : config.params) {
        if (p.key == key) {
            return p.default_value;
        }
    }
    return 0;
}
