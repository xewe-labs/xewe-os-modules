// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Registry.h
//
// The one list of compiled-in modes. Adding a mode: a new file in this folder (copy Solid.h, pick an
// unused id) plus its `#include` and one row in MODES[]. Removing a mode: delete both lines and the
// file; the other modes keep their ids (lookups are by id, never by position), and a stored `mode_id`
// that no longer exists falls back to the first row. tests/unit/test_led.py checks that every mode
// file is included and listed once, and that ids and names are unique. Standard library only.
#pragma once

#include "Mode.h"

#include "Solid.h"
#include "ColorFade.h"
#include "ColorFadeTwoZone.h"
#include "BrightnessFade.h"
#include "Pulse.h"
#include "Rainbow.h"
#include "ChristmasLights.h"

namespace led_fx {

// Order is the order of `$led mode list` and of the web page; the first row is the default mode.
inline constexpr ModeDef MODES[] = {
    MODE_SOLID,
    MODE_COLOR_FADE,
    MODE_COLOR_FADE_TWO_ZONE,
    MODE_BRIGHTNESS_FADE,
    MODE_PULSE,
    MODE_RAINBOW,
    MODE_CHRISTMAS_LIGHTS,
};
inline constexpr uint8_t MODE_COUNT = LED_FX_COUNT_OF(MODES);
static_assert(MODE_COUNT > 0, "at least one mode");

inline const ModeDef* find_mode(int id) {
    return find_mode_in(MODES, MODE_COUNT, id);
}

inline const ModeDef& default_mode() {
    return MODES[0];
}

}  // namespace led_fx
