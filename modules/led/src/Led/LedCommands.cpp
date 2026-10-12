// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedCommands.cpp

#include "LedCommands.h"

#include <climits>
#include <cstdlib>

int32_t led_arg_to_int(const std::string& text) {
    const long long v = std::strtoll(text.c_str(), nullptr, 10);
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return static_cast<int32_t>(v);
}

namespace {

using Args = xewe::span<const std::string>;

uint8_t u8(const std::string& s) {
    return static_cast<uint8_t>(led_arg_to_int(s));
}

int i32(const std::string& s) {
    return static_cast<int>(led_arg_to_int(s));
}

const std::array<uint8_t, 3> hsv_to_rgb(const std::array<uint8_t, 3>& hsv) {
    return xewe::color::hsv_to_rgb(hsv);
}

} // namespace

const LedCommand LED_COMMANDS[LED_COMMAND_COUNT] = {
    {"set_rgb", "Set RGB color", "$led set_rgb 255 0 0", 3,
        [](LedEngine& e, Args a) { e.sync_color({u8(a[0]), u8(a[1]), u8(a[2])}, nullptr); }},
    {"set_r", "Set red channel", "$led set_r 127", 1,
        [](LedEngine& e, Args a) { e.sync_color({u8(a[0]), e.get_g(), e.get_b()}, nullptr); }},
    {"set_g", "Set green channel", "$led set_g 255", 1,
        [](LedEngine& e, Args a) { e.sync_color({e.get_r(), u8(a[0]), e.get_b()}, nullptr); }},
    {"set_b", "Set blue channel", "$led set_b 200", 1,
        [](LedEngine& e, Args a) { e.sync_color({e.get_r(), e.get_g(), u8(a[0])}, nullptr); }},
    // adj_*: applied locally first, then announced (and applied a second time) with the result
    {"adj_rgb", "Adjust RGB color by deltas", "$led adj_rgb 10 -20 5", 3,
        [](LedEngine& e, Args a) {
            e.adj_rgb({i32(a[0]), i32(a[1]), i32(a[2])});
            e.sync_color(e.get_rgb(), nullptr);
        }},
    {"adj_r", "Adjust red channel by delta", "$led adj_r -10", 1,
        [](LedEngine& e, Args a) {
            e.adj_r(i32(a[0]));
            e.sync_color(e.get_rgb(), nullptr);
        }},
    {"adj_g", "Adjust green channel by delta", "$led adj_g 15", 1,
        [](LedEngine& e, Args a) {
            e.adj_g(i32(a[0]));
            e.sync_color(e.get_rgb(), nullptr);
        }},
    {"adj_b", "Adjust blue channel by delta", "$led adj_b -5", 1,
        [](LedEngine& e, Args a) {
            e.adj_b(i32(a[0]));
            e.sync_color(e.get_rgb(), nullptr);
        }},
    // V only scales the RGB that is converted back to hue/sat, so it is lost: V=0 gives black,
    // which converts to hue 0, sat 0, i.e. white. Kept so stored commands give the same colour.
    {"set_hsv", "Set HSV color", "$led set_hsv 75 255 0", 3,
        [](LedEngine& e, Args a) {
            std::array<uint8_t, 3> new_rgb = hsv_to_rgb({u8(a[0]), u8(a[1]), u8(a[2])});
            e.sync_color(new_rgb, nullptr);
        }},
    {"set_hue", "Set hue channel", "$led set_hue 255", 1,
        [](LedEngine& e, Args a) {
            std::array<uint8_t, 3> current_hsv = e.get_hsv();
            e.sync_color(hsv_to_rgb({u8(a[0]), current_hsv[1], current_hsv[2]}), nullptr);
        }},
    {"set_sat", "Set saturation channel", "$led set_sat 0", 1,
        [](LedEngine& e, Args a) {
            std::array<uint8_t, 3> current_hsv = e.get_hsv();
            e.sync_color(hsv_to_rgb({current_hsv[0], u8(a[0]), current_hsv[2]}), nullptr);
        }},
    {"set_val", "Set value channel", "$led set_val 255", 1,
        [](LedEngine& e, Args a) {
            std::array<uint8_t, 3> current_hsv = e.get_hsv();
            e.sync_color(hsv_to_rgb({current_hsv[0], current_hsv[1], u8(a[0])}), nullptr);
        }},
    {"adj_hsv", "Adjust HSV color by deltas", "$led adj_hsv 10 -20 5", 3,
        [](LedEngine& e, Args a) {
            e.adj_hsv({i32(a[0]), i32(a[1]), i32(a[2])});
            e.sync_color(hsv_to_rgb(e.get_hsv()), nullptr);
        }},
    // the sample usages name adj_h / adj_s / adj_v (not the command names); kept as published
    {"adj_hue", "Adjust hue channel by delta", "$led adj_h -10", 1,
        [](LedEngine& e, Args a) {
            e.adj_h(i32(a[0]));
            e.sync_color(hsv_to_rgb(e.get_hsv()), nullptr);
        }},
    {"adj_sat", "Adjust saturation channel by delta", "$led adj_s 15", 1,
        [](LedEngine& e, Args a) {
            e.adj_s(i32(a[0]));
            e.sync_color(hsv_to_rgb(e.get_hsv()), nullptr);
        }},
    {"adj_val", "Adjust value channel by delta", "$led adj_v -5", 1,
        [](LedEngine& e, Args a) {
            e.adj_v(i32(a[0]));
            e.sync_color(hsv_to_rgb(e.get_hsv()), nullptr);
        }},
    {"set_brightness", "Set global brightness", "$led set_brightness 255", 1,
        [](LedEngine& e, Args a) { e.sync_brightness(u8(a[0]), nullptr); }},
    {"adj_brightness", "Adjust global brightness by delta", "$led adj_brightness -10", 1,
        [](LedEngine& e, Args a) {
            e.adj_brightness(i32(a[0]));
            e.sync_brightness(e.get_brightness(), nullptr);
        }},
    {"set_state", "Set on/off state", "$led set_state 0", 1,
        [](LedEngine& e, Args a) { e.sync_state(led_arg_to_int(a[0]) != 0, nullptr); }},
    {"toggle_state", "If off->on, if on->off", "$led toggle_state", 0,
        [](LedEngine& e, Args) { e.sync_state(!e.get_state(), nullptr); }},
    {"turn_on", "Turn strip on", "$led turn_on", 0,
        [](LedEngine& e, Args) { e.sync_state(true, nullptr); }},
    {"turn_off", "Turn strip off", "$led turn_off", 0,
        [](LedEngine& e, Args) { e.sync_state(false, nullptr); }},
    {"set_mode", "Set LED strip mode", "$led set_mode 0", 1,
        [](LedEngine& e, Args a) { e.sync_mode(u8(a[0]), nullptr); }},
    {"adj_mode", "Adjust mode by delta (next/prev)", "$led adj_mode 1", 1,
        [](LedEngine& e, Args a) {
            e.adj_mode(i32(a[0]));
            e.sync_mode(e.get_current_mode_id(), nullptr);
        }},
    {"set_mode_param", "Set a parameter for the current mode", "$led set_mode_param hue 128", 2,
        [](LedEngine& e, Args a) {
            // an unknown key for the current mode changes nothing and prints nothing
            e.set_mode_param(a[0], static_cast<uint16_t>(led_arg_to_int(a[1])));
        }},
    {"adj_mode_param", "Adjust a parameter for the current mode by delta",
        "$led adj_mode_param hue -15", 2,
        [](LedEngine& e, Args a) { e.adj_mode_param(a[0], led_arg_to_int(a[1])); }},
    {"get_mode_params", "Get parameters of the current mode", "$led get_mode_params", 0,
        [](LedEngine& e, Args) { e.print_text(e.mode_params_text()); }},
    // no fan-out: listeners re-read the mode parameters themselves
    {"reset_current_mode", "Reset current mode parameters to defaults", "$led reset_current_mode",
        0, [](LedEngine& e, Args) { e.reset_current_mode(); }},
    {"set_length", "Set new number of LEDs", "$led set_length 500", 1,
        [](LedEngine& e, Args a) {
            e.sync_length(static_cast<uint16_t>(led_arg_to_int(a[0])), nullptr);
        }},
    // an empty name (`$led set_color_order ""`) starts the interactive calibration
    {"set_color_order", "Set LED color order (RGB/RBG/GRB/GBR/BRG/BGR)", "$led set_color_order GRB",
        1, [](LedEngine& e, Args a) { e.set_color_order(a[0]); }, false},
};

void led_run_command(LedEngine& engine, std::size_t index, Args args) {
    const LedCommand& command = LED_COMMANDS[index];
    if (!command.atomic) {
        command.run(engine, args);
        return;
    }
    LedEngine::Section section(engine);
    command.run(engine, args);
}
