// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedEngine.cpp

#include "LedEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {

// a float as `std::ostream << value` writes it with the default flags: "%g", 6 significant digits
std::string float_text(float value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    return text;
}

} // namespace

LedEngine::LedEngine(LedEngineIo& io) : io(io), self(this) {
    fill_solid(leds, XEWE_LED_NUM_LEDS_MAX, CRGB::Black);
}

void LedEngine::begin(uint16_t length, ModeParamStore& store, uint16_t brightness_transition_ms,
    uint16_t mode_transition_ms) {
    // The ModeController renders the stored length from the start (boot() repeats it). Built with
    // the buffer maximum instead, it would draw raw, undimmed mode output on the LEDs past num_led
    // until the first set_length.
    num_led = length;
    queued_store.inner = &store;
    brightness = std::make_unique<Brightness>(brightness_transition_ms, 0, 0);
    mode_controller =
        std::make_unique<ModeController>(leds, num_led, mode_transition_ms, queued_store);
}

bool LedEngine::begun() const {
    return mode_controller != nullptr;
}

void LedEngine::boot(uint8_t colorder, const std::vector<uint8_t>& rgb_blob, uint8_t new_brightness,
    uint8_t mode_id, bool state) {
    Section section(*this);
    // an out-of-range stored index falls back to RGB instead of indexing past the table
    color_order_index = colorder < LED_COLOR_ORDERS.size() ? colorder : 0;
    mode_controller->set_length(num_led); // see begin()

    // a missing or short `rgb` blob leaves mode 0 as built instead of reading past its end; the
    // stored colour is applied to mode 0 before the stored mode, so mode 0 takes the colour of
    // the mode that was active last
    if (rgb_blob.size() >= 3)
        set_rgb({rgb_blob[0], rgb_blob[1], rgb_blob[2]});
    set_brightness(new_brightness);
    set_mode(mode_id);
    set_state(state);
}

void LedEngine::set_self_origin(const void* origin) {
    self = origin;
}

const void* LedEngine::self_origin() const {
    return self;
}

// =============================================================================
// Atomic sections and queued effects
// =============================================================================
void LedEngine::enter_section() {
    io.lock();
    ++section_depth;
}

void LedEngine::leave_section() {
    std::vector<Effect> effects;
    if (--section_depth == 0)
        effects.swap(pending_effects);
    io.unlock();
    for (const Effect& effect : effects)
        run_effect(effect);
}

void LedEngine::defer(Effect effect) {
    if (section_depth > 0)
        pending_effects.push_back(std::move(effect));
    else
        run_effect(effect);
}

void LedEngine::run_effect(const Effect& f) {
    switch (f.kind) {
    case Effect::SAVE_U8:
        io.save_u8(f.name, static_cast<uint8_t>(f.value));
        break;
    case Effect::SAVE_U16:
        io.save_u16(f.name, f.value);
        break;
    case Effect::SAVE_BOOL:
        io.save_bool(f.name, f.state);
        break;
    case Effect::SAVE_RGB:
        io.save_rgb(f.rgb);
        break;
    case Effect::PRINT:
        io.print(f.text);
        break;
    case Effect::COMMIT:
        queued_store.inner->commit();
        break;
    case Effect::ON_COLOR:
        listeners.notify([&](LedListener& l) { l.on_color(f.rgb, f.origin); });
        break;
    case Effect::ON_BRIGHTNESS:
        listeners.notify(
            [&](LedListener& l) { l.on_brightness(static_cast<uint8_t>(f.value), f.origin); });
        break;
    case Effect::ON_STATE:
        listeners.notify([&](LedListener& l) { l.on_state(f.state, f.origin); });
        break;
    case Effect::ON_MODE:
        listeners.notify([&](LedListener& l) { l.on_mode(f.mode, f.origin); });
        break;
    case Effect::ON_LENGTH:
        listeners.notify([&](LedListener& l) { l.on_length(f.value, f.origin); });
        break;
    case Effect::ON_ALL:
        listeners.notify([&](LedListener& l) {
            l.on_all(f.rgb, static_cast<uint8_t>(f.value), f.state, f.mode, f.length, f.origin);
        });
        break;
    }
}

