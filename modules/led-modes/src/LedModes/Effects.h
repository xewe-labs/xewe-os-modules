// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-modes/src/LedModes/Effects.h
//
// The effect catalogue of xewe-led-os 2.3.x as pure functions over an RGB buffer. Host-compilable on
// purpose: standard library only, no Arduino, no FastLED, no XeWeCore (tests/host/test_effects.cpp
// builds it with g++). Colour maths is a small local re-implementation of the 2.3.x conversions: FastLED's
// rainbow HSV (bit-exact, see hsv_rainbow), core's float HSV (same code as XeWeCore Utils/Color.h) and the
// six-sector HSV of Color Fade Two Zone. Frames are still close to, not bit-identical with, 2.3.x because
// the noise is value noise, not FastLED's Perlin inoise8/16.
//
// The parameter tables below are parsed by tests/test_led-modes.py: keep one ParamDef per line.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace led_fx {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
static_assert(sizeof(Rgb) == 3, "Rgb must be 3 packed bytes");

// key, display name, min, max, default, step, type ('b' basic, 'a' advanced)
struct ParamDef {
    const char* key;
    const char* display;
    uint16_t    min_value;
    uint16_t    max_value;
    uint16_t    default_value;
    uint16_t    step;
    char        type;
};

struct ModeDef {
    uint8_t         id;
    const char*     name;
    const ParamDef* params;
    uint8_t         param_count;
};

constexpr uint8_t  MAX_PARAMS    = 8;
constexpr uint16_t TRANSITION_MS = 900;

// ---- parameter tables (ids, keys, ranges and defaults as in 2.3.x) ---------------------------------
constexpr ParamDef SOLID_PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
};
constexpr ParamDef FADE_COLOR_PARAMS[] = {
    {"hue", "Hue", 0, 255, 195, 1, 'b'},
    {"sat", "Min Saturation", 0, 245, 245, 1, 'b'},
    {"speed", "Speed", 1, 50, 4, 1, 'a'},
    {"fire_step", "Density", 1, 255, 20, 1, 'a'},
    {"h_gap", "Color Variance", 0, 65535, 15000, 100, 'a'},
    {"min_bright", "Depth", 0, 255, 150, 1, 'a'},
};
constexpr ParamDef TWO_ZONE_PARAMS[] = {
    {"hue", "Hue A", 0, 255, 81, 1, 'b'},
    {"hue_b", "Hue B", 0, 255, 225, 1, 'b'},
    {"blend", "Blend", 2, 255, 150, 1, 'a'},
    {"speed", "Speed", 1, 50, 3, 1, 'a'},
    {"fire_step", "Density", 1, 255, 10, 1, 'a'},
    {"min_bright", "Depth", 0, 255, 245, 1, 'a'},
    {"min_sat", "Min Sat", 0, 255, 215, 1, 'a'},
};
constexpr ParamDef FADE_BRIGHTNESS_PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
    {"speed", "Speed", 1, 50, 5, 1, 'a'},
    {"noise_step", "Density", 1, 255, 10, 1, 'a'},
    {"min_bright", "Min Brightness", 0, 255, 10, 1, 'a'},
};
constexpr ParamDef PULSE_PARAMS[] = {
    {"hue", "Hue", 0, 255, 0, 1, 'b'},
    {"sat", "Saturation", 0, 255, 255, 1, 'b'},
    {"speed", "Speed", 1, 255, 30, 1, 'a'},
};
constexpr ParamDef RAINBOW_PARAMS[] = {
    {"speed", "Speed", 1, 20, 5, 1, 'b'},
    {"density", "Density", 1, 30, 10, 1, 'a'},
};
constexpr ParamDef CHRISTMAS_PARAMS[] = {
    {"density", "Density", 1, 10, 1, 1, 'b'},
    {"speed", "Flicker", 0, 20, 5, 1, 'a'},
};

