// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Led.cpp

#include "Led.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <FastLED.h>

static_assert(sizeof(CRGB) == 3, "CRGB must be 3 packed bytes");

namespace {

bool parse_uint(const std::string& text, long min_value, long max_value, long& out) {
    if (text.empty()) return false;
    char*      end   = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < min_value || value > max_value) return false;
    out = value;
    return true;
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

// "Color Fade Two Zone" == "colorfadetwozone" == "color_fade_two_zone"; "LIST" == "list"
std::string fold(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == ' ' || c == '_' || c == '-') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Christmas Lights flicker seed, new at every mode start (2.3.x random16() in the mode constructor).
// Never 0: led_fx::prepare() treats 0 as "keep the fixed default seed" (host tests).
uint32_t effect_seed() {
    return (esp_random() ^ millis()) | 1u;
}

// Source channel for each output slot, indexed like LED_COLOR_ORDERS.
constexpr uint8_t CHANNEL_MAP[6][3] = {
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
};

constexpr const char* MODE_USAGE =
    "Led: usage: $led mode list | set <m> | param <m> <key> <value> | color [rrggbb] | reset_params [m] | speed <n>";

}  // namespace

Led::Led(xewe::Os& host, LedConfig config_param)
    : xewe::Module(host,
          /* id                  */ "led",
          /* name                */ "Led",
          /* description         */ "Drives an addressable LED strip and its modes",
          /* requires_init_setup */ false,
          /* can_be_disabled     */ false,
          /* has_cli_cmds        */ true)
    , config(config_param)
    , brightness(config_param.brightness_fade_ms)
{
    frame        = new LedRgb[LED_STRIP_NUM_LEDS_MAX]();
    out          = new uint8_t[LED_STRIP_NUM_LEDS_MAX * 3]();
    render_mutex = xSemaphoreCreateMutex();
    led_fx::default_params(*current.def, current.params);

    // ---- strip ----
    register_command({"on", "Turn the strip on (fades in)", "$led on", 0,
                      [this](xewe::span<const std::string>) { set_state(true); os.serial.print("Led: on"); }});
    register_command({"off", "Turn the strip off (fades out)", "$led off", 0,
                      [this](xewe::span<const std::string>) { set_state(false); os.serial.print("Led: off"); }});
    register_command({"brightness", "Set brightness <0-255>", "$led brightness 128", 1,
                      [this](xewe::span<const std::string> args) { cli_brightness(args); }});
    register_command({"set", "Set <key> <value>: chip, num_led, colorder, voltage", "$led set num_led 60", 2,
                      [this](xewe::span<const std::string> args) { cli_set(args); }});
    register_command({"fill", "Fill with a static colour <rrggbb>, or `off` to resume the mode", "$led fill ff0000", 1,
                      [this](xewe::span<const std::string> args) { cli_fill(args); }});
    register_command({"checksum", "Print the CRC-32 of the current frame", "$led checksum", 0,
                      [this](xewe::span<const std::string>) {
                          os.serial.printf("Led frame checksum: %08lx (%u leds)",
                                           static_cast<unsigned long>(get_frame_checksum()),
                                           static_cast<unsigned>(get_length()));
                      }});

    // ---- modes: one `mode` command per argument count, dispatched on the first argument -----------
    // (the core picks the registration whose count matches; on a mismatch it shows the first usage)
    register_command({"mode", "Modes: list | color | reset_params (current mode)", "$led mode list", 1,
                      [this](xewe::span<const std::string> args) { cli_mode(args); }});
    register_command({"mode", "Modes: set <m> | color <rrggbb> | reset_params <m> | speed <n>", "$led mode set 5", 2,
                      [this](xewe::span<const std::string> args) { cli_mode(args); }});
    register_command({"mode", "Modes: param <m> <key> <value>", "$led mode param 5 speed 7", 4,
                      [this](xewe::span<const std::string> args) { cli_mode(args); }});

    // 2.3.x command names (LM2, LM15): same handlers as above
    register_command({"set_brightness", "2.3.x name of `brightness`", "$led set_brightness 128", 1,
                      [this](xewe::span<const std::string> args) { cli_brightness(args); }});
    register_command({"set_state", "2.3.x: set on/off state <0|1>", "$led set_state 0", 1,
                      [this](xewe::span<const std::string> args) {
                          const bool on = args[0] != "0";
                          set_state(on);
                          os.serial.print(on ? "Led: on" : "Led: off");
                      }});
    register_command({"toggle_state", "2.3.x: on -> off, off -> on", "$led toggle_state", 0,
                      [this](xewe::span<const std::string>) {
                          const bool on = !get_state();
                          set_state(on);
                          os.serial.print(on ? "Led: on" : "Led: off");
                      }});
    register_command({"turn_on", "2.3.x name of `on`", "$led turn_on", 0,
                      [this](xewe::span<const std::string>) { set_state(true); os.serial.print("Led: on"); }});
    register_command({"turn_off", "2.3.x name of `off`", "$led turn_off", 0,
                      [this](xewe::span<const std::string>) { set_state(false); os.serial.print("Led: off"); }});
    register_command({"set_length", "2.3.x name of `set num_led`", "$led set_length 60", 1,
                      [this](xewe::span<const std::string> args) { set_setting("num_led", args[0]); }});
    register_command({"set_color_order", "2.3.x name of `set colorder`", "$led set_color_order GRB", 1,
                      [this](xewe::span<const std::string> args) { set_setting("colorder", args[0]); }});
    register_command({"set_mode", "2.3.x name of `mode set`", "$led set_mode 5", 1,
                      [this](xewe::span<const std::string> args) { mode_set(args[0]); }});
    register_command({"set_mode_param", "2.3.x name of `mode param`", "$led set_mode_param 5 speed 7", 3,
                      [this](xewe::span<const std::string> args) { mode_param(args[0], args[1], args[2]); }});
}