void LedEngine::save_u8(const char* name, uint8_t value) {
    Effect e{Effect::SAVE_U8};
    e.name = name;
    e.value = value;
    defer(std::move(e));
}

void LedEngine::save_u16(const char* name, uint16_t value) {
    Effect e{Effect::SAVE_U16};
    e.name = name;
    e.value = value;
    defer(std::move(e));
}

void LedEngine::save_bool(const char* name, bool value) {
    Effect e{Effect::SAVE_BOOL};
    e.name = name;
    e.state = value;
    defer(std::move(e));
}

void LedEngine::save_rgb(const std::array<uint8_t, 3>& rgb) {
    Effect e{Effect::SAVE_RGB};
    e.rgb = rgb;
    defer(std::move(e));
}

void LedEngine::print(std::string text) {
    Effect e{Effect::PRINT};
    e.text = std::move(text);
    defer(std::move(e));
}

uint16_t LedEngine::QueuedStore::read(
    uint8_t mode_id, std::string_view key, uint16_t default_value) {
    return inner->read(mode_id, key, default_value);
}

void LedEngine::QueuedStore::write(uint8_t mode_id, std::string_view key, uint16_t value) {
    inner->write(mode_id, key, value);
}

void LedEngine::QueuedStore::commit() {
    engine.defer(Effect{Effect::COMMIT});
}

// =============================================================================
// Fan-out: the strip applies first, then every listener is told
// =============================================================================
void LedEngine::sync_color(const std::array<uint8_t, 3> color, const void* origin) {
    Section section(*this);
    set_rgb(color);
    Effect e{Effect::ON_COLOR};
    e.rgb = color;
    e.origin = origin;
    defer(std::move(e));
}

void LedEngine::sync_brightness(const uint8_t value, const void* origin) {
    Section section(*this);
    set_brightness(value);
    Effect e{Effect::ON_BRIGHTNESS};
    e.value = value;
    e.origin = origin;
    defer(std::move(e));
}

void LedEngine::sync_state(const bool state, const void* origin) {
    Section section(*this);
    set_state(state);
    Effect e{Effect::ON_STATE};
    e.state = state;
    e.origin = origin;
    defer(std::move(e));
}

void LedEngine::sync_mode(const uint8_t mode, const void* origin) {
    Section section(*this);
    set_mode(mode);
    Effect e{Effect::ON_MODE};
    e.mode = mode;
    e.origin = origin;
    defer(std::move(e));
}

void LedEngine::sync_length(const uint16_t length, const void* origin) {
    Section section(*this);
    // a rejected length (above the maximum) is still announced to every listener
    set_length(length);
    Effect e{Effect::ON_LENGTH};
    e.value = length;
    e.origin = origin;
    defer(std::move(e));
}

void LedEngine::sync_all(const std::array<uint8_t, 3> color, const uint8_t value, const bool state,
    const uint8_t mode, const uint16_t length, const void* origin) {
    Section section(*this);
    // the strip's own order: state, length, mode, brightness, colour
    set_state(state);
    set_length(length);
    set_mode(mode);
    set_brightness(value);
    set_rgb(color);
    Effect e{Effect::ON_ALL};
    e.rgb = color;
    e.value = value;
    e.state = state;
    e.mode = mode;
    e.length = length;
    e.origin = origin;
    defer(std::move(e));
}

// =============================================================================
// Colour
// =============================================================================
void LedEngine::set_rgb(const std::array<uint8_t, 3> new_rgb) {
    Section section(*this);
    mode_controller->set_rgb(new_rgb);
    save_rgb(new_rgb);
}

