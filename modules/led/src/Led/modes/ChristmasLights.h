// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/ChristmasLights.h
//
// Christmas Lights (id 6): a five-colour palette in groups of `density` pixels, each pixel flickering
// along its own noise offset (state.words, seeded per start).
#pragma once

#include "Mode.h"

namespace led_fx {
namespace christmas_lights {

inline constexpr ParamDef PARAMS[] = {
    {"density", "Density", 1, 10, 1, 1, 'b'},
    {"speed", "Flicker", 0, 20, 5, 1, 'a'},
};

inline void prepare(ModeState& st, uint16_t n) {   // per-pixel noise offsets from the state's generator
    if (st.words.size() == n) return;
    st.words.resize(n);
    for (uint16_t& o : st.words) o = next_random16(st.rng);
}

inline Rgb color(const uint16_t*) {   // fixed amber
    return {85, 49, 22};
}

inline void render(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t) {   // density speed
    static constexpr Rgb palette[5] = {
        {255, 6, 0}, {199, 61, 3}, {6, 133, 3}, {10, 10, 122}, {119, 130, 30},
    };
    prepare(st, n);
    const uint16_t density = p[0] ? p[0] : 1;
    for (uint16_t i = 0; i < n; ++i) {
        buf[i] = scale_video(palette[(i / density) % 5], noise8(st.words[i], st.counter));
    }
    st.counter += p[1];
}

}  // namespace christmas_lights

inline constexpr ModeDef MODE_CHRISTMAS_LIGHTS = {6, "Christmas Lights", christmas_lights::PARAMS, LED_FX_COUNT_OF(christmas_lights::PARAMS), christmas_lights::prepare, christmas_lights::color, christmas_lights::render};

}  // namespace led_fx
