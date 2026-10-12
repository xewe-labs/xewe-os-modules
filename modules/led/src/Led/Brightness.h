// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Brightness.h
//
// Software dimming with a linear fade (xewe::AsyncTimer, progress recomputed at most every 5 ms).
// The last non-zero brightness is remembered for turn_on.
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <XeWeCore.h>

class Brightness {
public:
    Brightness(
        const uint16_t transition_delay, const uint8_t initial_brightness, const bool state_param);

    uint8_t get_start_value() const;
    uint8_t get_current_value() const;
    uint8_t get_target_value() const;
    // on: fade from the current value to new_brightness; off: no fade. A non-zero value becomes
    // the last brightness
    void set_brightness(const uint8_t new_brightness);
    void turn_on();
    void turn_off();
    uint8_t get_dimmed_color(const uint8_t color) const;
    std::array<uint8_t, 3> get_dimmed_color(const std::array<uint8_t, 3> color_rgb) const;
    bool get_state() const;
    uint8_t get_last_brightness() const;

private:
    std::unique_ptr<xewe::AsyncTimer<uint8_t>> timer;
    bool state;
    uint8_t last_brightness;
};