void LedEngine::set_rgb(const uint8_t r, const uint8_t g, const uint8_t b) {
    set_rgb({r, g, b});
}

void LedEngine::set_hsv(const std::array<uint8_t, 3> new_hsv) {
    Section section(*this);
    mode_controller->set_hsv(new_hsv);
    save_rgb(get_rgb());
}

void LedEngine::set_h(const uint8_t h) {
    Section section(*this);
    std::array<uint8_t, 3> old_hsv = get_hsv();
    set_hsv({h, old_hsv[1], old_hsv[2]});
}

void LedEngine::adj_rgb(const std::array<int, 3> rgb_delta) {
    Section section(*this);
    std::array<uint8_t, 3> adjusted_rgb = get_rgb();

    for (int i = 0; i < 3; i++)
        adjusted_rgb[i] =
            static_cast<uint8_t>(std::clamp<int>(adjusted_rgb[i] + rgb_delta[i], 0, 255));

    set_rgb(adjusted_rgb);
}

void LedEngine::adj_r(const int r_delta) {
    adj_rgb({r_delta, 0, 0});
}

void LedEngine::adj_g(const int g_delta) {
    adj_rgb({0, g_delta, 0});
}

void LedEngine::adj_b(const int b_delta) {
    adj_rgb({0, 0, b_delta});
}

void LedEngine::adj_hsv(const std::array<int, 3> hsv_delta) {
    Section section(*this);
    std::array<uint8_t, 3> adjusted_hsv = get_hsv();

    for (int i = 0; i < 3; i++) {
        int adj_value = adjusted_hsv[i] + hsv_delta[i];

        if (i == 0) { // Hue wraps around 0-255
            while (adj_value < 0)
                adj_value += 256;
            adjusted_hsv[i] = static_cast<uint8_t>(adj_value % 256);
        } else { // Saturation and Value constrain 0-255
            adjusted_hsv[i] = static_cast<uint8_t>(std::clamp<int>(adj_value, 0, 255));
        }
    }

    set_hsv(adjusted_hsv);
}

void LedEngine::adj_h(const int h_delta) {
    adj_hsv({h_delta, 0, 0});
}

void LedEngine::adj_s(const int s_delta) {
    adj_hsv({0, s_delta, 0});
}

void LedEngine::adj_v(const int v_delta) {
    adj_hsv({0, 0, v_delta});
}

std::array<uint8_t, 3> LedEngine::get_rgb() const {
    return mode_controller->get_rgb();
}

uint8_t LedEngine::get_r() const {
    return get_rgb()[0];
}

uint8_t LedEngine::get_g() const {
    return get_rgb()[1];
}

uint8_t LedEngine::get_b() const {
    return get_rgb()[2];
}

std::array<uint8_t, 3> LedEngine::get_hsv() const {
    return xewe::color::rgb_to_hsv(get_rgb());
}

// =============================================================================
// Brightness
// =============================================================================
void LedEngine::set_brightness(const uint8_t new_brightness) {
    Section section(*this);
    brightness->set_brightness(new_brightness);
    save_u8("brightness", get_brightness());
}

void LedEngine::adj_brightness(const int brightness_delta) {
    Section section(*this);
    // from the fade target (0 while off)
    int new_brightness = brightness->get_target_value() + brightness_delta;
    set_brightness(static_cast<uint8_t>(std::clamp<int>(new_brightness, 0, 255)));
    save_u8("brightness", get_brightness());
}

uint8_t LedEngine::get_brightness() const {
    return brightness->get_last_brightness();
}

// =============================================================================
// State
// =============================================================================
void LedEngine::set_state(const bool state) {
    Section section(*this);
    if (state) {
        turn_on();
    } else {
        turn_off();
    }
    save_bool("state", get_state());
}

void LedEngine::toggle_state() {
    Section section(*this);
    if (get_state()) {
        turn_off();
    } else {
        turn_on();
    }
    save_bool("state", get_state());
}