#define LED_FX_COUNT_OF(a) static_cast<uint8_t>(sizeof(a) / sizeof((a)[0]))
constexpr ModeDef MODES[] = {
    {0, "Solid", SOLID_PARAMS, LED_FX_COUNT_OF(SOLID_PARAMS)},
    {1, "Color Fade", FADE_COLOR_PARAMS, LED_FX_COUNT_OF(FADE_COLOR_PARAMS)},
    {2, "Color Fade Two Zone", TWO_ZONE_PARAMS, LED_FX_COUNT_OF(TWO_ZONE_PARAMS)},
    {3, "Brightness Fade", FADE_BRIGHTNESS_PARAMS, LED_FX_COUNT_OF(FADE_BRIGHTNESS_PARAMS)},
    {4, "Pulse", PULSE_PARAMS, LED_FX_COUNT_OF(PULSE_PARAMS)},
    {5, "Rainbow", RAINBOW_PARAMS, LED_FX_COUNT_OF(RAINBOW_PARAMS)},
    {6, "Christmas Lights", CHRISTMAS_PARAMS, LED_FX_COUNT_OF(CHRISTMAS_PARAMS)},
};
constexpr uint8_t MODE_COUNT = LED_FX_COUNT_OF(MODES);
#undef LED_FX_COUNT_OF

enum ModeId : uint8_t {
    SOLID = 0, FADE_COLOR = 1, TWO_ZONE = 2, FADE_BRIGHTNESS = 3, PULSE = 4, RAINBOW = 5, CHRISTMAS = 6,
};

// ---- lookups and parameter rules --------------------------------------------------------------------
inline const ModeDef* find_mode(int id) {
    return (id >= 0 && id < MODE_COUNT) ? &MODES[id] : nullptr;
}

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

// ---- colour maths -----------------------------------------------------------------------------------
inline uint8_t scale8(uint8_t value, uint8_t scale) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * (static_cast<uint16_t>(scale) + 1)) >> 8);
}

// linear map of v from [in_lo, in_hi] to [out_lo, out_hi] (Arduino map() semantics, 64-bit safe)
inline int32_t map_range(int64_t v, int64_t in_lo, int64_t in_hi, int64_t out_lo, int64_t out_hi) {
    if (in_hi == in_lo) return static_cast<int32_t>(out_lo);
    return static_cast<int32_t>((v - in_lo) * (out_hi - out_lo) / (in_hi - in_lo) + out_lo);
}

