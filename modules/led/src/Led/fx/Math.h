// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/fx/Math.h
//
// Shared pure maths of the LED modes: colour conversions, noise, blending, cross-fade. Host-compilable
// on purpose: standard library only, no Arduino, no FastLED, no XeWeCore (tests/unit/test_effects.cpp
// builds it with g++). The conversions are small local re-implementations of the 2.3.x ones: FastLED's
// rainbow HSV (bit-exact, see hsv_rainbow), core's float HSV (same code as XeWeCore Utils/Color.h) and
// the six-sector HSV of Color Fade Two Zone. Frames are close to, not bit-identical with, 2.3.x because
// the noise is value noise, not FastLED's Perlin inoise8/16.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../Pixel.h"

namespace led_fx {

using Rgb = LedRgb;   // one pixel type for the strip and the modes (LM17)

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
// tests/unit checks it against the core function for all 65,536 hue x sat inputs at every value tested.
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

// ---- random -----------------------------------------------------------------------------------------
inline uint16_t next_random16(uint32_t& rng) {   // xorshift32, deterministic per state
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return static_cast<uint16_t>(rng >> 16);
}

// ---- cross-fade -------------------------------------------------------------------------------------
// Cross-fade: out[i] = blend(from[i], to[i], progress), progress 0..255.
inline void crossfade(const Rgb* from, const Rgb* to, Rgb* out, uint16_t n, uint8_t progress) {
    for (uint16_t i = 0; i < n; ++i) out[i] = blend(from[i], to[i], progress);
}

}  // namespace led_fx