void LedEngine::turn_on() {
    Section section(*this);
    brightness->turn_on();
    save_bool("state", get_state());
}

void LedEngine::turn_off() {
    Section section(*this);
    brightness->turn_off();
    save_bool("state", get_state());
}

bool LedEngine::get_state() const {
    return brightness->get_state();
}

// =============================================================================
// Mode
// =============================================================================
void LedEngine::set_mode(const uint8_t new_mode) {
    Section section(*this);
    mode_controller->set_mode(new_mode);
    // The new mode's colour is applied again through set_rgb and announced to every listener:
    // a second build of the mode, with hue/sat taken from an rgb -> hsv round trip, so they may
    // move by one step (Color Fade sat 245 -> 246). Kept as is so that frames, stored values and
    // events match what devices already in use do. The requested id is stored even when it is
    // unknown and mode 0 runs.
    sync_color(get_rgb(), self);
    save_u8("mode_id", new_mode);
}

void LedEngine::adj_mode(const int mode_delta) {
    Section section(*this);
    uint8_t new_mode =
        static_cast<uint8_t>(std::clamp<int>(get_current_mode_id() + mode_delta, 0, 255));
    set_mode(new_mode);
}

void LedEngine::set_mode_param(std::string_view key, const uint16_t value) {
    Section section(*this);
    if (mode_controller->set_mode_param(key, value)) {
        sync_color(get_rgb(), self);
    }
}

void LedEngine::adj_mode_param(std::string_view key, const int32_t value_delta) {
    Section section(*this);
    if (mode_controller->adj_mode_param(key, value_delta)) {
        sync_color(get_rgb(), self);
    }
}

uint8_t LedEngine::get_current_mode_id() const {
    return mode_controller->get_current_mode_id();
}

std::string_view LedEngine::get_current_mode_name() const {
    return mode_controller->get_current_mode_name();
}

uint16_t LedEngine::get_current_mode_param(std::string_view key) const {
    return mode_controller->get_current_mode_param(key);
}

std::vector<ModeParam> LedEngine::get_current_mode_params() const {
    return mode_controller->get_current_mode_params();
}

void LedEngine::reset_current_mode() {
    Section section(*this);
    mode_controller->reset_current_mode();
}

std::string LedEngine::get_all_modes_json() const {
    return mode_controller->get_all_modes_json();
}

uint16_t LedEngine::get_mode_transition_delay() const {
    return mode_controller->get_mode_transition_delay();
}

std::string LedEngine::mode_params_text() const {
    std::string text = "Mode " + std::to_string(get_current_mode_id()) + " (" +
        std::string(get_current_mode_name()) + ") Parameters:\n";

    for (const auto& param : mode_controller->get_current_mode_config().params) {
        text += "  - " + param.key + " (" + param.display_name +
            "): " + std::to_string(param.default_value) +
            " [Range: " + std::to_string(param.min_value) + "-" + std::to_string(param.max_value) +
            "]\n";
    }
    return text;
}

void LedEngine::print_text(std::string_view text) {
    print(std::string(text));
}

// =============================================================================
// Length
// =============================================================================
void LedEngine::set_length(const uint16_t length) {
    Section section(*this);
    if (length > XEWE_LED_NUM_LEDS_MAX) {
        print("That's too many. Max supported: " + std::to_string(XEWE_LED_NUM_LEDS_MAX) + " LEDs");
        return;
    }

    set_black();
    num_led = length;
    mode_controller->set_length(length);
    save_u16("num_led", length);
}

uint16_t LedEngine::get_length() const {
    return num_led;
}