// =============================================================================
// Module logic
// =============================================================================
void Led::begin_routines_regular() {
    // ---- strip ----
    const LedChipset* default_chip = led_chipset_by_name(LED_CHIPSET);
    if (default_chip == nullptr) default_chip = led_chipset_by_name("WS2812B");
    int default_order = led_color_order_index(LED_COLOR_ORDER);
    if (default_order < 0) default_order = 2;  // GRB

    stored_chip_id = os.nvs.read<uint8_t>(id, "chip", default_chip->id);
    num_led        = os.nvs.read<uint16_t>(id, "num_led", LED_COUNT);
    color_order    = os.nvs.read<uint8_t>(id, "colorder", static_cast<uint8_t>(default_order));
    voltage        = os.nvs.read<uint8_t>(id, "voltage", LED_VOLTAGE);

    num_led        = std::clamp<uint16_t>(num_led, 1, LED_STRIP_NUM_LEDS_MAX);
    if (color_order > 5) color_order = static_cast<uint8_t>(default_order);

    if (!add_leds(stored_chip_id)) {
        os.serial.printf("Led: stored chip id %u is not compiled in, using %s",
                         static_cast<unsigned>(stored_chip_id), default_chip->name);
        add_leds(default_chip->id);
    }
    FastLED.setBrightness(255);

    brightness_setting = os.nvs.read<uint8_t>(id, "brightness", 128);
    if (brightness_setting == 0) brightness_setting = 128;   // a 0 stored by v2 0.1.0 (`brightness 0`): use the default

    // ---- modes: stored mode (unknown id -> the first registry row), prepared here, not in the task --
    const led_fx::ModeDef* mode = led_fx::find_mode(os.nvs.read<uint8_t>(id, "mode_id", led_fx::default_mode().id));
    if (mode == nullptr) mode = &led_fx::default_mode();
    Slot slot;
    slot.def = mode;
    load_params(*mode, slot.params);
    led_fx::prepare(*mode, slot.state, num_led, effect_seed());

    {
        xewe::LockGuard lock(render_mutex);
        brightness.set_brightness(brightness_setting);
        if (os.nvs.read<bool>(id, "state", true)) brightness.turn_on();
        current       = std::move(slot);
        transitioning = false;
    }
    start_render_task();
}

