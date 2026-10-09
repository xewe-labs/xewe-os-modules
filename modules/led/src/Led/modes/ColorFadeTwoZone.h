// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/ColorFadeTwoZone.h
//
// Color Fade Two Zone (id 2): noise between two hues, smoothed frame to frame (state.pixels).
// 2.3.x: its hand-written six-sector HSV (hsv).
#pragma once

#include "Mode.h"

namespace led_fx {
namespace color_fade_two_zone {

inline constexpr ParamDef PARAMS[] = {
    {"hue", "Hue A", 0, 255, 81, 1, 'b'},
    {"hue_b", "Hue B", 0, 255, 225, 1, 'b'},
    {"blend", "Blend", 2, 255, 150, 1, 'a'},
    {"speed", "Speed", 1, 50, 3, 1, 'a'},
    {"fire_step", "Density", 1, 255, 10, 1, 'a'},
    {"min_bright", "Depth", 0, 255, 245, 1, 'a'},
    {"min_sat", "Min Sat", 0, 255, 215, 1, 'a'},
};

inline void prepare(ModeState& st, uint16_t n) {   // the smoothed frame
    if (st.pixels.size() != n) st.pixels.assign(n, Rgb{0, 0, 0});
}

inline Rgb color(const uint16_t* p) {
    return hsv(static_cast<uint8_t>(p[0]), 255, 255);
}

// hue hue_b blend speed fire_step min_bright min_sat
inline void render(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t) {
    prepare(st, n);
    const uint8_t  amount  = static_cast<uint8_t>(p[2] ? p[2] : 1);
    const uint32_t spatial = static_cast<uint32_t>(p[4] ? p[4] : 1) * 400u;
    for (uint16_t i = 0; i < n; ++i) {
        const uint16_t v      = noise16(static_cast<uint32_t>(i) * spatial, st.counter);
        const Rgb      target = hsv(static_cast<uint8_t>(map_range(v, 0, 65535, p[0], p[1])),
                                    clamp8(map_range(v, 0, 65535, p[6], 255)),
                                    clamp8(map_range(v, 0, 65535, p[5], 255)));
        st.pixels[i] = blend(st.pixels[i], target, amount);
        buf[i]       = st.pixels[i];
    }
    st.counter += static_cast<uint32_t>(p[3] ? p[3] : 1) * 250u;
}

}  // namespace color_fade_two_zone

inline constexpr ModeDef MODE_COLOR_FADE_TWO_ZONE = {2, "Color Fade Two Zone", color_fade_two_zone::PARAMS, LED_FX_COUNT_OF(color_fade_two_zone::PARAMS), color_fade_two_zone::prepare, color_fade_two_zone::color, color_fade_two_zone::render};

}  // namespace led_fx
