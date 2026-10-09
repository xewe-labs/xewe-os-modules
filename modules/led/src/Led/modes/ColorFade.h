// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/ColorFade.h
//
// Color Fade (id 1): hue and saturation drift around a base colour along 2D noise. 2.3.x: CHSV, so
// FastLED rainbow HSV (hsv_rainbow).
#pragma once

#include "Mode.h"

namespace led_fx {
namespace color_fade {

inline constexpr ParamDef PARAMS[] = {
    {"hue", "Hue", 0, 255, 195, 1, 'b'},
    {"sat", "Min Saturation", 0, 245, 245, 1, 'b'},
    {"speed", "Speed", 1, 50, 4, 1, 'a'},
    {"fire_step", "Density", 1, 255, 20, 1, 'a'},
    {"h_gap", "Color Variance", 0, 65535, 15000, 100, 'a'},
    {"min_bright", "Depth", 0, 255, 150, 1, 'a'},
};

inline Rgb color(const uint16_t* p) {   // 2.3.x: core hsv_to_rgb of the base colour
    return hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
}

// hue sat speed fire_step h_gap min_bright
inline void render(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t) {
    const int64_t base = static_cast<int64_t>(p[0]) * 256, gap = p[4];
    for (uint16_t i = 0; i < n; ++i) {
        const uint8_t v   = noise8(static_cast<uint32_t>(i) * p[3], st.counter);
        const int64_t h16 = base - gap / 2 + map_range(v, 0, 255, 0, gap);
        buf[i] = hsv_rainbow(static_cast<uint8_t>(static_cast<uint16_t>(h16) >> 8),
                             clamp8(map_range(v, 0, 255, 255, p[1])),
                             clamp8(map_range(v, 0, 255, p[5], 255)));
    }
    st.counter += p[2];
}

}  // namespace color_fade

inline constexpr ModeDef MODE_COLOR_FADE = {1, "Color Fade", color_fade::PARAMS, LED_FX_COUNT_OF(color_fade::PARAMS), nullptr, color_fade::color, color_fade::render};

}  // namespace led_fx