// `$led reset` clears the whole `led` namespace: strip settings, mode_id and every mode parameter.
void Led::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    {
        xewe::LockGuard lock(render_mutex);
        fill_active   = false;
        brightness.turn_off();
        current       = Slot{};
        led_fx::default_params(*current.def, current.params);
        transitioning = false;
    }
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Led::status(const bool verbose) const {
    const LedChipset* stored = led_chipset_by_id(stored_chip_id);
    char              buf[560];

    // One snapshot under the render mutex: the render task reads these fields, and setters change them
    // with the mutex held. Cheap: a few copies plus the power sum.
    uint16_t               snap_num_led;
    uint8_t                snap_order, snap_brightness, snap_stored_brightness;
    bool                   snap_on, snap_fill, snap_fading;
    const led_fx::ModeDef* snap_mode;
    uint16_t               values[led_fx::MAX_PARAMS];
    uint32_t               mw = 0;
    {
        xewe::LockGuard lock(render_mutex);
        snap_num_led           = num_led;
        snap_order             = color_order;
        snap_brightness        = brightness_setting;
        snap_stored_brightness = brightness.get_brightness();
        snap_on                = brightness.get_state();
        snap_fill              = fill_active;
        snap_fading            = transitioning;
        snap_mode              = current.def;
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
        if (snap_on) mw = calculate_unscaled_power_mW(reinterpret_cast<const CRGB*>(out), shown_led);
    }
    const float power_w = mw / 1000.0f;
    char        brightness_note[32] = "";
    if (snap_brightness == 0 && snap_stored_brightness != 0) {   // `brightness 0` keeps the last non-zero value in NVS
        std::snprintf(brightness_note, sizeof(brightness_note), " (stored %u)",
                      static_cast<unsigned>(snap_stored_brightness));
    }
    const led_fx::Rgb color = led_fx::mode_color(*snap_mode, values);

    std::snprintf(buf, sizeof(buf),
        "\nChip:        %s (id %u)%s"
        "\nData pin:    GPIO %d"
        "\nClock pin:   GPIO %d%s"
        "\nLength:      %u (max %u)"
        "\nColor order: %s"
        "\nVoltage:     %u V"
        "\nBrightness:  %u/255%s"
        "\nState:       %s"
        "\nFPS:         %u"
        "\nPower:       %.2f W, %.2f A"
        "\nSource:      %s"
        "\nMode:        [%u] %s"
        "\nColor:       %02x%02x%02x"
        "\nTransition:  %s"
        "\nParams:",
        chipset ? chipset->name : "none", chipset ? static_cast<unsigned>(chipset->id) : 0u,
        (stored && chipset && stored->id != chipset->id) ? " (restart to apply stored chip)" : "",
        static_cast<int>(LED_PIN_DATA),
        static_cast<int>(LED_PIN_CLOCK), (chipset && chipset->clocked) ? "" : " (unused)",
        static_cast<unsigned>(snap_num_led), static_cast<unsigned>(LED_STRIP_NUM_LEDS_MAX),
        LED_COLOR_ORDERS[snap_order],
        static_cast<unsigned>(voltage),
        static_cast<unsigned>(snap_brightness), brightness_note,
        snap_on ? "on" : "off",
        static_cast<unsigned>(fps.load()),
        power_w, voltage ? power_w / voltage : 0.0f,
        snap_fill ? "fill" : "mode",
        static_cast<unsigned>(snap_mode->id), snap_mode->name,
        color.r, color.g, color.b,
        snap_fading ? "running" : "idle");

    std::string text = Module::status(false) + buf;
    char        line[160];
    for (uint8_t i = 0; i < snap_mode->param_count; ++i) {
        const led_fx::ParamDef& p = snap_mode->params[i];
        std::snprintf(line, sizeof(line), "\n    %s = %u [%u-%u] (%s)", p.key, static_cast<unsigned>(values[i]),
                      static_cast<unsigned>(p.min_value), static_cast<unsigned>(p.max_value), p.display);
        text += line;
    }
    if (verbose) os.serial.print(text);
    return text;
}

// =============================================================================
// Strip API
// =============================================================================
// 2.3.x semantics: 0 fades the strip to dark but keeps State on, and NVS keeps the last non-zero
// brightness, so a restart (or off -> on) comes back lit at that level instead of dark with State on.
void Led::set_brightness(uint8_t value, const void* origin) {
    uint8_t persisted;
    bool    changed;
    {
        xewe::LockGuard lock(render_mutex);
        changed            = brightness_setting != value;
        brightness.set_brightness(value);
        brightness_setting = value;
        persisted          = brightness.get_brightness();   // last non-zero (0 only if never set)
    }
    if (persisted != 0) os.nvs.write<uint8_t>(id, "brightness", persisted);
    if (changed) listeners.notify([&](LedListener& l) { l.on_brightness(value, origin); });
}

