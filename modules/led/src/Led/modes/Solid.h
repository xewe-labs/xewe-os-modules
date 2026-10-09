// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Solid.h
//
// Solid (id 0): one colour on every pixel. 2.3.x: core float hsv_to_rgb.
#pragma once

#include "Mode.h"

namespace led_fx {
namespace solid {

inline constexpr ParamDef PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
};

inline Rgb color(const uint16_t* p) {
    return hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
}

inline void render(const uint16_t* p, ModeState&, Rgb* buf, uint16_t n, uint32_t) {   // hue sat
    const Rgb c = color(p);
    for (uint16_t i = 0; i < n; ++i) buf[i] = c;
}

}  // namespace solid

inline constexpr ModeDef MODE_SOLID = {0, "Solid", solid::PARAMS, LED_FX_COUNT_OF(solid::PARAMS), nullptr, solid::color, solid::render};

}  // namespace led_fx
