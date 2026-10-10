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

// settings table defaults from the build defines (an unknown name falls back to WS2812B / GRB)
constexpr uint8_t default_chip_id() {
    const LedChipset* c = led_chipset_by_name(LED_CHIPSET);
    return c ? c->id : 41;
}
constexpr uint8_t default_color_order() {
    const int i = led_color_order_index(LED_COLOR_ORDER);
    return i < 0 ? 2 : static_cast<uint8_t>(i);
}

// "Color Fade Two Zone" -> "color_fade_two_zone" (schema group names; parse_mode accepts it)
std::string slug(const char* name) {
    std::string out;
    for (; *name; ++name) out += *name == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(*name)));
    return out;
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

// Christmas Lights flicker seed, new at every mode start.
// Never 0: led_fx::prepare() treats 0 as "keep the fixed default seed" (host tests).
uint32_t effect_seed() {
    return (esp_random() ^ millis()) | 1u;
}

// Source channel for each output slot, indexed like LED_COLOR_ORDERS.
constexpr uint8_t CHANNEL_MAP[6][3] = {
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
};

constexpr const char* MODE_USAGE =
    "! Led: usage: $led mode list | set <m> | param <m> <key> <value> | color [rrggbb] | reset_params [m] | speed <n>";

// Clockless chips on a run-time data pin: FastLED's RMT5 driver takes the pin as a constructor argument
// underneath its template, so one small driver class serves every GPIO. Elsewhere (RMT4, the I2S
// or SPI clockless drivers) the data pin stays the build define.
#if defined(FASTLED_RMT5) && FASTLED_RMT5 && !defined(FASTLED_ESP32_I2S) && !defined(FASTLED_ESP32_USE_CLOCKLESS_SPI)
#define LED_RUNTIME_PINS 1

// FastLED 3.10.3 platforms/esp/32/rmt_5/idf5_clockless_rmt_esp32.h ClocklessController, the same body
// with `pin` and the timings as constructor arguments (RGB order: Led applies the colour order itself)
class LedRmtController : public CPixelLEDController<RGB> {
public:
    LedRmtController(int pin, int t1, int t2, int t3) : rmt(pin, t1, t2, t3, fl::RmtController5::DMA_AUTO) {}
    void     init() override {}
    uint16_t getMaxRefreshRate() const override { return 800; }

protected:
    void showPixels(PixelController<RGB>& pixels) override {
        fl::PixelIterator iterator = pixels.as_iterator(this->getRgbw());
        rmt.loadPixelData(iterator);
    }
    void endShowLeds(void* data) override {
        CPixelLEDController<RGB>::endShowLeds(data);
        rmt.showPixels();
    }

private:
    fl::RmtController5 rmt;
};

struct LedTiming { int t1, t2, t3; };

// T1..T3 of a FastLED clockless chip, deduced from its ClocklessController base class
template <int P, int T1, int T2, int T3, EOrder O, int X, bool F, int W>
constexpr LedTiming clockless_timing(const ClocklessController<P, T1, T2, T3, O, X, F, W>*) {
    return {T1, T2, T3};
}
#else
#define LED_RUNTIME_PINS 0
#endif

// a GPIO FastLED can drive on this chip (output-capable, not flash/PSRAM/USB)
bool pin_usable(int pin) {
#if LED_RUNTIME_PINS && defined(_FL_VALID_PIN_MASK)
    return pin >= 0 && pin < 64 && ((uint64_t(_FL_VALID_PIN_MASK) >> pin) & 1u);
#else
    return pin == XEWE_MODULE_LED_PIN_DATA;
#endif
}

