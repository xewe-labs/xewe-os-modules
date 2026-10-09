// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Rainbow.h
//
// Rainbow (id 5): a moving rainbow. 2.3.x: fill_rainbow (hsv_rainbow).
#pragma once

#include "Mode.h"

namespace led_fx {
namespace rainbow {

inline constexpr ParamDef PARAMS[] = {
    {"speed", "Speed", 1, 20, 5, 1, 'b'},
    {"density", "Density", 1, 30, 10, 1, 'a'},
};

inline Rgb color(const uint16_t*) {   // no colour of its own: white (2.3.x)
    return {255, 255, 255};
}

inline void render(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t) {   // speed density
    const uint8_t start = static_cast<uint8_t>(st.counter);
    for (uint16_t i = 0; i < n; ++i) {
        buf[i] = hsv_rainbow(static_cast<uint8_t>(start + i * p[1]), 240, 255);
    }
    st.counter += p[0];
}

}  // namespace rainbow

inline constexpr ModeDef MODE_RAINBOW = {5, "Rainbow", rainbow::PARAMS, LED_FX_COUNT_OF(rainbow::PARAMS), nullptr, rainbow::color, rainbow::render};

}  // namespace led_fx
