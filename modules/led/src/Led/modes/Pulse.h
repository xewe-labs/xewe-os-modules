// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Pulse.h
//
// Pulse (id 4): one colour, brightness on a sine at `speed` beats per minute. 2.3.x: core float
// hsv_to_rgb, then nscale8(beatsin8). Uses std::sin, so its frames are host-specific in the last bit.
#pragma once

#include "Mode.h"

namespace led_fx {
namespace pulse {

inline constexpr ParamDef PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
    {"speed", "Speed", 1, 255, 30, 1, 'a'},
};

inline Rgb color(const uint16_t* p) {
    return hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
}

inline void render(const uint16_t* p, ModeState&, Rgb* buf, uint16_t n, uint32_t now_ms) {   // hue sat speed
    const Rgb     c = color(p);
    const uint8_t s = beat_sin8(p[2], now_ms);
    const Rgb     d = {scale8(c.r, s), scale8(c.g, s), scale8(c.b, s)};
    for (uint16_t i = 0; i < n; ++i) buf[i] = d;
}

}  // namespace pulse

inline constexpr ModeDef MODE_PULSE = {4, "Pulse", pulse::PARAMS, LED_FX_COUNT_OF(pulse::PARAMS), nullptr, pulse::color, pulse::render};

}  // namespace led_fx
