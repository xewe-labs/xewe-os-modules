// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-modes/src/LedModes/LedModes.cpp

#include "LedModes.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>

static_assert(sizeof(led_fx::Rgb) == sizeof(LedRgb), "led_fx::Rgb and LedRgb must share a layout");

namespace {

// Christmas Lights flicker seed, new at every mode start (2.3.x random16() in the mode constructor).
// Never 0: Effects.h prepare() treats 0 as "keep the fixed default seed" (host tests).
uint32_t effect_seed() {
    return (esp_random() ^ millis()) | 1u;
}

bool parse_long(const std::string& text, long& out) {
    if (text.empty()) return false;
    char*      end   = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') return false;
    out = value;
    return true;
}

bool parse_hex_color(const std::string& text, LedRgb& out) {
    std::string s = text;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return false;
    for (const char c : s) {   // strtoul alone takes "-12345", "+12345", " 12345", "0x1234"
        const char l = static_cast<char>(c | 0x20);
        if (!((c >= '0' && c <= '9') || (l >= 'a' && l <= 'f'))) return false;
    }
    char*               end   = nullptr;
    const unsigned long value = std::strtoul(s.c_str(), &end, 16);
    if (*end != '\0') return false;
    out = {static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    return true;
}

// "Color Fade Two Zone" == "colorfadetwozone" == "color_fade_two_zone"
std::string fold(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == ' ' || c == '_' || c == '-') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

LedModes::LedModes(xewe::Os& host, LedStrip& led_strip_ref)
    : xewe::Module(host,
          /* id                  */ "led_modes",
          /* name                */ "Led Modes",
          /* description         */ "LED effects: solid, fades, pulse, rainbow, christmas lights",
          /* requires_init_setup */ false,
          /* can_be_disabled     */ false,
          /* has_cli_cmds        */ true)
    , led_strip(led_strip_ref)
{
    add_requirement(led_strip);

    register_command({"list", "List the modes and their parameters", "$led_modes list", 0,
                      [this](xewe::span<const std::string> args) { cli_list(args); }});
    register_command({"set", "Select a mode by id or name", "$led_modes set 5", 1,
                      [this](xewe::span<const std::string> args) { cli_set(args); }});
    register_command({"param", "Set a parameter: <mode> <key> <value>", "$led_modes param 5 speed 7", 3,
                      [this](xewe::span<const std::string> args) { cli_param(args); }});
    register_command({"color", "Set the colour of the current mode <rrggbb>", "$led_modes color ff8000", 1,
                      [this](xewe::span<const std::string> args) { cli_color(args); }});
    register_command({"color", "Print the colour of the current mode (rrggbb)", "$led_modes color", 0,
                      [this](xewe::span<const std::string>) {
                          os.serial.printf("Led Modes: color %06lx", static_cast<unsigned long>(get_color()));
                      }});
    register_command({"reset_params", "Reset the current mode's parameters to their defaults", "$led_modes reset_params", 0,
                      [this](xewe::span<const std::string> args) { cli_reset_params(args); }});
    register_command({"reset_params", "Reset a mode's parameters to their defaults <mode>", "$led_modes reset_params 5", 1,
                      [this](xewe::span<const std::string> args) { cli_reset_params(args); }});
    register_command({"speed", "Set the speed of the current mode <n>", "$led_modes speed 10", 1,
                      [this](xewe::span<const std::string> args) { cli_speed(args); }});
}

// =============================================================================
// Module logic
// =============================================================================
void LedModes::begin_routines_regular() {
    uint8_t mode_id = os.nvs.read<uint8_t>(id, "mode_id", 0);
    if (led_fx::find_mode(mode_id) == nullptr) mode_id = 0;

    Slot slot;
    slot.mode = mode_id;
    load_params(mode_id, slot.params);
    led_fx::prepare(mode_id, slot.state, led_strip.get_length(), effect_seed());   // no allocation in the render task
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        current       = std::move(slot);
        transitioning = false;
    }
    led_strip.set_frame_source(this);
}

void LedModes::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        current       = Slot{};
        led_fx::default_params(led_fx::MODES[0], current.params);
        transitioning = false;
    }
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string LedModes::status(const bool verbose) const {
    uint8_t  mode_id;
    uint16_t values[led_fx::MAX_PARAMS];
    bool     fading;
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        mode_id = current.mode;
        for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) values[i] = current.params[i];
        fading = transitioning;
    }
    const led_fx::ModeDef& mode  = led_fx::MODES[mode_id];
    const led_fx::Rgb      color = led_fx::mode_color(mode_id, values);

    char line[160];
    std::string text = Module::status(false);
    std::snprintf(line, sizeof(line), "\nMode:       [%u] %s\nColor:      %02x%02x%02x\nTransition: %s\nParams:",
                  static_cast<unsigned>(mode_id), mode.name, color.r, color.g, color.b,
                  fading ? "running" : "idle");
    text += line;
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        const led_fx::ParamDef& p = mode.params[i];
        std::snprintf(line, sizeof(line), "\n    %s = %u [%u-%u] (%s)", p.key, static_cast<unsigned>(values[i]),
                      static_cast<unsigned>(p.min_value), static_cast<unsigned>(p.max_value), p.display);
        text += line;
    }
    if (verbose) os.serial.print(text);
    return text;
}

