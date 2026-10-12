// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/ModeController.cpp

#include "ModeController.h"

ModeController::ModeController(
    CRGB* output_buffer, uint16_t num_leds, uint16_t transition_delay_ms, ModeParamStore& store)
    : num_leds(num_leds), output_buffer(output_buffer), buffer_old_static_flag(false),
      store(store) {
    fill_solid(buffer_current.data(), XEWE_MODULE_LED_NUM_LEDS_MAX, CRGB::Black);
    fill_solid(buffer_old.data(), XEWE_MODULE_LED_NUM_LEDS_MAX, CRGB::Black);

    transition_timer = std::make_unique<xewe::AsyncTimer<uint8_t>>(transition_delay_ms, 0, 255);

    // On boot, restore mode 0 params from the store if present; fades in from black.
    set_mode(0, {});
}

void ModeController::loop() {
    if (transition_timer->is_active()) {
        update_interpolate_buffers(output_buffer);
        if (transition_timer->is_done()) {
            transition_timer->terminate();
            buffer_old_static_flag = false;
        }
        return;
    }

    current_mode->loop(output_buffer, num_leds);
}

void ModeController::set_mode(
    const uint8_t mode_id, const std::map<std::string, uint16_t>& params) {
    auto& registry = ModeRegistry::get_registry();
    // an unknown id builds mode 0; the caller still stores and announces the requested id, so a
    // stored command with that id keeps behaving the same way
    const uint8_t effective_mode_id = registry.count(mode_id) ? mode_id : 0;
    ModeFactory factory = registry.at(effective_mode_id);

    // Resolution order:
    // 1. mode defaults
    // 2. stored values
    // 3. caller-provided overrides
    std::map<std::string, uint16_t> resolved_params =
        get_default_params_for_mode(effective_mode_id);

    const auto nvs_params = load_mode_params_from_nvs(effective_mode_id);
    for (const auto& [key, value] : nvs_params) {
        resolved_params[key] = value;
    }

    for (const auto& [key, value] : params) {
        resolved_params[key] = value;
    }

    // a change during a transition freezes the blended frame as the new "old" picture
    if (transition_timer->is_active()) {
        update_interpolate_buffers(buffer_old.data());
        buffer_old_static_flag = true;
    }

    old_mode = std::move(current_mode);
    current_mode = factory(resolved_params);

    // Persist sanitized/live values after mode creation.
    persist_mode_params_to_nvs(current_mode->get_id());

    transition_timer->reset();
    transition_timer->initiate();
}

bool ModeController::set_mode_param(std::string_view key, uint16_t value) {
    auto params_map = get_params_as_map();
    auto it = params_map.find(std::string(key));

    if (it == params_map.end()) {
        return false;
    }

    it->second = normalize_mode_param_value(key, value);
    set_mode(get_current_mode_id(), params_map);

    return true;
}

void ModeController::set_rgb(const std::array<uint8_t, 3> new_rgb) {
    auto params_map = get_params_as_map();

    if (params_map.count("hue") || params_map.count("sat")) {
        const std::array<uint8_t, 3> new_hsv = xewe::color::rgb_to_hsv(new_rgb);
        if (params_map.count("hue"))
            params_map["hue"] = new_hsv[0];
        if (params_map.count("sat"))
            params_map["sat"] = new_hsv[1];
    }

    if (params_map.count("r"))
        params_map["r"] = new_rgb[0];
    if (params_map.count("g"))
        params_map["g"] = new_rgb[1];
    if (params_map.count("b"))
        params_map["b"] = new_rgb[2];

    set_mode(get_current_mode_id(), params_map);
}

void ModeController::set_hsv(const std::array<uint8_t, 3> new_hsv) {
    auto params_map = get_params_as_map();

    if (params_map.count("r") || params_map.count("g") || params_map.count("b")) {
        const std::array<uint8_t, 3> new_rgb = xewe::color::hsv_to_rgb(new_hsv);
        if (params_map.count("r"))
            params_map["r"] = new_rgb[0];
        if (params_map.count("g"))
            params_map["g"] = new_rgb[1];
        if (params_map.count("b"))
            params_map["b"] = new_rgb[2];
    }

    if (params_map.count("hue"))
        params_map["hue"] = new_hsv[0];
    if (params_map.count("sat"))
        params_map["sat"] = new_hsv[1];

    set_mode(get_current_mode_id(), params_map);
}

bool ModeController::adj_mode_param(std::string_view key, int32_t value_delta) {
    for (const auto& param : current_mode->get_params()) {
        if (param.key == key) {
            const int32_t current_value = static_cast<int32_t>(current_mode->get_param(param.key));
            const uint16_t new_value = normalize_mode_param_value(key, current_value + value_delta);
            set_mode_param(key, new_value);

            return true;
        }
    }

    // a key the current mode does not have changes nothing, like set_mode_param
    return false;
}

void ModeController::reset_current_mode() {
    if (!current_mode) {
        return;
    }

    const uint8_t mode_id = get_current_mode_id();
    const auto default_params = get_default_params_for_mode(mode_id);

    // Rebuild the current mode using only its defaults.
    // set_mode() will also persist the sanitized/live values back to the store.
    set_mode(mode_id, default_params);
}

std::array<uint8_t, 3> ModeController::get_rgb() const {
    return current_mode->get_rgb();
}

uint8_t ModeController::get_current_mode_id() const {
    return current_mode->get_id();
}

std::string_view ModeController::get_current_mode_name() const {
    return current_mode->get_name();
}

std::vector<ModeParam> ModeController::get_current_mode_params() const {
    return current_mode->get_params();
}