inline uint8_t clamp8(int32_t v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// 8-bit HSV to RGB, six linear sectors (the hand-written ColorHSV of 2.3.x's Color Fade Two Zone; v2 uses
// it for Two Zone only)
inline Rgb hsv(uint8_t hue8, uint8_t sat, uint8_t val) {
    uint8_t        r, g, b;
    const uint16_t h = static_cast<uint16_t>(hue8) * 6;
    if (h < 510) {
        b = 0;
        if (h < 255) { r = 255; g = static_cast<uint8_t>(h); }
        else         { r = static_cast<uint8_t>(510 - h); g = 255; }
    } else if (h < 1020) {
        r = 0;
        if (h < 765) { g = 255; b = static_cast<uint8_t>(h - 510); }
        else         { g = static_cast<uint8_t>(1020 - h); b = 255; }
    } else if (h < 1530) {
        g = 0;
        if (h < 1275) { r = static_cast<uint8_t>(h - 1020); b = 255; }
        else          { r = 255; b = static_cast<uint8_t>(1530 - h); }
    } else {
        r = 255; g = 0; b = 0;
    }
    const uint32_t v1 = 1u + val;
    const uint32_t s1 = 1u + sat;
    const uint32_t s2 = 255u - sat;
    r = static_cast<uint8_t>(((((r * s1) >> 8) + s2) * v1) >> 8);
    g = static_cast<uint8_t>(((((g * s1) >> 8) + s2) * v1) >> 8);
    b = static_cast<uint8_t>(((((b * s1) >> 8) + s2) * v1) >> 8);
    return {r, g, b};
}

// FastLED's "rainbow" HSV to RGB (CHSV -> CRGB, fill_rainbow), used by 2.3.x Color Fade, Brightness Fade
// and Rainbow. Re-implemented from FastLED 3.10.3 src/hsv2rgb.cpp hsv2rgb_rainbow() (MIT licence,
// https://github.com/FastLED/FastLED) with its defaults Y1 = 1, Y2 = 0, G2 = 0, Gscale = 0 and
// FASTLED_SCALE8_FIXED = 1; no FastLED include so this header stays host-buildable. Verified bit-exact
// against the real function for all 256 x 256 x 256 inputs (wip/xewe-led-os-v2 MIGRATION-REPORT, LX1).
inline uint8_t scale8_video_fl(uint8_t i, uint8_t scale) {   // FastLED scale8_video
    return static_cast<uint8_t>(((static_cast<int>(i) * scale) >> 8) + ((i && scale) ? 1 : 0));
}
inline Rgb hsv_rainbow(uint8_t hue, uint8_t sat, uint8_t val) {
    const uint8_t offset8 = static_cast<uint8_t>((hue & 0x1F) << 3);
    const uint8_t third   = scale8(offset8, 256 / 3);           // max 85
    const uint8_t twothirds = scale8(offset8, (256 * 2) / 3);   // max 170
    uint8_t r, g, b;
    switch (hue >> 5) {
        case 0:  r = static_cast<uint8_t>(255 - third);    g = third;                              b = 0; break;  // R -> O
        case 1:  r = 171;                                  g = static_cast<uint8_t>(85 + third);   b = 0; break;  // O -> Y
        case 2:  r = static_cast<uint8_t>(171 - twothirds); g = static_cast<uint8_t>(170 + third); b = 0; break;  // Y -> G
        case 3:  r = 0; g = static_cast<uint8_t>(255 - third);     b = third;                              break;  // G -> A
        case 4:  r = 0; g = static_cast<uint8_t>(171 - twothirds); b = static_cast<uint8_t>(85 + twothirds); break; // A -> B
        case 5:  r = third;                                g = 0; b = static_cast<uint8_t>(255 - third); break;   // B -> P
        case 6:  r = static_cast<uint8_t>(85 + third);     g = 0; b = static_cast<uint8_t>(171 - third); break;   // P -> K
        default: r = static_cast<uint8_t>(170 + third);    g = 0; b = static_cast<uint8_t>(85 - third);  break;   // K -> R
    }
    if (sat != 255) {
        if (sat == 0) {
            r = 255; g = 255; b = 255;
        } else {
            const uint8_t desat    = scale8_video_fl(static_cast<uint8_t>(255 - sat), static_cast<uint8_t>(255 - sat));
            const uint8_t satscale = static_cast<uint8_t>(255 - desat);
            r = static_cast<uint8_t>(scale8(r, satscale) + desat);
            g = static_cast<uint8_t>(scale8(g, satscale) + desat);
            b = static_cast<uint8_t>(scale8(b, satscale) + desat);
        }
    }
    if (val != 255) {
        val = scale8_video_fl(val, val);
        if (val == 0) {
            r = 0; g = 0; b = 0;
        } else {
            r = scale8(r, val);
            g = scale8(g, val);
            b = scale8(b, val);
        }
    }
    return {r, g, b};
}

// Core's float HSV to RGB, the same code as XeWeCore Utils/Color.h xewe::color::hsv_to_rgb (2.3.x Solid,
// Pulse and every mode's base colour). Copied rather than included so the header stays host-buildable;
// tests/host checks it against the core function for all 65,536 hue x sat inputs at every value tested.
inline Rgb hsv_spectrum(uint8_t hue, uint8_t sat, uint8_t val) {
    const float h_f = hue / 255.0f;
    const float s_f = sat / 255.0f;
    const float v_f = val / 255.0f;
    float       r_f = 0.0f, g_f = 0.0f, b_f = 0.0f;
    const int   i = static_cast<int>(h_f * 6.0f);
    const float f = h_f * 6.0f - i;
    const float p = v_f * (1.0f - s_f);
    const float q = v_f * (1.0f - f * s_f);
    const float t = v_f * (1.0f - (1.0f - f) * s_f);
    switch (i % 6) {
        case 0: r_f = v_f; g_f = t;   b_f = p;   break;
        case 1: r_f = q;   g_f = v_f; b_f = p;   break;
        case 2: r_f = p;   g_f = v_f; b_f = t;   break;
        case 3: r_f = p;   g_f = q;   b_f = v_f; break;
        case 4: r_f = t;   g_f = p;   b_f = v_f; break;
        case 5: r_f = v_f; g_f = p;   b_f = q;   break;
    }
    return {static_cast<uint8_t>(r_f * 255.0f), static_cast<uint8_t>(g_f * 255.0f), static_cast<uint8_t>(b_f * 255.0f)};
}

// a + (b - a) * amount / 255, per channel
inline Rgb blend(Rgb a, Rgb b, uint8_t amount) {
    return {
        static_cast<uint8_t>(a.r + (static_cast<int16_t>(b.r) - a.r) * amount / 255),
        static_cast<uint8_t>(a.g + (static_cast<int16_t>(b.g) - a.g) * amount / 255),
        static_cast<uint8_t>(a.b + (static_cast<int16_t>(b.b) - a.b) * amount / 255),
    };
}

// like FastLED nscale8_video: a lit channel never drops to 0 while scale > 0
inline Rgb scale_video(Rgb c, uint8_t scale) {
    auto ch = [scale](uint8_t v) -> uint8_t {
        if (v == 0) return 0;
        return static_cast<uint8_t>(((static_cast<uint16_t>(v) * scale) >> 8) + (scale ? 1 : 0));
    };
    return {ch(c.r), ch(c.g), ch(c.b)};
}

inline uint32_t hash32(uint32_t x, uint32_t y) {
    uint32_t h = (x * 0x9E3779B1u) ^ ((y + 0x7F4A7C15u) * 0x85EBCA77u);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

// 2D value noise, smoothstep-interpolated, 0..65535. One lattice cell is 2^shift input units
// (shift 8 matches FastLED inoise8's 8.8 inputs, shift 16 inoise16's 16.16 inputs).
inline uint16_t value_noise(uint32_t x, uint32_t y, unsigned shift) {
    const uint32_t xi   = x >> shift, yi = y >> shift;
    const uint32_t mask = (shift >= 32) ? 0xFFFFFFFFu : ((1u << shift) - 1u);
    const uint32_t fx   = (x & mask) << (16 - shift);   // 0..65535
    const uint32_t fy   = (y & mask) << (16 - shift);
    auto smooth = [](uint32_t t) -> uint32_t {          // 3t^2 - 2t^3 in 16-bit fixed point
        const uint64_t t2 = (static_cast<uint64_t>(t) * t) >> 16;
        return static_cast<uint32_t>((t2 * (3u * 65536u - 2u * t)) >> 16);
    };
    const int64_t sx  = smooth(fx), sy = smooth(fy);
    const int64_t a   = hash32(xi, yi) & 0xFFFFu,     b = hash32(xi + 1, yi) & 0xFFFFu;
    const int64_t c   = hash32(xi, yi + 1) & 0xFFFFu, d = hash32(xi + 1, yi + 1) & 0xFFFFu;
    const int64_t top = a + (((b - a) * sx) >> 16);
    const int64_t bot = c + (((d - c) * sx) >> 16);
    const int64_t v   = top + (((bot - top) * sy) >> 16);
    return static_cast<uint16_t>(v < 0 ? 0 : (v > 65535 ? 65535 : v));
}
inline uint8_t  noise8 (uint32_t x, uint32_t y) { return static_cast<uint8_t>(value_noise(x, y, 8) >> 8); }
inline uint16_t noise16(uint32_t x, uint32_t y) { return value_noise(x, y, 16); }

// sine wave 0..255 at `bpm` beats per minute (beatsin8)
inline uint8_t beat_sin8(uint16_t bpm, uint32_t now_ms) {
    const double phase = std::fmod(static_cast<double>(now_ms) * bpm / 60000.0, 1.0);
    return static_cast<uint8_t>(std::lround(127.5 + 127.5 * std::sin(phase * 6.283185307179586)));
}

// ---- effects ----------------------------------------------------------------------------------------
// Per-instance state. Counters advance once per rendered frame (50 fps in the firmware), as in 2.3.x.
struct EffectState {
    uint32_t              counter = 0;
    uint32_t              rng     = 0x2545F491u;
    std::vector<Rgb>      previous;   // Color Fade Two Zone smoothing
    std::vector<uint16_t> offsets;    // Christmas Lights per-pixel noise offsets
};

inline uint16_t next_random16(uint32_t& rng) {   // xorshift32, deterministic per state
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return static_cast<uint16_t>(rng >> 16);
}

// Size the per-instance buffers of `mode_id` for n pixels (Two Zone smoothing frame, Christmas Lights
// offsets). render() calls it as well, but LedModes calls it from the main loop when it activates a
// mode, so the render task does not allocate on a mode change (only when the strip length changes).
// `seed` != 0 reseeds the state's generator first (Christmas Lights flicker offsets): the firmware passes
// esp_random() ^ millis() so the pattern differs per boot (2.3.x random16()); 0 keeps the fixed default
// seed, which tests/host relies on for pinned frames.
inline void prepare(uint8_t mode_id, EffectState& st, uint16_t n, uint32_t seed = 0) {
    if (seed != 0) {
        st.rng = seed;
        st.offsets.clear();   // refill from the new seed
    }
    if (mode_id == TWO_ZONE && st.previous.size() != n) st.previous.assign(n, Rgb{0, 0, 0});
    if (mode_id == CHRISTMAS && st.offsets.size() != n) {
        st.offsets.resize(n);
        for (uint16_t& o : st.offsets) o = next_random16(st.rng);
    }
}

// Colour that stands for the mode (2.3.x Mode::get_rgb): base colour, white for Rainbow.
inline Rgb mode_color(uint8_t mode_id, const uint16_t* p) {
    switch (mode_id) {
        case SOLID: case FADE_BRIGHTNESS: case PULSE: case FADE_COLOR:   // 2.3.x: core hsv_to_rgb
            return hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
        case TWO_ZONE:  return hsv(static_cast<uint8_t>(p[0]), 255, 255);
        case CHRISTMAS: return {85, 49, 22};
        default:        return {255, 255, 255};
    }
}

// Render one frame of `mode_id` with parameter values `p` (in table order) into buf[0..n).
inline void render(uint8_t mode_id, const uint16_t* p, EffectState& st, Rgb* buf, uint16_t n, uint32_t now_ms) {
    switch (mode_id) {
        case SOLID: {        // 2.3.x: core float hsv_to_rgb
            const Rgb c = hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
            for (uint16_t i = 0; i < n; ++i) buf[i] = c;
            break;
        }
        case FADE_COLOR: {   // hue sat speed fire_step h_gap min_bright
            const int64_t base = static_cast<int64_t>(p[0]) * 256, gap = p[4];
            for (uint16_t i = 0; i < n; ++i) {
                const uint8_t v   = noise8(static_cast<uint32_t>(i) * p[3], st.counter);
                const int64_t h16 = base - gap / 2 + map_range(v, 0, 255, 0, gap);
                buf[i] = hsv_rainbow(static_cast<uint8_t>(static_cast<uint16_t>(h16) >> 8),   // 2.3.x CHSV
                                     clamp8(map_range(v, 0, 255, 255, p[1])),
                                     clamp8(map_range(v, 0, 255, p[5], 255)));
            }
            st.counter += p[2];
            break;
        }
        case TWO_ZONE: {     // hue hue_b blend speed fire_step min_bright min_sat
            prepare(mode_id, st, n);
            const uint8_t  amount  = static_cast<uint8_t>(p[2] ? p[2] : 1);
            const uint32_t spatial = static_cast<uint32_t>(p[4] ? p[4] : 1) * 400u;
            for (uint16_t i = 0; i < n; ++i) {
                const uint16_t v      = noise16(static_cast<uint32_t>(i) * spatial, st.counter);
                const Rgb      target = hsv(static_cast<uint8_t>(map_range(v, 0, 65535, p[0], p[1])),
                                            clamp8(map_range(v, 0, 65535, p[6], 255)),
                                            clamp8(map_range(v, 0, 65535, p[5], 255)));
                st.previous[i] = blend(st.previous[i], target, amount);
                buf[i]         = st.previous[i];
            }
            st.counter += static_cast<uint32_t>(p[3] ? p[3] : 1) * 250u;
            break;
        }
        case FADE_BRIGHTNESS: {   // hue sat speed noise_step min_bright
            for (uint16_t i = 0; i < n; ++i) {
                const uint8_t v = noise8(static_cast<uint32_t>(i) * p[3], st.counter);
                buf[i] = hsv_rainbow(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]),   // 2.3.x CHSV
                                     clamp8(map_range(v, 0, 255, p[4], 255)));
            }
            st.counter += p[2];
            break;
        }
        case PULSE: {        // hue sat speed (bpm); 2.3.x: core float hsv_to_rgb, then nscale8(beatsin8)
            const Rgb     c = hsv_spectrum(static_cast<uint8_t>(p[0]), static_cast<uint8_t>(p[1]), 255);
            const uint8_t s = beat_sin8(p[2], now_ms);
            const Rgb     d = {scale8(c.r, s), scale8(c.g, s), scale8(c.b, s)};
            for (uint16_t i = 0; i < n; ++i) buf[i] = d;
            break;
        }
        case RAINBOW: {      // speed density
            const uint8_t start = static_cast<uint8_t>(st.counter);
            for (uint16_t i = 0; i < n; ++i) {
                buf[i] = hsv_rainbow(static_cast<uint8_t>(start + i * p[1]), 240, 255);   // fill_rainbow
            }
            st.counter += p[0];
            break;
        }
        case CHRISTMAS: {    // density speed
            static constexpr Rgb palette[5] = {
                {255, 6, 0}, {199, 61, 3}, {6, 133, 3}, {10, 10, 122}, {119, 130, 30},
            };
            prepare(mode_id, st, n);
            const uint16_t density = p[0] ? p[0] : 1;
            for (uint16_t i = 0; i < n; ++i) {
                buf[i] = scale_video(palette[(i / density) % 5], noise8(st.offsets[i], st.counter));
            }
            st.counter += p[1];
            break;
        }
        default:
            for (uint16_t i = 0; i < n; ++i) buf[i] = Rgb{0, 0, 0};
            break;
    }
}

// Cross-fade: out[i] = blend(from[i], to[i], progress), progress 0..255.
inline void crossfade(const Rgb* from, const Rgb* to, Rgb* out, uint16_t n, uint8_t progress) {
    for (uint16_t i = 0; i < n; ++i) out[i] = blend(from[i], to[i], progress);
}

inline uint8_t transition_progress(uint32_t elapsed_ms, uint32_t duration_ms = TRANSITION_MS) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) return 255;
    return static_cast<uint8_t>(elapsed_ms * 255u / duration_ms);
}

}  // namespace led_fx