// =============================================================================
// Rendering (strip render task, render mutex held)
// =============================================================================
void LedModes::render(LedRgb* frame, uint16_t count, uint32_t now_ms) {
    led_fx::Rgb* out = reinterpret_cast<led_fx::Rgb*>(frame);

    if (!transitioning) {
        led_fx::render(current.mode, current.params, current.state, out, count, now_ms);
        return;
    }
    if (transition_start_ms == 0) transition_start_ms = now_ms ? now_ms : 1;   // first frame of the fade
    if (buffer_old.size() != count) buffer_old.resize(count);
    if (buffer_new.size() != count) buffer_new.resize(count);

    led_fx::render(previous.mode, previous.params, previous.state, buffer_old.data(), count, now_ms);
    led_fx::render(current.mode, current.params, current.state, buffer_new.data(), count, now_ms);
    const uint8_t progress = led_fx::transition_progress(now_ms - transition_start_ms);
    led_fx::crossfade(buffer_old.data(), buffer_new.data(), out, count, progress);

    if (progress == 255) {
        transitioning = false;
        previous      = Slot{};
    }
}

// =============================================================================
// Public API
// =============================================================================
bool LedModes::set_mode(int mode_id, const void* origin) {
    if (led_fx::find_mode(mode_id) == nullptr) return false;
    uint16_t values[led_fx::MAX_PARAMS];
    load_params(static_cast<uint8_t>(mode_id), values);
    activate(static_cast<uint8_t>(mode_id), values);
    os.nvs.write<uint8_t>(id, "mode_id", static_cast<uint8_t>(mode_id));
    const uint8_t  m     = static_cast<uint8_t>(mode_id);
    const uint32_t color = get_color();
    led_strip.notify_listeners([&](LedListener& l) {
        l.on_mode(m, origin);
        l.on_color(color, origin);
    });
    return true;
}

bool LedModes::set_param(int mode_id, const std::string& param, int32_t value, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    const int index = led_fx::param_index(*mode, param.c_str());
    if (index < 0) return false;

    uint16_t values[led_fx::MAX_PARAMS];
    load_params(mode->id, values);
    if (mode->id == get_mode()) {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) values[i] = current.params[i];
    }
    uint16_t old_values[led_fx::MAX_PARAMS];
    for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) old_values[i] = values[i];
    values[index] = led_fx::clamp_param(mode->params[index], value);
    persist_params(mode->id, values);
    if (mode->id == get_mode()) activate(mode->id, values);
    notify_params(mode->id, old_values, values, origin);
    return true;
}

bool LedModes::set_color(LedRgb color, const void* origin) {
    const uint8_t          mode_id = get_mode();
    const led_fx::ModeDef& mode    = led_fx::MODES[mode_id];
    const int              hue     = led_fx::param_index(mode, "hue");
    const int              sat     = led_fx::param_index(mode, "sat");
    if (hue < 0 && sat < 0) return false;

    const std::array<uint8_t, 3> hsv = xewe::color::rgb_to_hsv({color.r, color.g, color.b});
    uint16_t values[led_fx::MAX_PARAMS];
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) values[i] = current.params[i];
    }
    uint16_t old_values[led_fx::MAX_PARAMS];
    for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) old_values[i] = values[i];
    if (hue >= 0) values[hue] = led_fx::clamp_param(mode.params[hue], hsv[0]);
    if (sat >= 0) values[sat] = led_fx::clamp_param(mode.params[sat], hsv[1]);
    persist_params(mode_id, values);
    activate(mode_id, values);
    notify_params(mode_id, old_values, values, origin);
    return true;
}

