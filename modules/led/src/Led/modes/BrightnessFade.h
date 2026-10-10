// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/BrightnessFade.h
//
// Brightness Fade (id 3): one colour, brightness along 2D noise. Colours: FastLED rainbow HSV (hsv_rainbow).
#pragma once

#include "Mode.h"

namespace led_fx {
namespace brightness_fade {

inline constexpr ParamDef PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
    {"speed", "Speed", 1, 50, 5, 1, 'a'},
    {"noise_step", "Density", 1, 255, 10, 1, 'a'},
    {"min_bright", "Min Brightness", 0, 255, 10, 1, 'a'},
};

inline Rgb color(const uint16_t* p) {   // core hsv_to_rgb of the base colour
    return hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
}

// hue sat speed noise_step min_bright
inline void render(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t) {
    for (uint16_t i = 0; i < n; ++i) {
        const uint8_t v = noise8(static_cast<uint32_t>(i) * p[3], st.counter);
        buf[i] = hsv_rainbow(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]),
                             clamp8(map_range(v, 0, 255, p[4], 255)));
    }
    st.counter += p[2];
}

}  // namespace brightness_fade

inline constexpr ModeDef MODE_BRIGHTNESS_FADE = {3, "Brightness Fade", brightness_fade::PARAMS, LED_FX_COUNT_OF(brightness_fade::PARAMS), nullptr, brightness_fade::color, brightness_fade::render};

}  // namespace led_fx
