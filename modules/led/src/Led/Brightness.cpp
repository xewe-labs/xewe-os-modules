// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Brightness.cpp

#include "Brightness.h"

LedBrightness::LedBrightness(uint16_t fade_ms)
    : timer(fade_ms, 0, 0) {
    timer.initiate();
}

void LedBrightness::set_brightness(uint8_t value) {
    if (state) {
        const uint8_t current = timer.get_current_value();
        timer.reset(current, value);
        timer.initiate();
    }
    if (value) last_brightness = value;
}

void LedBrightness::turn_on() {
    if (state) return;
    state = true;
    set_brightness(last_brightness);
}

void LedBrightness::turn_off() {
    if (!state) return;
    set_brightness(0);
    state = false;
}

uint8_t LedBrightness::get_brightness() const {
    return last_brightness;
}

bool LedBrightness::get_state() const {
    return state;
}

uint8_t LedBrightness::get_frame_scale() const {
    if (!state && timer.is_done()) return 0;
    return timer.get_current_value();
}