bool LedModes::set_speed(int32_t value, const void* origin) {
    return set_param(get_mode(), "speed", value, origin);
}

// 2.3.x reset_current_mode: every parameter back to its table default in one pass: one NVS write per
// parameter, one cross-fade (only when `mode_id` is the current mode), on_param per changed value.
bool LedModes::reset_params(int mode_id, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    const bool is_current = mode->id == get_mode();
    uint16_t   old_values[led_fx::MAX_PARAMS];
    load_params(mode->id, old_values);
    if (is_current) {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) old_values[i] = current.params[i];
    }
    uint16_t values[led_fx::MAX_PARAMS];
    led_fx::default_params(*mode, values);
    persist_params(mode->id, values);
    if (is_current) activate(mode->id, values);
    notify_params(mode->id, old_values, values, origin);
    return true;
}

uint32_t LedModes::get_color() const {
    uint8_t  mode_id;
    uint16_t values[led_fx::MAX_PARAMS];
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        mode_id = current.mode;
        for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) values[i] = current.params[i];
    }
    const led_fx::Rgb c = led_fx::mode_color(mode_id, values);
    return led_pack_rgb(c.r, c.g, c.b);
}

// on_param per changed value; on_color when the change is on the current mode and moved its colour
void LedModes::notify_params(uint8_t mode_id, const uint16_t* old_values, const uint16_t* new_values,
                             const void* origin) {
    const led_fx::ModeDef& mode       = led_fx::MODES[mode_id];
    const bool             is_current = mode_id == get_mode();
    const led_fx::Rgb      before     = led_fx::mode_color(mode_id, old_values);
    const led_fx::Rgb      after      = led_fx::mode_color(mode_id, new_values);
    const bool             recolored  = is_current && (before.r != after.r || before.g != after.g || before.b != after.b);
    led_strip.notify_listeners([&](LedListener& l) {
        for (uint8_t i = 0; i < mode.param_count; ++i) {
            if (old_values[i] != new_values[i]) l.on_param(mode_id, mode.params[i].key, new_values[i], origin);
        }
        if (recolored) l.on_color(led_pack_rgb(after.r, after.g, after.b), origin);
    });
}

uint8_t LedModes::get_mode() const {
    xewe::LockGuard lock(led_strip.get_render_mutex());
    return current.mode;
}

uint16_t LedModes::get_param(int mode_id, const std::string& param) const {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return 0;
    const int index = led_fx::param_index(*mode, param.c_str());
    if (index < 0) return 0;
    uint16_t values[led_fx::MAX_PARAMS];
    load_params(mode->id, values);
    return values[index];
}

// =============================================================================
// Helpers
// =============================================================================
std::string LedModes::nvs_param_name(uint8_t mode_id, const char* param) const {
    return "m:" + std::to_string(mode_id) + ":" + param;   // <= 15 chars, checked by the host test
}

void LedModes::load_params(uint8_t mode_id, uint16_t* out) const {
    const led_fx::ModeDef& mode = led_fx::MODES[mode_id];
    led_fx::default_params(mode, out);
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        const uint16_t stored = os.nvs.read<uint16_t>(id, nvs_param_name(mode_id, mode.params[i].key), out[i]);
        out[i] = led_fx::clamp_param(mode.params[i], stored);
    }
}

void LedModes::persist_params(uint8_t mode_id, const uint16_t* values) {
    const led_fx::ModeDef& mode = led_fx::MODES[mode_id];
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        os.nvs.write<uint16_t>(id, nvs_param_name(mode_id, mode.params[i].key), values[i]);
    }
}