const ModeConfig& ModeController::get_current_mode_config() const {
    return current_mode->get_config();
}

uint16_t ModeController::get_current_mode_param(std::string_view key) const {
    if (!current_mode) {
        return 0;
    }

    for (const auto& param : current_mode->get_params()) {
        if (param.key == key) {
            return current_mode->get_param(param.key);
        }
    }

    return 0;
}

const ModeConfig& ModeController::get_mode_config(uint8_t mode_id) const {
    // one temporary instance per id and process; ChristmasLights draws random16() values here
    static std::map<uint8_t, ModeConfig> config_cache;

    auto it = config_cache.find(mode_id);

    if (it == config_cache.end()) {
        auto& registry = ModeRegistry::get_registry();
        ModeFactory factory = registry.count(mode_id) ? registry.at(mode_id) : registry.at(0);

        auto temp_mode = factory({});
        it = config_cache.emplace(mode_id, temp_mode->get_config()).first;
    }

    return it->second;
}

std::string ModeController::get_all_modes_json() const {
    std::string json = "[";
    bool first_mode = true;

    const auto& registry = ModeRegistry::get_registry();

    for (const auto& [id, factory] : registry) {
        if (!first_mode)
            json += ",";
        first_mode = false;

        auto temp_mode = factory({});
        const auto& config = temp_mode->get_config();

        const bool is_active_mode = (id == get_current_mode_id());

        json += "{\"id\":" + std::to_string(config.id) + ",\"name\":\"" + config.name + "\"" +
            ",\"params\":[";

        bool first_param = true;
        for (const auto& p : config.params) {
            if (!first_param)
                json += ",";
            first_param = false;

            // live value for the running mode, code default for the others
            int output_value = p.default_value;
            if (is_active_mode) {
                output_value = current_mode->get_param(p.key);
            }

            json += "{\"key\":\"" + p.key + "\"" + ",\"display_name\":\"" + p.display_name + "\"" +
                ",\"min\":" + std::to_string(p.min_value) +
                ",\"max\":" + std::to_string(p.max_value) +
                ",\"step\":" + std::to_string(p.step_value) +
                ",\"value\":" + std::to_string(output_value) + ",\"type\":\"" +
                std::string(1, p.type) + "\"}";
        }

        json += "]}";
    }

    json += "]";
    return json;
}

std::vector<std::pair<uint8_t, std::string>> ModeController::get_all_modes() const {
    std::vector<std::pair<uint8_t, std::string>> modes;

    const auto& registry = ModeRegistry::get_registry();
    for (const auto& [id, factory] : registry) {
        auto temp_mode = factory({});
        modes.emplace_back(id, temp_mode->get_config().name);
    }

    return modes;
}

uint16_t ModeController::get_mode_transition_delay() const {
    return transition_timer->get_delay_ms();
}

void ModeController::set_length(const uint16_t new_num_leds) {
    num_leds = new_num_leds;
}

void ModeController::update_interpolate_buffers(CRGB* output_buffer_ref) {
    if (!buffer_old_static_flag && old_mode) {
        old_mode->loop(buffer_old.data(), num_leds);
    }

    current_mode->loop(buffer_current.data(), num_leds);
    const uint8_t progress = transition_timer->get_current_value();

    for (uint16_t i = 0; i < num_leds; i++) {
        output_buffer_ref[i] = blend(buffer_old[i], buffer_current[i], progress);
    }
}

std::map<std::string, uint16_t> ModeController::get_params_as_map() const {
    std::map<std::string, uint16_t> map;

    if (!current_mode) {
        return map;
    }

    for (const auto& param : current_mode->get_params()) {
        map[param.key] = current_mode->get_param(param.key);
    }

    return map;
}

std::map<std::string, uint16_t> ModeController::get_default_params_for_mode(uint8_t mode_id) const {
    std::map<std::string, uint16_t> map;
    const auto& config = get_mode_config(mode_id);

    for (const auto& param : config.params) {
        map[param.key] = param.default_value;
    }

    return map;
}

std::map<std::string, uint16_t> ModeController::load_mode_params_from_nvs(uint8_t mode_id) const {
    std::map<std::string, uint16_t> map;
    const auto& config = get_mode_config(mode_id);

    // stored values are not clamped on load
    for (const auto& param : config.params) {
        map[param.key] = store.read(mode_id, param.key, param.default_value);
    }

    return map;
}

void ModeController::persist_mode_params_to_nvs(uint8_t mode_id) const {
    if (!current_mode) {
        return;
    }

    const auto& config = get_mode_config(mode_id);

    for (const auto& param : config.params) {
        store.write(mode_id, param.key, current_mode->get_param(param.key));
    }
    store.commit();
}

uint16_t ModeController::normalize_mode_param_value(std::string_view key, int32_t value) const {
    for (const auto& param : current_mode->get_params()) {
        if (param.key != key) {
            continue;
        }

        // "hue" wraps modulo 256 (only reached when the current mode has a "hue" parameter); every
        // other key, hue_b too, clamps to its range. Stored commands rely on this exact rule.
        if (key == "hue") {
            int32_t wrapped = value % 256;
            if (wrapped < 0) {
                wrapped += 256;
            }
            return static_cast<uint16_t>(wrapped);
        }

        const int32_t clamped = std::clamp<int32_t>(
            value, static_cast<int32_t>(param.min_value), static_cast<int32_t>(param.max_value));

        return static_cast<uint16_t>(clamped);
    }

    const int32_t clamped =
        std::clamp<int32_t>(value, 0, static_cast<int32_t>(std::numeric_limits<uint16_t>::max()));

    return static_cast<uint16_t>(clamped);
}