uint8_t Led::get_brightness() const {
    return brightness_setting;
}

void Led::set_state(bool on, const void* origin) {
    bool    was_on;
    uint8_t old_setting, new_setting;
    {
        xewe::LockGuard lock(render_mutex);
        was_on      = brightness.get_state();
        old_setting = brightness_setting;
        if (on) brightness.turn_on();
        else    brightness.turn_off();
        // off -> on fades to the last non-zero brightness (2.3.x), also after `brightness 0`
        if (on && !was_on) brightness_setting = brightness.get_brightness();
        new_setting = brightness_setting;
    }
    os.nvs.write<bool>(id, "state", on);
    if (on != was_on)              listeners.notify([&](LedListener& l) { l.on_state(on, origin); });
    if (new_setting != old_setting) listeners.notify([&](LedListener& l) { l.on_brightness(new_setting, origin); });
}

bool Led::get_state() const {
    return brightness.get_state();
}

bool Led::add_listener(LedListener* listener) {
    return listeners.add(listener);
}

bool Led::remove_listener(LedListener* listener) {
    return listeners.remove(listener);
}

bool Led::set_setting(const std::string& key, const std::string& value) {
    long number = 0;
    if (key == "chip") {
        const LedChipset* chip = parse_uint(value, 0, 255, number) ? led_chipset_by_id(number)
                                                                    : led_chipset_by_name(value.c_str());
        if (chip == nullptr) {
            std::string names;
            for (const LedChipset& c : LED_CHIPSETS) names += std::string(names.empty() ? "" : ", ") + c.name;
            os.serial.printf("Led: unknown chip '%s'; compiled in: %s", value.c_str(), names.c_str());
            return false;
        }
        stored_chip_id = chip->id;
        os.nvs.write<uint8_t>(id, "chip", chip->id);
        os.serial.printf("Led: chip set to %s; applies after restart ($system restart)", chip->name);
        return true;
    }
    if (key == "num_led" || key == "length") {
        if (!parse_uint(value, 1, LED_STRIP_NUM_LEDS_MAX, number)) {
            os.serial.printf("Led: num_led must be 1..%u", static_cast<unsigned>(LED_STRIP_NUM_LEDS_MAX));
            return false;
        }
        {
            xewe::LockGuard lock(render_mutex);
            num_led = static_cast<uint16_t>(number);
        }
        os.nvs.write<uint16_t>(id, "num_led", num_led);
        os.serial.printf("Led: num_led set to %u", static_cast<unsigned>(num_led));
        return true;
    }
    if (key == "colorder" || key == "color_order") {
        const int index = led_color_order_index(value.c_str());
        if (index < 0) {
            os.serial.print("Led: colorder must be one of RGB RBG GRB GBR BRG BGR");
            return false;
        }
        {
            xewe::LockGuard lock(render_mutex);
            color_order = static_cast<uint8_t>(index);
        }
        os.nvs.write<uint8_t>(id, "colorder", color_order);
        os.serial.printf("Led: colorder set to %s", LED_COLOR_ORDERS[color_order]);
        return true;
    }
    if (key == "voltage") {
        if (!parse_uint(value, 1, 48, number)) {
            os.serial.print("Led: voltage must be 1..48");
            return false;
        }
        voltage = static_cast<uint8_t>(number);
        os.nvs.write<uint8_t>(id, "voltage", voltage);
        os.serial.printf("Led: voltage set to %u V", static_cast<unsigned>(voltage));
        return true;
    }
    os.serial.printf("Led: unknown key '%s'; keys: chip, num_led, colorder, voltage", key.c_str());
    return false;
}

uint16_t Led::get_length() const     { return num_led; }
uint16_t Led::get_max_length() const { return LED_STRIP_NUM_LEDS_MAX; }
uint16_t Led::get_fps() const        { return fps.load(); }

void Led::fill(LedRgb color) {
    xewe::LockGuard lock(render_mutex);
    fill_color  = color;
    fill_active = true;
}

void Led::clear_fill() {
    xewe::LockGuard lock(render_mutex);
    fill_active = false;
}

uint32_t Led::get_frame_checksum() const {
    return frame_crc.load();
}