void LedModes::activate(uint8_t mode_id, const uint16_t* values) {
    Slot next;
    next.mode = mode_id;
    for (uint8_t i = 0; i < led_fx::MAX_PARAMS; ++i) next.params[i] = values[i];
    // allocate here (main loop), not in the render task: effect state and the two fade buffers
    const uint16_t n = led_strip.get_length();
    led_fx::prepare(mode_id, next.state, n, effect_seed());
    {
        xewe::LockGuard lock(led_strip.get_render_mutex());
        if (buffer_old.size() != n) buffer_old.resize(n);
        if (buffer_new.size() != n) buffer_new.resize(n);
        previous            = std::move(current);   // a fade in progress restarts from the newer mode
        current             = std::move(next);
        transitioning       = true;
        transition_start_ms = 0;                    // set by the next render()
    }
    led_strip.clear_fill();
}

int LedModes::parse_mode(const std::string& text) const {
    long number = 0;
    if (parse_long(text, number)) return led_fx::find_mode(number) ? static_cast<int>(number) : -1;
    const std::string wanted = fold(text);
    for (const led_fx::ModeDef& m : led_fx::MODES) {
        if (fold(m.name) == wanted) return m.id;
    }
    return -1;
}

// =============================================================================
// CLI
// =============================================================================
void LedModes::cli_list(xewe::span<const std::string>) {
    const uint8_t active = get_mode();
    for (const led_fx::ModeDef& m : led_fx::MODES) {
        std::string line = std::string(m.id == active ? "* " : "  ") + "[" + std::to_string(m.id) + "] " + m.name + ":";
        for (uint8_t i = 0; i < m.param_count; ++i) line += std::string(" ") + m.params[i].key;
        os.serial.print(line);
    }
}

void LedModes::cli_set(xewe::span<const std::string> args) {
    const int mode_id = parse_mode(args[0]);
    if (mode_id < 0 || !set_mode(mode_id)) {
        os.serial.printf("Led Modes: unknown mode '%s' (see $led_modes list)", args[0].c_str());
        return;
    }
    os.serial.printf("Led Modes: mode [%d] %s", mode_id, led_fx::MODES[mode_id].name);
}

void LedModes::cli_param(xewe::span<const std::string> args) {
    const int mode_id = parse_mode(args[0]);
    long      value   = 0;
    if (mode_id < 0) {
        os.serial.printf("Led Modes: unknown mode '%s' (see $led_modes list)", args[0].c_str());
        return;
    }
    if (!parse_long(args[2], value)) {
        os.serial.print("Led Modes: value must be a number");
        return;
    }
    if (!set_param(mode_id, args[1], static_cast<int32_t>(value))) {
        os.serial.printf("Led Modes: mode [%d] has no parameter '%s'", mode_id, args[1].c_str());
        return;
    }
    os.serial.printf("Led Modes: [%d] %s = %u", mode_id, args[1].c_str(),
                     static_cast<unsigned>(get_param(mode_id, args[1])));
}

void LedModes::cli_color(xewe::span<const std::string> args) {
    LedRgb color{};
    if (!parse_hex_color(args[0], color)) {
        os.serial.print("Led Modes: color needs rrggbb (hex)");
        return;
    }
    if (!set_color(color)) {
        os.serial.print("Led Modes: the current mode has no colour");
        return;
    }
    os.serial.printf("Led Modes: color %02x%02x%02x", color.r, color.g, color.b);
}

void LedModes::cli_reset_params(xewe::span<const std::string> args) {
    const int mode_id = args.size() == 0 ? static_cast<int>(get_mode()) : parse_mode(args[0]);
    if (mode_id < 0 || !reset_params(mode_id)) {
        os.serial.printf("Led Modes: unknown mode '%s' (see $led_modes list)", args.size() ? args[0].c_str() : "");
        return;
    }
    os.serial.printf("Led Modes: [%d] %s parameters reset to defaults", mode_id, led_fx::MODES[mode_id].name);
}

void LedModes::cli_speed(xewe::span<const std::string> args) {
    long value = 0;
    if (!parse_long(args[0], value)) {
        os.serial.print("Led Modes: speed must be a number");
        return;
    }
    if (!set_speed(static_cast<int32_t>(value))) {
        os.serial.print("Led Modes: the current mode has no speed");
        return;
    }
    os.serial.printf("Led Modes: speed %u", static_cast<unsigned>(get_param(get_mode(), "speed")));
}