// a clockless chip on `pin`: FastLED's own driver on the build pin, LedRmtController elsewhere
template <template <uint8_t, EOrder> class CHIP>
CLEDController* add_clockless(int pin, CRGB* leds, int count) {
#if LED_RUNTIME_PINS
    if (pin != XEWE_MODULE_LED_PIN_DATA) {
        constexpr LedTiming t = clockless_timing(static_cast<const CHIP<XEWE_MODULE_LED_PIN_DATA, RGB>*>(nullptr));
        return &FastLED.addLeds(new LedRmtController(pin, t.t1, t.t2, t.t3), leds, count);   // once, at begin
    }
#endif
    (void)pin;
    return &FastLED.addLeds<CHIP, XEWE_MODULE_LED_PIN_DATA, RGB>(leds, count);
}

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
    frame        = new LedRgb[XEWE_MODULE_LED_NUM_LEDS_MAX]();
    out          = new uint8_t[XEWE_MODULE_LED_NUM_LEDS_MAX * 3]();
    render_mutex = xSemaphoreCreateMutex();
    led_fx::default_params(*current.def, current.params);

    // ---- strip ----
    register_command({"on", "Turn the strip on (fades in)", "$led on", 0,
                      [this](xewe::span<const std::string>) { cli_state(true); }});
    register_command({"off", "Turn the strip off (fades out)", "$led off", 0,
                      [this](xewe::span<const std::string>) { cli_state(false); }});
    register_command({"brightness", "Set brightness <0-255>", "$led brightness 128", 1,
                      [this](xewe::span<const std::string> args) { cli_brightness(args); }});
    register_command({"set", "Set <key> <value> (see $led schema); chip and colorder also by name", "$led set num_led 60", 2,
                      [this](xewe::span<const std::string> args) { cli_set(args); }});
    register_command({"fill", "Fill with a static colour <rrggbb>, or `off` to resume the mode", "$led fill ff0000", 1,
                      [this](xewe::span<const std::string> args) { cli_fill(args); }});
    register_command({"fill", "Fill with <rrggbb>, cross-faded over <ms> (0-60000)", "$led fill ff0000 500", 2,
                      [this](xewe::span<const std::string> args) { cli_fill(args); }});
    register_command({"checksum", "Print the CRC-32 of the current frame", "$led checksum", 0,
                      [this](xewe::span<const std::string>) {
                          os.serial.printf("Led: frame checksum: %08lx (%u leds)",
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

    // xewe-led-os command names, kept as aliases: same handlers as above
    register_command({"set_brightness", "2.3.x name of `brightness`", "$led set_brightness 128", 1,
                      [this](xewe::span<const std::string> args) { cli_brightness(args); }});
    register_command({"set_state", "2.3.x: set on/off state <0|1>", "$led set_state 0", 1,
                      [this](xewe::span<const std::string> args) { cli_state(args[0] != "0"); }});
    register_command({"toggle_state", "2.3.x: on -> off, off -> on", "$led toggle_state", 0,
                      [this](xewe::span<const std::string>) { cli_state(!get_state()); }});
    register_command({"turn_on", "2.3.x name of `on`", "$led turn_on", 0,
                      [this](xewe::span<const std::string>) { cli_state(true); }});
    register_command({"turn_off", "2.3.x name of `off`", "$led turn_off", 0,
                      [this](xewe::span<const std::string>) { cli_state(false); }});
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
    // ---- strip: the core loaded and range-checked the table rows before begin; what it cannot check ----
    const LedChipset* chip = led_chipset_by_id(stored_chip_id);
    if (chip == nullptr) {
        chip = led_chipset_by_id(default_chip_id());
        os.serial.printf("! Led: stored chip id %u is not compiled in, using %s",
                         static_cast<unsigned>(stored_chip_id), chip->name);
    }
    claim_pins(*chip);
    if (active_data_pin >= 0) add_leds(chip->id);
    FastLED.setBrightness(255);

    brightness_setting = brightness_saved;   // a stored 0 is outside [1, 255]: the core used 128

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
        if (state_saved) brightness.turn_on();
        current       = std::move(slot);
        transitioning = false;
    }
    start_render_task();
}

// `$led reset` clears the whole `led` namespace: strip settings, mode_id and every mode parameter
// (the core reloads the table defaults), and releases the pins.
void Led::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    {
        xewe::LockGuard lock(render_mutex);
        fill_on   = false;
        brightness.turn_off();
        current       = Slot{};
        led_fx::default_params(*current.def, current.params);
        transitioning = false;
    }
    release_pins();
    Module::reset(verbose, do_restart, keep_enabled);
}

// The plain strip settings: `$led set|get <key>`, `$led schema`, the status lines, loaded before begin.
// Keys and types match xewe-led-os (a device moved from it keeps its values); never rename or retype one.
xewe::Settings Led::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Led::stored_chip_id>  ("chip", 0, 45, default_chip_id(), "Chip id (2.3.x ids); by name: $led set chip WS2812B",
                                              xewe::SettingDef::RESTART),
        xewe::setting<&Led::num_led>         ("num_led", 1, XEWE_MODULE_LED_NUM_LEDS_MAX, LED_COUNT, "Strip length"),
        xewe::setting<&Led::color_order>     ("colorder", 0, 5, default_color_order(), "Colour order RGB=0 RBG GRB GBR BRG BGR=5"),
        xewe::setting<&Led::voltage>         ("voltage", 1, 48, LED_VOLTAGE, "Supply voltage, V (power estimate)"),
        xewe::setting<&Led::brightness_saved>("brightness", 1, 255, 128, "Brightness (last non-zero)"),
        xewe::setting<&Led::state_saved>     ("state", true, "On at boot"),
        xewe::setting<&Led::pin_data>        ("pin_data", 0, 48, XEWE_MODULE_LED_PIN_DATA, "Data GPIO", xewe::SettingDef::RESTART),
        xewe::setting<&Led::pin_clock>       ("pin_clock", 0, 48, XEWE_MODULE_LED_PIN_CLOCK, "Clock GPIO (APA102)", xewe::SettingDef::RESTART),
    };
    return {table, this};
}

