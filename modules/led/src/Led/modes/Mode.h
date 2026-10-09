// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Mode.h
//
// What a mode is: a parameter table plus three functions over a per-instance state. Standard library
// only (host-tested by tests/unit/test_effects.cpp). One file per mode in this folder declares an
// `inline constexpr ModeDef MODE_<NAME>`; Registry.h lists them. See README.md "Adding a mode".
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../fx/Math.h"

namespace led_fx {

constexpr uint8_t  MAX_PARAMS    = 8;     // parameter slots per mode (uint16_t each)
constexpr uint16_t TRANSITION_MS = 900;   // cross-fade on every mode or parameter change

// key, display name, min, max, default, step, type ('b' basic, 'a' advanced). The mode files keep one
// ParamDef per line: tests/unit/test_led.py parses them.
struct ParamDef {
    const char* key;
    const char* display;
    uint16_t    min_value;
    uint16_t    max_value;
    uint16_t    default_value;
    uint16_t    step;
    char        type;
};

// Per-instance state of a running mode. Counters advance once per rendered frame (50 fps in the
// firmware), as in 2.3.x. The two per-pixel buffers are for modes that need them (Color Fade Two Zone
// keeps its smoothed frame in `pixels`, Christmas Lights its noise offsets in `words`); the mode's
// prepare() sizes them.
struct ModeState {
    uint32_t              counter = 0;
    uint32_t              rng     = 0x2545F491u;
    std::vector<Rgb>      pixels;
    std::vector<uint16_t> words;
};

// prepare: size the state's buffers for n pixels (nullptr: the mode needs none). Led calls it from the
//          main loop when a mode starts, so the render task does not allocate on a mode change;
//          render() calls it again and only allocates when the strip length changed.
// color:   the colour that stands for the mode (2.3.x Mode::get_rgb; status `Color:`, on_color).
// render:  one frame into buf[0..n) from the parameter values `p` (table order). Runs in the render
//          task with the render mutex held: no blocking, no NVS, no serial.
using PrepareFn = void (*)(ModeState& st, uint16_t n);
using ColorFn   = Rgb  (*)(const uint16_t* p);
using RenderFn  = void (*)(const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t now_ms);

struct ModeDef {
    uint8_t         id;            // stable: NVS keys m:<id>:<key>, `$led mode set <id>`; never reused
    const char*     name;
    const ParamDef* params;
    uint8_t         param_count;
    PrepareFn       prepare;
    ColorFn         color;
    RenderFn        render;
};

#define LED_FX_COUNT_OF(a) static_cast<uint8_t>(sizeof(a) / sizeof((a)[0]))

// ---- parameter rules ----------------------------------------------------------------------------------
inline int param_index(const ModeDef& mode, const char* key) {
    for (uint8_t i = 0; i < mode.param_count; ++i) {
        if (std::strcmp(mode.params[i].key, key) == 0) return i;
    }
    return -1;
}

// clamp into [min, max]; `hue` wraps around 0..255 instead (as in 2.3.x)
inline uint16_t clamp_param(const ParamDef& p, int32_t value) {
    if (std::strcmp(p.key, "hue") == 0) {
        int32_t wrapped = value % 256;
        if (wrapped < 0) wrapped += 256;
        return static_cast<uint16_t>(wrapped);
    }
    if (value < p.min_value) return p.min_value;
    if (value > p.max_value) return p.max_value;
    return static_cast<uint16_t>(value);
}

inline void default_params(const ModeDef& mode, uint16_t* out) {
    for (uint8_t i = 0; i < MAX_PARAMS; ++i) out[i] = i < mode.param_count ? mode.params[i].default_value : 0;
}

// ---- running a mode -----------------------------------------------------------------------------------
// `seed` != 0 reseeds the state's generator first and drops seeded buffers (Christmas Lights flicker
// offsets): the firmware passes esp_random() ^ millis() so the pattern differs per start (2.3.x
// random16()); 0 keeps the fixed default seed, which the unit tests rely on for pinned frames.
inline void prepare(const ModeDef& mode, ModeState& st, uint16_t n, uint32_t seed = 0) {
    if (seed != 0) {
        st.rng = seed;
        st.words.clear();
    }
    if (mode.prepare != nullptr) mode.prepare(st, n);
}

inline Rgb mode_color(const ModeDef& mode, const uint16_t* p) {
    return mode.color != nullptr ? mode.color(p) : Rgb{255, 255, 255};
}

inline void render(const ModeDef& mode, const uint16_t* p, ModeState& st, Rgb* buf, uint16_t n, uint32_t now_ms) {
    mode.render(p, st, buf, n, now_ms);
}

inline uint8_t transition_progress(uint32_t elapsed_ms, uint32_t duration_ms = TRANSITION_MS) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) return 255;
    return static_cast<uint8_t>(elapsed_ms * 255u / duration_ms);
}

// lookup by stable id in any list of modes (Registry.h: find_mode over MODES)
inline const ModeDef* find_mode_in(const ModeDef* list, std::size_t count, int id) {
    for (std::size_t i = 0; i < count; ++i) {
        if (list[i].id == id) return &list[i];
    }
    return nullptr;
}

}  // namespace led_fx