// =============================================================================
// Colour order
// =============================================================================
bool LedEngine::set_color_order(std::string_view order) {
    if (order.empty()) {
        // the calibration waits for answers while the render task shows the test colours, so it
        // runs outside any section
        io.calibrate_color_order();
        return true;
    }

    Section section(*this);
    std::string value(order);

    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    const auto it = std::find(LED_COLOR_ORDERS.begin(), LED_COLOR_ORDERS.end(), value);
    if (it == LED_COLOR_ORDERS.end()) {
        print("Invalid color order. Use RGB, RBG, GRB, GBR, BRG, or BGR");
        return false;
    }

    const uint8_t index = static_cast<uint8_t>(it - LED_COLOR_ORDERS.begin());
    color_order_index = index;

    save_u8("colorder", index);
    print("Color order set to " + value);
    return true;
}

void LedEngine::set_color_order_index(uint8_t index) {
    if (index >= LED_COLOR_ORDERS.size())
        return;
    Section section(*this);
    color_order_index = index;
}

uint8_t LedEngine::get_color_order_index() const {
    return color_order_index;
}

// =============================================================================
// Render
// =============================================================================
void LedEngine::render_frame() {
    mode_controller->loop();
    set_all(leds);
}

bool LedEngine::take_blank_frame() {
    if (!blank_requested)
        return false;
    fill_solid(leds, blank_length, CRGB::Black);
    blank_requested = false;
    blank_length = 0;
    return true;
}

CRGB* LedEngine::pixels() {
    return leds;
}

const ModeController& LedEngine::get_mode_controller() const {
    return *mode_controller;
}

void LedEngine::set_pixel(uint16_t i, std::array<uint8_t, 3> color_rgb) {
    if (i < num_led) {
        // brightness is sampled per pixel (AsyncTimer recomputes at most every 5 ms)
        std::array<uint8_t, 3> dimmed_color = brightness->get_dimmed_color(color_rgb);

        switch (color_order_index) {
        case 0: // RGB
            leds[i] = CRGB(dimmed_color[0], dimmed_color[1], dimmed_color[2]);
            break;

        case 1: // RBG
            leds[i] = CRGB(dimmed_color[0], dimmed_color[2], dimmed_color[1]);
            break;

        case 2: // GRB
            leds[i] = CRGB(dimmed_color[1], dimmed_color[0], dimmed_color[2]);
            break;

        case 3: // GBR
            leds[i] = CRGB(dimmed_color[1], dimmed_color[2], dimmed_color[0]);
            break;

        case 4: // BRG
            leds[i] = CRGB(dimmed_color[2], dimmed_color[0], dimmed_color[1]);
            break;

        case 5: // BGR
            leds[i] = CRGB(dimmed_color[2], dimmed_color[1], dimmed_color[0]);
            break;
        }
    }
}

void LedEngine::set_all(CRGB* new_leds) {
    if (new_leds != nullptr) {
        for (uint16_t i = 0; i < num_led; i++) {
            set_pixel(i, {new_leds[i].r, new_leds[i].g, new_leds[i].b});
        }
    }
}

void LedEngine::set_black() {
    // only the render task writes the buffer: it blacks the current length (the longest one when
    // several changes come before a frame) and shows that once before its next mode frame
    blank_length = std::max(blank_length, num_led);
    blank_requested = true;
}