// Mode parameters: one row per mode and parameter, changed with `$led mode param` (not `$led set`).
void Led::schema_extra(xewe::SchemaOut& out) const {
    for (const led_fx::ModeDef& m : led_fx::MODES) {
        uint16_t values[led_fx::MAX_PARAMS];
        live_params(m, values);
        for (uint8_t i = 0; i < m.param_count; ++i) {
            const led_fx::ParamDef& p = m.params[i];
            char row[256];
            std::snprintf(row, sizeof(row),
                          "\"key\":\"%s\",\"group\":\"mode:%s\",\"type\":\"u16\",\"min\":%u,\"max\":%u,\"default\":%u,"
                          "\"value\":%u,\"doc\":\"%s\",\"set\":\"$led mode param %u %s <v>\"",
                          p.key, slug(m.name).c_str(), static_cast<unsigned>(p.min_value), static_cast<unsigned>(p.max_value),
                          static_cast<unsigned>(p.default_value), static_cast<unsigned>(values[i]), p.display,
                          static_cast<unsigned>(m.id), p.key);
            out.row(row);
        }
    }
}

// After `$led set` / apply_setting saved a row (the core wrote the member): make the strip follow.
// num_led and colorder are read by the next frame; chip and the pins apply after a restart.
void Led::on_setting_changed(const xewe::SettingDef& def) {
    const std::string_view k = def.key;
    if (k == "brightness") {
        set_brightness(brightness_saved, nullptr, false);
    } else if (k == "state") {
        set_state(state_saved, nullptr, false);
    } else if (k == "pin_data" || k == "pin_clock") {
        const int         pin   = k == "pin_data" ? pin_data : pin_clock;
        const char* const owner = xewe::pins::owner_of(pin);
        if (owner != nullptr && id != owner) os.serial.printf("! Led: GPIO %d is claimed by %s", pin, owner);
    }
}