// =============================================================================
// Modes API
// =============================================================================
bool Led::set_mode(int mode_id, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    uint16_t values[led_fx::MAX_PARAMS];
    load_params(*mode, values);
    activate(*mode, values);
    os.nvs.write<uint8_t>(id, "mode_id", mode->id);
    const uint8_t  m     = mode->id;
    const uint32_t color = get_color();
    listeners.notify([&](LedListener& l) {
        l.on_mode(m, origin);
        l.on_color(color, origin);
    });
    return true;
}

bool Led::set_param(int mode_id, const std::string& param, int32_t value, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    const int index = led_fx::param_index(*mode, param.c_str());
    if (index < 0) return false;

    const bool is_current = mode->id == get_mode();
    uint16_t   values[led_fx::MAX_PARAMS];
    load_params(*mode, values);
    if (is_current) {
        xewe::LockGuard lock(render_mutex);
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
    }
    uint16_t old_values[led_fx::MAX_PARAMS];
    std::copy(values, values + led_fx::MAX_PARAMS, old_values);
    values[index] = led_fx::clamp_param(mode->params[index], value);
    persist_params(*mode, values);
    if (is_current) activate(*mode, values);
    notify_params(*mode, old_values, values, origin);
    return true;
}

bool Led::set_color(LedRgb color, const void* origin) {
    const led_fx::ModeDef* mode;
    uint16_t               values[led_fx::MAX_PARAMS];
    {
        xewe::LockGuard lock(render_mutex);
        mode = current.def;
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
    }
    const int hue = led_fx::param_index(*mode, "hue");
    const int sat = led_fx::param_index(*mode, "sat");
    if (hue < 0 && sat < 0) return false;

    const std::array<uint8_t, 3> hsv = xewe::color::rgb_to_hsv({color.r, color.g, color.b});
    uint16_t old_values[led_fx::MAX_PARAMS];
    std::copy(values, values + led_fx::MAX_PARAMS, old_values);
    if (hue >= 0) values[hue] = led_fx::clamp_param(mode->params[hue], hsv[0]);
    if (sat >= 0) values[sat] = led_fx::clamp_param(mode->params[sat], hsv[1]);
    persist_params(*mode, values);
    activate(*mode, values);
    notify_params(*mode, old_values, values, origin);
    return true;
}

bool Led::set_speed(int32_t value, const void* origin) {
    return set_param(get_mode(), "speed", value, origin);
}

// 2.3.x reset_current_mode: every parameter back to its table default in one pass: one NVS write per
// parameter, one cross-fade (only when `mode_id` is the current mode), on_param per changed value.
bool Led::reset_params(int mode_id, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    const bool is_current = mode->id == get_mode();
    uint16_t   old_values[led_fx::MAX_PARAMS];
    load_params(*mode, old_values);
    if (is_current) {
        xewe::LockGuard lock(render_mutex);
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, old_values);
    }
    uint16_t values[led_fx::MAX_PARAMS];
    led_fx::default_params(*mode, values);
    persist_params(*mode, values);
    if (is_current) activate(*mode, values);
    notify_params(*mode, old_values, values, origin);
    return true;
}

uint8_t Led::get_mode() const {
    xewe::LockGuard lock(render_mutex);
    return current.def->id;
}

uint16_t Led::get_param(int mode_id, const std::string& param) const {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return 0;
    const int index = led_fx::param_index(*mode, param.c_str());
    if (index < 0) return 0;
    uint16_t values[led_fx::MAX_PARAMS];
    load_params(*mode, values);
    return values[index];
}

uint32_t Led::get_color() const {
    const led_fx::ModeDef* mode;
    uint16_t               values[led_fx::MAX_PARAMS];
    {
        xewe::LockGuard lock(render_mutex);
        mode = current.def;
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
    }
    const led_fx::Rgb c = led_fx::mode_color(*mode, values);
    return led_pack_rgb(c.r, c.g, c.b);
}

