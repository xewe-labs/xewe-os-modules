// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-modes/tests/unit/test_effects.cpp
//
// Unit test of src/LedModes/Effects.h (no Arduino): built and run by unit/test_led-modes.py with
// g++ -std=c++17 -Wall -Wextra -Werror. Prints one line per check, then
// "PASSED|FAILED: <n> check(s), <f> failure(s)" (CONTRACT.md "Unit tests"); exits non-zero on a failure.
#include <cstdio>
#include <cstdint>
#include <vector>

#include "../../src/LedModes/Effects.h"

using namespace led_fx;

static int failures = 0;
static int checks   = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (cond) { std::printf("ok   %s\n", #cond); }                             \
        else      { std::printf("FAIL %s (line %d)\n", #cond, __LINE__); ++failures; } \
    } while (0)

static uint32_t crc32(const std::vector<Rgb>& f) {
    const uint8_t* p   = reinterpret_cast<const uint8_t*>(f.data());
    uint32_t       crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < f.size() * 3; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static std::vector<Rgb> frames(uint8_t mode, int count, uint16_t n = 32) {
    uint16_t p[MAX_PARAMS];
    default_params(MODES[mode], p);
    EffectState      st;
    std::vector<Rgb> buf(n);
    for (int f = 0; f < count; ++f) render(mode, p, st, buf.data(), n, static_cast<uint32_t>(f) * 20u);
    return buf;
}

static bool same(const std::vector<Rgb>& a, const std::vector<Rgb>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].r != b[i].r || a[i].g != b[i].g || a[i].b != b[i].b) return false;
    }
    return true;
}

int main() {
    // Solid: hue 0 sat 255 -> pure red on every pixel
    {
        uint16_t p[MAX_PARAMS] = {0, 255};
        EffectState      st;
        std::vector<Rgb> buf(16);
        render(SOLID, p, st, buf.data(), 16, 0);
        bool uniform_red = true;
        for (const Rgb& c : buf) uniform_red &= (c.r == 255 && c.g == 0 && c.b == 0);
        CHECK(uniform_red);
    }
    // Rainbow: deterministic, animated, and not uniform
    {
        const auto a = frames(RAINBOW, 3), b = frames(RAINBOW, 3), c = frames(RAINBOW, 4);
        CHECK(same(a, b));
        CHECK(!same(a, c));
        CHECK(!(a[0].r == a[5].r && a[0].g == a[5].g && a[0].b == a[5].b));
        // pinned: integer-only maths, so the frame is the same on every host (and on the ESP32)
        CHECK(crc32(a) == 0x2a589e69u);   // LX1: FastLED rainbow HSV (was 0x9103d394 with six-sector HSV)
        uint16_t    p[MAX_PARAMS] = {0, 255};
        EffectState st;
        std::vector<Rgb> red(32);
        render(SOLID, p, st, red.data(), 32, 0);
        CHECK(crc32(red) == 0x537dea52u);   // == zlib.crc32(b"\xff\x00\x00" * 32), as `$led checksum` prints
    }
    // Christmas Lights and the noise modes: deterministic per state
    for (uint8_t m : {FADE_COLOR, TWO_ZONE, FADE_BRIGHTNESS, CHRISTMAS}) {
        CHECK(same(frames(m, 5), frames(m, 5)));
    }
    // Pulse: brightness follows time, 0 ms vs a quarter beat differ
    {
        uint16_t p[MAX_PARAMS] = {0, 255, 60};
        EffectState      st;
        std::vector<Rgb> a(4), b(4);
        render(PULSE, p, st, a.data(), 4, 0);
        render(PULSE, p, st, b.data(), 4, 250);
        CHECK(a[0].r != b[0].r);
    }
    // cross-fade end points
    {
        std::vector<Rgb> from(8, Rgb{200, 0, 0}), to(8, Rgb{0, 0, 100}), out(8);
        crossfade(from.data(), to.data(), out.data(), 8, 0);
        CHECK(same(out, from));
        crossfade(from.data(), to.data(), out.data(), 8, 255);
        CHECK(same(out, to));
        CHECK(transition_progress(0) == 0 && transition_progress(TRANSITION_MS) == 255);
        CHECK(transition_progress(450) > 100 && transition_progress(450) < 155);
    }
    // parameter clamping
    {
        const ModeDef& rainbow = MODES[RAINBOW];
        const ParamDef& speed  = rainbow.params[param_index(rainbow, "speed")];
        CHECK(clamp_param(speed, 0) == 1 && clamp_param(speed, 99) == 20 && clamp_param(speed, 7) == 7);
        const ParamDef& hue = MODES[SOLID].params[0];
        CHECK(clamp_param(hue, 256) == 0 && clamp_param(hue, -1) == 255);
        CHECK(param_index(rainbow, "nope") == -1);
    }
    // n = 0 and n changes never write out of range (vector-backed state resizes)
    {
        uint16_t p[MAX_PARAMS];
        default_params(MODES[TWO_ZONE], p);
        EffectState      st;
        std::vector<Rgb> buf(64);
        render(TWO_ZONE, p, st, buf.data(), 64, 0);
        render(TWO_ZONE, p, st, buf.data(), 8, 20);
        render(TWO_ZONE, p, st, buf.data(), 0, 40);
        CHECK(st.previous.empty());
    }
    // LX1 colour fidelity: hsv_rainbow == FastLED 3.10.3 hsv2rgb_rainbow (2.3.x CHSV / fill_rainbow) and
    // hsv_spectrum == core xewe::color::hsv_to_rgb (2.3.x Solid / Pulse). The exhaustive comparison against
    // the real functions is test_logs/2026-10-09-night-run/led/lx1-colour-compare.*; these are spot values.
    {
        struct Ref { uint8_t h, s, v; Rgb c; };
        static const Ref rainbow_ref[] = {   // printed by FastLED's hsv2rgb_rainbow
            {0, 255, 255, {0xff, 0x00, 0x00}},   {32, 255, 255, {0xab, 0x55, 0x00}},
            {64, 255, 255, {0xab, 0xaa, 0x00}},  {96, 255, 255, {0x00, 0xff, 0x00}},
            {128, 255, 255, {0x00, 0xab, 0x55}}, {160, 255, 255, {0x00, 0x00, 0xff}},
            {195, 255, 255, {0x5d, 0x00, 0xa3}}, {224, 255, 255, {0xaa, 0x00, 0x55}},
            {96, 240, 128, {0x00, 0x41, 0x00}},
        };
        bool rainbow_ok = true;
        for (const Ref& r : rainbow_ref) {
            const Rgb c = hsv_rainbow(r.h, r.s, r.v);
            rainbow_ok &= (c.r == r.c.r && c.g == r.c.g && c.b == r.c.b);
        }
        CHECK(rainbow_ok);
        const Rgb grey = hsv_rainbow(77, 0, 255), black = hsv_rainbow(77, 200, 0);
        CHECK(grey.r == 255 && grey.g == 255 && grey.b == 255 && black.r == 0 && black.g == 0 && black.b == 0);
        const Rgb red = hsv_spectrum(0, 255, 255), green = hsv_spectrum(85, 255, 255);
        CHECK(red.r == 255 && red.g == 0 && red.b == 0);
        CHECK(green.r == 0 && green.g == 255 && green.b == 0);   // `$led_modes color 00ff00` -> Color: 00ff00
    }
    // Christmas Lights: an explicit seed changes the flicker offsets, the same seed repeats them, and
    // seed 0 keeps the fixed default (the pinned CRC below)
    {
        EffectState a, b, c, d;
        prepare(CHRISTMAS, a, 32, 0x1234567u);
        prepare(CHRISTMAS, b, 32, 0x1234567u);
        prepare(CHRISTMAS, c, 32, 0x89abcdefu);
        prepare(CHRISTMAS, d, 32);
        CHECK(a.offsets == b.offsets);
        CHECK(a.offsets != c.offsets && a.offsets != d.offsets);
        EffectState e;
        prepare(CHRISTMAS, e, 32, 0);
        CHECK(e.offsets == d.offsets);
    }
    // every effect: same inputs -> same CRC (two independent states), pinned per mode. LX1 changed the
    // pins of Color Fade (1), Brightness Fade (3) and Rainbow (5) when they moved to FastLED rainbow HSV
    // (were 0xb3575225, 0xb8d00e70, 0x5dd08412); Solid, Two Zone, Pulse, Christmas are unchanged. The pins are
    // host values (checked with g++ -O0 and -O2 on aarch64); Pulse uses std::sin, so its pin is host-only.
    {
        static const uint32_t pinned[MODE_COUNT] = {
            0x537dea52u, 0xadec45b6u, 0x4510ffddu, 0x608f73d2u, 0xb07b1a25u, 0xd80afa2fu, 0x817dd83cu,
        };
        for (uint8_t m = 0; m < MODE_COUNT; ++m) {
            const uint32_t a = crc32(frames(m, 7)), b = crc32(frames(m, 7));
            std::printf("     mode %u crc %08x\n", static_cast<unsigned>(m), static_cast<unsigned>(a));
            CHECK(a == b);
            CHECK(a == pinned[m]);
        }
    }
    // prepare() sizes the state up front, so render() at that length neither reallocates nor changes
    // the output (LedModes calls prepare() from the main loop; the render task must not allocate)
    for (uint8_t m : {TWO_ZONE, CHRISTMAS}) {
        uint16_t p[MAX_PARAMS];
        default_params(MODES[m], p);
        EffectState st;
        prepare(m, st, 32);
        const void* before = (m == TWO_ZONE) ? static_cast<const void*>(st.previous.data())
                                             : static_cast<const void*>(st.offsets.data());
        std::vector<Rgb> buf(32);
        for (int f = 0; f < 7; ++f) render(m, p, st, buf.data(), 32, static_cast<uint32_t>(f) * 20u);
        const void* after = (m == TWO_ZONE) ? static_cast<const void*>(st.previous.data())
                                            : static_cast<const void*>(st.offsets.data());
        CHECK(before != nullptr && before == after);
        CHECK(crc32(buf) == crc32(frames(m, 7)));
    }
    std::printf("%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