// Module::status(false) prints the table rows (`chip: 41`, `num_led: 60`, ...); these lines add what
// the table cannot: names, the pins and chip in use (stored ones apply after a restart), the live
// output (a transient off or `brightness 0`), fps, power, source and the mode.
std::string Led::status(const bool verbose) const {
    char buf[560];

    // One snapshot under the render mutex: the render task reads these fields, and setters change them
    // with the mutex held. Cheap: a few copies plus the power sum.
    uint8_t                snap_order, snap_brightness;
    bool                   snap_on, snap_fill, snap_fading;
    const led_fx::ModeDef* snap_mode;
    uint16_t               values[led_fx::MAX_PARAMS];
    uint32_t               mw = 0;
    {
        xewe::LockGuard lock(render_mutex);
        snap_order             = color_order;
        snap_brightness        = brightness_setting;
        snap_on                = brightness.get_state();
        snap_fill              = fill_on;
        snap_fading            = transitioning || fill_fade_ms != 0;
        snap_mode              = current.def;
        std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
        if (snap_on) mw = calculate_unscaled_power_mW(reinterpret_cast<const CRGB*>(out), shown_led);
    }
    const float       power_w = mw / 1000.0f;
    const led_fx::Rgb color   = led_fx::mode_color(*snap_mode, values);
    const bool        restart = (chipset && chipset->id != stored_chip_id) || active_data_pin != pin_data ||
                                (active_clock_pin >= 0 && active_clock_pin != pin_clock);

    std::snprintf(buf, sizeof(buf),
        "\nChip:        %s (id %u)"
        "\nPins:        data GPIO %d, clock GPIO %d%s"
        "\nColor order: %s"
        "\nOutput:      %s, %u/255"
        "\nFPS:         %u"
        "\nPower:       %.2f W, %.2f A"
        "\nSource:      %s"
        "\nMode:        [%u] %s"
        "\nColor:       %02x%02x%02x"
        "\nTransition:  %s"
        "\nParams:",
        chipset ? chipset->name : "none", chipset ? static_cast<unsigned>(chipset->id) : 0u,
        static_cast<int>(active_data_pin), static_cast<int>(active_clock_pin),
        restart ? " (restart to apply the stored chip/pins)" : "",
        LED_COLOR_ORDERS[snap_order < 6 ? snap_order : 0],
        snap_on ? "on" : "off", static_cast<unsigned>(snap_brightness),
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
// 0 fades the strip to dark but keeps State on, and NVS keeps the last non-zero
// brightness, so a restart (or off -> on) comes back lit at that level instead of dark with State on.
// The NVS key and type are the table row's (`brightness`, u8), so the next boot loads it.
void Led::set_brightness(uint8_t value, const void* origin, bool persist) {
    bool changed;
    {
        xewe::LockGuard lock(render_mutex);
        changed            = brightness_setting != value;
        brightness.set_brightness(value);
        brightness_setting = value;
    }
    if (persist && value != 0 && value != brightness_saved) {
        brightness_saved = value;
        os.nvs.write<uint8_t>(id, "brightness", value);
    }
    if (changed) listeners.notify([&](LedListener& l) { l.on_brightness(value, origin); });
}

uint8_t Led::get_brightness() const {
    return brightness_setting;
}

void Led::set_state(bool on, const void* origin, bool persist) {
    bool    was_on;
    uint8_t old_setting, new_setting;
    {
        xewe::LockGuard lock(render_mutex);
        was_on      = brightness.get_state();
        old_setting = brightness_setting;
        if (on) brightness.turn_on();
        else    brightness.turn_off();
        // off -> on fades to the last non-zero brightness, also after `brightness 0`
        if (on && !was_on) brightness_setting = brightness.get_brightness();
        new_setting = brightness_setting;
    }
    if (persist && on != state_saved) {   // the table row `state` (bool)
        state_saved = on;
        os.nvs.write<bool>(id, "state", on);
    }
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

// `$led set` and its aliases: names and alias keys translated, then the table path (validate,
// assign, persist, on_setting_changed), which prints `key=value` or the `!` error line.
bool Led::set_setting(const std::string& key, const std::string& value) {
    const std::string k = key == "length" ? "num_led" : key == "color_order" ? "colorder" : key;
    std::string       v = value;
    if (k == "chip") {
        const bool        digits = !v.empty() && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
        const LedChipset* chip   = digits ? led_chipset_by_id(std::atoi(v.c_str())) : led_chipset_by_name(v.c_str());
        if (chip == nullptr) {
            std::string names;
            for (const LedChipset& c : LED_CHIPSETS) names += std::string(names.empty() ? "" : ", ") + c.name;
            os.serial.printf("! Led: unknown chip '%s'; compiled in: %s", value.c_str(), names.c_str());
            return false;
        }
        v = std::to_string(chip->id);
    } else if (k == "colorder" && led_color_order_index(v.c_str()) >= 0) {
        v = std::to_string(led_color_order_index(v.c_str()));
    } else if (k == "pin_data" && v.size() < 4 && !pin_usable(std::atoi(v.c_str()))) {
        os.serial.printf("! Led: GPIO %s cannot drive the strip on this chip/driver", v.c_str());
        return false;
    }
    return apply_setting(k, v, true);
}

uint16_t Led::get_length() const     { return num_led; }
uint16_t Led::get_max_length() const { return XEWE_MODULE_LED_NUM_LEDS_MAX; }
uint16_t Led::get_fps() const        { return fps.load(); }

void Led::fill(LedRgb color, uint16_t fade_ms) {
    xewe::LockGuard lock(render_mutex);
    if (fade_ms != 0) buffer_old.assign(frame, frame + num_led);   // fade from what is shown now
    fill_rgb    = color;
    fill_on   = true;
    fill_fade_ms  = fade_ms;
    fill_start_ms = 0;                                             // set by the next rendered frame
}

void Led::clear_fill() {
    xewe::LockGuard lock(render_mutex);
    fill_on  = false;
    fill_fade_ms = 0;
}

bool Led::fill_active() const {
    xewe::LockGuard lock(render_mutex);
    return fill_on;
}

LedRgb Led::fill_color() const {
    xewe::LockGuard lock(render_mutex);
    return fill_rgb;
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

    uint16_t values[led_fx::MAX_PARAMS];
    const bool is_current = live_params(*mode, values);
    uint16_t old_values[led_fx::MAX_PARAMS];
    std::copy(values, values + led_fx::MAX_PARAMS, old_values);
    values[index] = led_fx::clamp_param(mode->params[index], value);
    persist_params(*mode, values);
    if (is_current) activate(*mode, values);
    notify_params(*mode, old_values, values, origin);
    return true;
}

bool Led::set_color(LedRgb color, const void* origin) {
    uint16_t values[led_fx::MAX_PARAMS];
    const led_fx::ModeDef* mode = current_mode(values);
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

// Every parameter back to its table default in one pass: one NVS write per
// parameter, one cross-fade (only when `mode_id` is the current mode), on_param per changed value.
bool Led::reset_params(int mode_id, const void* origin) {
    const led_fx::ModeDef* mode = led_fx::find_mode(mode_id);
    if (mode == nullptr) return false;
    uint16_t old_values[led_fx::MAX_PARAMS];
    const bool is_current = live_params(*mode, old_values);
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
    uint16_t values[led_fx::MAX_PARAMS];
    const led_fx::ModeDef* mode = current_mode(values);
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

const led_fx::ModeDef* Led::current_mode(uint16_t* values) const {
    xewe::LockGuard lock(render_mutex);
    std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
    return current.def;
}

bool Led::live_params(const led_fx::ModeDef& mode, uint16_t* values) const {
    load_params(mode, values);
    xewe::LockGuard lock(render_mutex);
    if (current.def->id != mode.id) return false;
    std::copy(current.params, current.params + led_fx::MAX_PARAMS, values);
    return true;
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
        fill_on         = false;                // a mode change ends `$led fill`
        fill_fade_ms        = 0;
    }
}

const led_fx::ModeDef* Led::parse_mode(const std::string& text) const {
    int number = 0;
    if (xewe::str::parse_int(text, number)) return led_fx::find_mode(number);
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
    uint8_t value = 0;
    if (!xewe::str::parse_int(args[0], value)) {
        os.serial.print("! Led: brightness must be 0..255");
        return;
    }
    set_brightness(value);
    os.serial.printf("Led: brightness %u", static_cast<unsigned>(value));
}

void Led::cli_state(bool on) {
    set_state(on);
    os.serial.print(on ? "Led: on" : "Led: off");
}

void Led::cli_set(xewe::span<const std::string> args) {
    set_setting(args[0], args[1]);
}

void Led::cli_fill(xewe::span<const std::string> args) {
    if (args.size() == 1 && args[0] == "off") {
        clear_fill();
        os.serial.print("Led: fill off");
        return;
    }
    LedRgb color{};
    int fade_ms = 0;
    if (!xewe::str::parse_hex_color(args[0], color.r, color.g, color.b)) {
        os.serial.print("! Led: fill needs a colour rrggbb (hex) or off");
        return;
    }
    if (args.size() == 2 && (!xewe::str::parse_int(args[1], fade_ms) || fade_ms < 0 || fade_ms > 60000)) {
        os.serial.print("! Led: fill fade must be 0..60000 ms");
        return;
    }
    fill(color, static_cast<uint16_t>(fade_ms));
    os.serial.printf("Led: fill %02x%02x%02x", color.r, color.g, color.b);
}

// $led mode <sub> [args]: registered for 1, 2 and 4 arguments
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
        os.serial.printf("! Led: unknown mode '%s' (see $led mode list)", text.c_str());
        return;
    }
    os.serial.printf("Led: mode [%u] %s", static_cast<unsigned>(mode->id), mode->name);
}

void Led::mode_param(const std::string& text, const std::string& key, const std::string& value_text) {
    const led_fx::ModeDef* mode = parse_mode(text);
    int value = 0;
    if (mode == nullptr) {
        os.serial.printf("! Led: unknown mode '%s' (see $led mode list)", text.c_str());
        return;
    }
    if (!xewe::str::parse_int(value_text, value)) {
        os.serial.print("! Led: value must be a number");
        return;
    }
    if (!set_param(mode->id, key, value)) {
        os.serial.printf("! Led: mode [%u] has no parameter '%s'", static_cast<unsigned>(mode->id), key.c_str());
        return;
    }
    os.serial.printf("Led: [%u] %s = %u", static_cast<unsigned>(mode->id), key.c_str(),
                     static_cast<unsigned>(get_param(mode->id, key)));
}

void Led::mode_color(const std::string& text) {
    LedRgb color{};
    if (!xewe::str::parse_hex_color(text, color.r, color.g, color.b)) {
        os.serial.print("! Led: color needs rrggbb (hex)");
        return;
    }
    if (!set_color(color)) {
        os.serial.print("! Led: the current mode has no colour");
        return;
    }
    os.serial.printf("Led: color %02x%02x%02x", color.r, color.g, color.b);
}

void Led::mode_reset_params(const std::string* text) {
    const led_fx::ModeDef* mode = text == nullptr ? led_fx::find_mode(get_mode()) : parse_mode(*text);
    if (mode == nullptr || !reset_params(mode->id)) {
        os.serial.printf("! Led: unknown mode '%s' (see $led mode list)", text ? text->c_str() : "");
        return;
    }
    os.serial.printf("Led: [%u] %s parameters reset to defaults", static_cast<unsigned>(mode->id), mode->name);
}

void Led::mode_speed(const std::string& text) {
    int value = 0;
    if (!xewe::str::parse_int(text, value)) {
        os.serial.print("! Led: speed must be a number");
        return;
    }
    if (!set_speed(value)) {
        os.serial.print("! Led: the current mode has no speed");
        return;
    }
    os.serial.printf("Led: speed %u", static_cast<unsigned>(get_param(get_mode(), "speed")));
}

// =============================================================================
// FastLED and the render task
// =============================================================================
// FastLED driver for the chip on the claimed pins (claim_pins first). Clocked chips take their pins as
// template arguments: the build defines (claim_pins already switched to them).
bool Led::add_leds(uint8_t chip_id) {
    const LedChipset* chip = led_chipset_by_id(chip_id);
    if (chip == nullptr) return false;

    CRGB*           leds = reinterpret_cast<CRGB*>(out);
    const int       pin  = active_data_pin;
    CLEDController* c    = nullptr;
    switch (chip_id) {
        case 0:  c = &FastLED.addLeds<APA102, XEWE_MODULE_LED_PIN_DATA, XEWE_MODULE_LED_PIN_CLOCK, RGB>(leds, num_led); break;
        case 19: c = add_clockless<SK6812>(pin, leds, num_led); break;
        case 38: c = add_clockless<WS2811>(pin, leds, num_led); break;
        case 40: c = add_clockless<WS2812>(pin, leds, num_led); break;
        case 41: c = add_clockless<WS2812B>(pin, leds, num_led); break;
        default: return false;
    }
    c->setCorrection(TypicalLEDStrip);
    led_driver = c;
    chipset    = chip;
    shown_led  = num_led;
    return true;
}

// Picks and claims (xewe::pins, owner `led`) the pins for `chip`: the stored pin_data/pin_clock, else
// the build pins. The core prints conflicts and the strapping-pin warning. No data pin: no driver.
void Led::claim_pins(const LedChipset& chip) {
    int data  = pin_data;
    int clock = chip.clocked ? pin_clock : -1;
    if (chip.clocked || !LED_RUNTIME_PINS) {
        if (data != XEWE_MODULE_LED_PIN_DATA || (chip.clocked && clock != XEWE_MODULE_LED_PIN_CLOCK)) {
            os.serial.printf("! Led: %s runs on the build pins (data GPIO %d, clock GPIO %d) in this firmware",
                             chip.name, XEWE_MODULE_LED_PIN_DATA, XEWE_MODULE_LED_PIN_CLOCK);
        }
        data  = XEWE_MODULE_LED_PIN_DATA;
        clock = chip.clocked ? XEWE_MODULE_LED_PIN_CLOCK : -1;
    } else if (!pin_usable(data)) {
        os.serial.printf("! Led: GPIO %d cannot drive the strip, using GPIO %d", data, XEWE_MODULE_LED_PIN_DATA);
        data = XEWE_MODULE_LED_PIN_DATA;
    }
    if (!xewe::pins::claim(data, id.c_str())) {
        if (data == XEWE_MODULE_LED_PIN_DATA || !xewe::pins::claim(XEWE_MODULE_LED_PIN_DATA, id.c_str())) {
            os.serial.print("! Led: no free data pin, the strip stays dark");
            return;
        }
        data = XEWE_MODULE_LED_PIN_DATA;
    }
    if (clock >= 0 && !xewe::pins::claim(clock, id.c_str())) {
        xewe::pins::release(data, id.c_str());
        os.serial.print("! Led: clock pin refused, the strip stays dark");
        return;
    }
    active_data_pin  = static_cast<int8_t>(data);
    active_clock_pin = static_cast<int8_t>(clock);
}

void Led::release_pins() {
    if (active_data_pin >= 0)  xewe::pins::release(active_data_pin, id.c_str());
    if (active_clock_pin >= 0) xewe::pins::release(active_clock_pin, id.c_str());
    active_data_pin  = -1;
    active_clock_pin = -1;
}

void Led::start_render_task() {
    if (render_task_handle != nullptr || led_driver == nullptr) return;
    const BaseType_t core = config.render_task_core < portNUM_PROCESSORS ? config.render_task_core : 0;
    if (xTaskCreatePinnedToCore(&Led::render_task_entry, "led_render", config.render_task_stack_size,
                                this, config.render_task_priority, &render_task_handle, core) != pdPASS) {
        render_task_handle = nullptr;
        os.serial.print("! Led: failed to start the render task");
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
    const uint16_t n      = num_led;
    const uint32_t now_ms = millis();

    if (fill_on) {
        std::fill(frame, frame + n, fill_rgb);
        if (fill_fade_ms != 0) {   // `fill(colour, fade_ms)`: from the frame shown at the call (buffer_old)
            if (fill_start_ms == 0) fill_start_ms = now_ms ? now_ms : 1;
            if (buffer_old.size() < n) buffer_old.resize(n, LedRgb{0, 0, 0});   // strip grown since
            const uint8_t progress = led_fx::transition_progress(now_ms - fill_start_ms, fill_fade_ms);
            led_fx::crossfade(buffer_old.data(), frame, frame, n, progress);
            if (progress == 255) fill_fade_ms = 0;
        }
    } else {
        render_modes(frame, n, now_ms);
    }
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