// on_param per changed value; on_color when the change is on the current mode and moved its colour
void Led::notify_params(const led_fx::ModeDef& mode, const uint16_t* old_values, const uint16_t* new_values,
                        const void* origin) {
    const bool        is_current = mode.id == get_mode();
    const led_fx::Rgb before     = led_fx::mode_color(mode, old_values);
    const led_fx::Rgb after      = led_fx::mode_color(mode, new_values);
    const bool        recolored  = is_current && (before.r != after.r || before.g != after.g || before.b != after.b);
    listeners.notify([&](LedListener& l) {
        for (uint8_t i = 0; i < mode.param_count; ++i) {
            if (old_values[i] != new_values[i]) l.on_param(mode.id, mode.params[i].key, new_values[i], origin);
        }
        if (recolored) l.on_color(led_pack_rgb(after.r, after.g, after.b), origin);
    });
}

// =============================================================================
// Mode helpers
// =============================================================================
std::string Led::nvs_param_name(uint8_t mode_id, const char* param) const {
    return "m:" + std::to_string(mode_id) + ":" + param;   // <= 15 chars, checked by the unit test
}

void Led::load_params(const led_fx::ModeDef& mode, uint16_t* values) const {
    led_fx::default_params(mode, values);
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        const uint16_t stored = os.nvs.read<uint16_t>(id, nvs_param_name(mode.id, mode.params[i].key), values[i]);
        values[i] = led_fx::clamp_param(mode.params[i], stored);
    }
}

void Led::persist_params(const led_fx::ModeDef& mode, const uint16_t* values) {
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        os.nvs.write<uint16_t>(id, nvs_param_name(mode.id, mode.params[i].key), values[i]);
    }
}

void Led::activate(const led_fx::ModeDef& mode, const uint16_t* values) {
    Slot next;
    next.def = &mode;
    std::copy(values, values + led_fx::MAX_PARAMS, next.params);
    // allocate here (main loop), not in the render task: mode state and the two fade buffers
    const uint16_t n = get_length();
    led_fx::prepare(mode, next.state, n, effect_seed());
    {
        xewe::LockGuard lock(render_mutex);
        if (buffer_old.size() != n) buffer_old.resize(n);
        if (buffer_new.size() != n) buffer_new.resize(n);
        previous            = std::move(current);   // a fade in progress restarts from the newer mode
        current             = std::move(next);
        transitioning       = true;
        transition_start_ms = 0;                    // set by the next rendered frame
        fill_active         = false;                // a mode change ends `$led fill`
    }
}

const led_fx::ModeDef* Led::parse_mode(const std::string& text) const {
    long number = 0;
    if (parse_long(text, number)) return led_fx::find_mode(number);
    const std::string wanted = fold(text);
    for (const led_fx::ModeDef& m : led_fx::MODES) {
        if (fold(m.name) == wanted) return &m;
    }
    return nullptr;
}

// =============================================================================
// CLI
// =============================================================================
void Led::cli_brightness(xewe::span<const std::string> args) {
    long value = 0;
    if (!parse_uint(args[0], 0, 255, value)) {
        os.serial.print("Led: brightness must be 0..255");
        return;
    }
    set_brightness(static_cast<uint8_t>(value));
    os.serial.printf("Led: brightness %u", static_cast<unsigned>(value));
}

void Led::cli_set(xewe::span<const std::string> args) {
    set_setting(args[0], args[1]);
}

void Led::cli_fill(xewe::span<const std::string> args) {
    if (args[0] == "off") {
        clear_fill();
        os.serial.print("Led: fill off");
        return;
    }
    LedRgb color{};
    if (!parse_hex_color(args[0], color)) {
        os.serial.print("Led: fill needs a colour rrggbb (hex) or off");
        return;
    }
    fill(color);
    os.serial.printf("Led: fill %02x%02x%02x", color.r, color.g, color.b);
}

// $led mode <sub> [args]: registered for 1, 2 and 4 arguments (LM15)
void Led::cli_mode(xewe::span<const std::string> args) {
    const std::string sub = fold(args[0]);
    switch (args.size()) {
        case 1:
            if (sub == "list")        return mode_list();
            if (sub == "color") {
                os.serial.printf("Led: color %06lx", static_cast<unsigned long>(get_color()));
                return;
            }
            if (sub == "resetparams") return mode_reset_params(nullptr);
            break;
        case 2:
            if (sub == "set")         return mode_set(args[1]);
            if (sub == "color")       return mode_color(args[1]);
            if (sub == "resetparams") return mode_reset_params(&args[1]);
            if (sub == "speed")       return mode_speed(args[1]);
            break;
        case 4:
            if (sub == "param")       return mode_param(args[1], args[2], args[3]);
            break;
        default:
            break;
    }
    os.serial.print(MODE_USAGE);
}