// =============================================================================
// Status
// =============================================================================
std::string LedEngine::status_text(const LedStatusInput& in) const {
    const bool use_clk = in.use_clk;
    const uint8_t configured_chip = in.chip;
    const uint8_t configured_v = in.voltage;
    const uint16_t configured_lines = in.lines;

    const float current_v = (configured_v > 0) ? static_cast<float>(configured_v) : 5.0f;
    const bool is_on = get_state();
    const uint16_t signal_length = get_length();

    auto get_chip_name = [](uint8_t chip_id) -> std::string {
        return (chip_id < LED_CHIPSET_COUNT) ? LED_CHIPSET_TABLE[chip_id].name : "Unknown";
    };

    // one pass per parallel line (one pass for a single strip)
    uint32_t total_physical_leds = 0;
    uint32_t total_power_mw = 0;
    const uint16_t loop_count = (configured_lines == 0) ? 1 : configured_lines;

    std::string hw_lines;
    std::string pwr_lines;

    for (uint16_t i = 0; i < loop_count; ++i) {
        uint16_t line_len = num_led; // Default for 0 configured lines

        if (configured_lines > 0) {
            line_len = i < in.line_lengths.size() ? in.line_lengths[i] : 0;
            hw_lines += "    Line " + std::to_string(i + 1) +
                " Length:    " + std::to_string(line_len) + "\n";
        } else {
            hw_lines += "    Length:           " + std::to_string(line_len) + "\n";
        }

        total_physical_leds += line_len;

        // FastLED's power model over the dimmed, colour-ordered buffer; every line measures the
        // same first pixels
        uint32_t line_power_mw = 0;
        if (is_on && line_len > 0) {
            const uint16_t powered_len = (line_len < signal_length) ? line_len : signal_length;
            io.lock();
            line_power_mw = calculate_unscaled_power_mW(leds, powered_len);
            io.unlock();
        }
        total_power_mw += line_power_mw;

        const float power_w = line_power_mw / 1000.0f;
        const float current_a = power_w / current_v; // current_v is safely > 0

        pwr_lines += "    Line " + (configured_lines > 0 ? std::to_string(i + 1) : "1") +
            ":           " + float_text(power_w) + " W, " + float_text(current_a) + " A\n";
    }

    std::string text = "Hardware:\n";
    text += "    Chip:             " + get_chip_name(configured_chip) + "\n";
    text += "    Data Pin:         GPIO_" + std::to_string(in.pin_data) + "\n";

    if (use_clk) {
        text += "    Clock Pin:        GPIO_" + std::to_string(in.pin_clock) + "\n";
    }

    text += "    Voltage:          " + std::to_string(configured_v) + " V\n";
    text += "    Color Order:      ";
    text += LED_COLOR_ORDERS[color_order_index];
    text += "\n";

    if (configured_lines == 0) {
        text +=
            hw_lines + "    Max Length:       " + std::to_string(XEWE_LED_NUM_LEDS_MAX) + "\n\n";
    } else {
        text += "    Parallel Lines:   " + std::to_string(configured_lines) + "\n" + hw_lines +
            "    Total LEDs:       " + std::to_string(total_physical_leds) + "\n" +
            "    Max LEDs/line:    " + std::to_string(XEWE_LED_NUM_LEDS_MAX) + "\n\n";
    }

    text += "Live State:\n";
    text += "    FPS:              " + std::to_string(in.fps) + "\n";
    text += "    Brightness:       " + std::to_string(get_brightness()) + "/255\n";
    text += std::string("    Power State:      ") + (is_on ? "ON" : "OFF") + "\n";
    text += "    Color (RGB):      (" + std::to_string(get_r()) + ", " + std::to_string(get_g()) +
        ", " + std::to_string(get_b()) + ")\n\n";

    const float total_power_w = total_power_mw / 1000.0f;
    text += "Power:\n";
    text += "    Voltage:          " + float_text(current_v) + " V\n";
    text += pwr_lines + "    Total:            " + float_text(total_power_w) + " W, " +
        float_text(total_power_w / current_v) + " A\n\n";
    text += "Mode: [" + std::to_string(get_current_mode_id()) + "] ";
    text += get_current_mode_name();
    text += "\n";

    const auto& params = mode_controller->get_current_mode_config().params;
    if (params.empty()) {
        text += "    (No parameters for this mode)\n";
    } else {
        for (const auto& param : params) {
            text += "    - " + param.key + ": " +
                std::to_string(mode_controller->get_current_mode_param(param.key)) + " [" +
                std::to_string(param.min_value) + "-" + std::to_string(param.max_value) + "] (" +
                param.display_name + ")\n";
        }
    }

    return text;
}