void Led::mode_list() {
    const uint8_t active = get_mode();
    for (const led_fx::ModeDef& m : led_fx::MODES) {
        std::string line = std::string(m.id == active ? "* " : "  ") + "[" + std::to_string(m.id) + "] " + m.name + ":";
        for (uint8_t i = 0; i < m.param_count; ++i) line += std::string(" ") + m.params[i].key;
        os.serial.print(line);
    }
}

void Led::mode_set(const std::string& text) {
    const led_fx::ModeDef* mode = parse_mode(text);
    if (mode == nullptr || !set_mode(mode->id)) {
        os.serial.printf("Led: unknown mode '%s' (see $led mode list)", text.c_str());
        return;
    }
    os.serial.printf("Led: mode [%u] %s", static_cast<unsigned>(mode->id), mode->name);
}

void Led::mode_param(const std::string& text, const std::string& key, const std::string& value_text) {
    const led_fx::ModeDef* mode  = parse_mode(text);
    long                   value = 0;
    if (mode == nullptr) {
        os.serial.printf("Led: unknown mode '%s' (see $led mode list)", text.c_str());
        return;
    }
    if (!parse_long(value_text, value)) {
        os.serial.print("Led: value must be a number");
        return;
    }
    if (!set_param(mode->id, key, static_cast<int32_t>(value))) {
        os.serial.printf("Led: mode [%u] has no parameter '%s'", static_cast<unsigned>(mode->id), key.c_str());
        return;
    }
    os.serial.printf("Led: [%u] %s = %u", static_cast<unsigned>(mode->id), key.c_str(),
                     static_cast<unsigned>(get_param(mode->id, key)));
}

void Led::mode_color(const std::string& text) {
    LedRgb color{};
    if (!parse_hex_color(text, color)) {
        os.serial.print("Led: color needs rrggbb (hex)");
        return;
    }
    if (!set_color(color)) {
        os.serial.print("Led: the current mode has no colour");
        return;
    }
    os.serial.printf("Led: color %02x%02x%02x", color.r, color.g, color.b);
}

void Led::mode_reset_params(const std::string* text) {
    const led_fx::ModeDef* mode = text == nullptr ? led_fx::find_mode(get_mode()) : parse_mode(*text);
    if (mode == nullptr || !reset_params(mode->id)) {
        os.serial.printf("Led: unknown mode '%s' (see $led mode list)", text ? text->c_str() : "");
        return;
    }
    os.serial.printf("Led: [%u] %s parameters reset to defaults", static_cast<unsigned>(mode->id), mode->name);
}

void Led::mode_speed(const std::string& text) {
    long value = 0;
    if (!parse_long(text, value)) {
        os.serial.print("Led: speed must be a number");
        return;
    }
    if (!set_speed(static_cast<int32_t>(value))) {
        os.serial.print("Led: the current mode has no speed");
        return;
    }
    os.serial.printf("Led: speed %u", static_cast<unsigned>(get_param(get_mode(), "speed")));
}

// =============================================================================
// FastLED and the render task
// =============================================================================
bool Led::add_leds(uint8_t chip_id) {
    const LedChipset* chip = led_chipset_by_id(chip_id);
    if (chip == nullptr) return false;

    CRGB*           leds = reinterpret_cast<CRGB*>(out);
    CLEDController* c    = nullptr;
    switch (chip_id) {
        case 0:  c = &FastLED.addLeds<APA102, LED_PIN_DATA, LED_PIN_CLOCK, RGB>(leds, num_led); break;
        case 19: c = &FastLED.addLeds<SK6812, LED_PIN_DATA, RGB>(leds, num_led); break;
        case 38: c = &FastLED.addLeds<WS2811, LED_PIN_DATA, RGB>(leds, num_led); break;
        case 40: c = &FastLED.addLeds<WS2812, LED_PIN_DATA, RGB>(leds, num_led); break;
        case 41: c = &FastLED.addLeds<WS2812B, LED_PIN_DATA, RGB>(leds, num_led); break;
        default: return false;
    }
    c->setCorrection(TypicalLEDStrip);
    led_driver = c;
    chipset    = chip;
    shown_led  = num_led;
    return true;
}

void Led::start_render_task() {
    if (render_task_handle != nullptr || led_driver == nullptr) return;
    const BaseType_t core = config.render_task_core < portNUM_PROCESSORS ? config.render_task_core : 0;
    if (xTaskCreatePinnedToCore(&Led::render_task_entry, "led_render", config.render_task_stack_size,
                                this, config.render_task_priority, &render_task_handle, core) != pdPASS) {
        render_task_handle = nullptr;
        os.serial.print("Led: failed to start the render task");
    }
}

void Led::render_task_entry(void* self) {
    static_cast<Led*>(self)->render_task();
}

void Led::render_task() {
    const TickType_t period    = std::max<TickType_t>(1, pdMS_TO_TICKS(config.frame_delay_ms));
    TickType_t       last_wake = xTaskGetTickCount();
    fps_window_start_ms        = millis();

    for (;;) {
        {
            xewe::LockGuard lock(render_mutex);
            render_frame();
        }
        FastLED.show();   // outside the lock: setters never wait for the data transfer
        if (frame_len != shown_led) {
            // only this task calls show(), so the led_driver's length can change here
            static_cast<CLEDController*>(led_driver)->setLeds(reinterpret_cast<CRGB*>(out), frame_len);
            shown_led = frame_len;
        }
        count_frame();

        if (xTaskDelayUntil(&last_wake, period) == pdFALSE) {
            last_wake = xTaskGetTickCount();   // overran: resync, still yield a tick to loop()
            vTaskDelay(1);
        }
    }
}

void Led::render_frame() {
    const uint16_t n = num_led;

    if (fill_active) std::fill(frame, frame + n, fill_color);
    else             render_modes(frame, n, millis());
    frame_crc = led_frame_crc32(frame, n);

    // brightness and colour order into the output buffer
    const uint8_t  scale = brightness.get_frame_scale();
    const uint8_t* order = CHANNEL_MAP[color_order < 6 ? color_order : 0];
    for (uint16_t i = 0; i < n; ++i) {
        const uint8_t dimmed[3] = {
            static_cast<uint8_t>((static_cast<uint32_t>(frame[i].r) * scale) / 255),
            static_cast<uint8_t>((static_cast<uint32_t>(frame[i].g) * scale) / 255),
            static_cast<uint8_t>((static_cast<uint32_t>(frame[i].b) * scale) / 255),
        };
        out[i * 3 + 0] = dimmed[order[0]];
        out[i * 3 + 1] = dimmed[order[1]];
        out[i * 3 + 2] = dimmed[order[2]];
    }

    // shrinking: this frame still goes out over the old length with a black tail; the length is
    // switched after FastLED.show() in render_task()
    if (n < shown_led) std::fill(out + n * 3, out + shown_led * 3, uint8_t{0});
    frame_len = n;
}

// The mode state machine: the current mode, or during the 900 ms after a change the previous and the
// current mode blended by elapsed time. The fade buffers are sized by activate() on the main loop; a
// resize here only happens when the strip length changed since.
void Led::render_modes(LedRgb* dest, uint16_t count, uint32_t now_ms) {
    if (!transitioning) {
        led_fx::render(*current.def, current.params, current.state, dest, count, now_ms);
        return;
    }
    if (transition_start_ms == 0) transition_start_ms = now_ms ? now_ms : 1;   // first frame of the fade
    if (buffer_old.size() != count) buffer_old.resize(count);
    if (buffer_new.size() != count) buffer_new.resize(count);

    led_fx::render(*previous.def, previous.params, previous.state, buffer_old.data(), count, now_ms);
    led_fx::render(*current.def, current.params, current.state, buffer_new.data(), count, now_ms);
    const uint8_t progress = led_fx::transition_progress(now_ms - transition_start_ms);
    led_fx::crossfade(buffer_old.data(), buffer_new.data(), dest, count, progress);

    if (progress == 255) {
        transitioning = false;
        previous      = Slot{};
    }
}

void Led::count_frame() {
    fps_counter++;
    const uint32_t now_ms   = millis();
    const uint32_t window_s = config.fps_window_s ? config.fps_window_s : 1;
    if (now_ms - fps_window_start_ms < window_s * 1000) return;
    fps                 = static_cast<uint16_t>(fps_counter / window_s);
    fps_counter         = 0;
    fps_window_start_ms = now_ms;
}
