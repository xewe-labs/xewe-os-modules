// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/fan/tests/unit/test_curve.cpp
//
// Unit test of src/Fan/Curve.h (no Arduino): built and run by
// tests/unit/test_fan.py::test_curve_unit_gpp with g++ -std=c++17 -Wall -Wextra -Werror.
// Prints one line per check, then "PASSED|FAILED: N check(s), F failure(s)"; exits non-zero on a failure.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "../../src/Fan/Curve.h"

using namespace curve_math;

// Stand-in for the FlexData-backed CurvePoint: same field names and types.
struct Pt {
    float   temp;
    uint8_t speed;
};

static int failures = 0;
static int checks   = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (cond) { std::printf("ok   %s\n", #cond); }                             \
        else      { std::printf("FAIL %s (line %d)\n", #cond, __LINE__); ++failures; } \
    } while (0)

static bool same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

int main() {
    const std::vector<Pt> def = {{22.0f, 0}, {27.0f, 100}, {32.0f, 100}};   // the firmware default
    const std::vector<Pt> none;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // ---- interpolation and clamps ----
    CHECK(target_speed(def, 24.5f) == 50);
    CHECK(target_speed(def, 26.99f) == 99);                  // truncated, never rounded up
    CHECK(target_speed(def, 22.0f) == 0);                    // exactly on a point
    CHECK(target_speed(def, 10.0f) == 0);                    // below the first: first speed
    CHECK(target_speed(def, -40.0f) == 0);
    CHECK(target_speed(def, 255.0f) == 100);                 // sensor offline (255 C): last speed
    CHECK(target_speed(def, 40.0f) == 100);                  // above the last: last speed
    CHECK(target_speed(none, 30.0f) == 0);                   // empty curve: fans off
    CHECK(target_speed(def, nan) == 100);                    // unreadable: fail-safe hot
    {
        const std::vector<Pt> hi = {{30.0f, 20}, {50.0f, 80}};
        CHECK(target_speed(hi, 0.0f) == 20);                 // below-first clamp is the first speed, not 0
        CHECK(target_speed(hi, 90.0f) == 80);                // above-last clamp is the last speed, not 100
    }
    {   // monotonic: a non-decreasing curve gives a non-decreasing speed, sampled every 0.05 C
        const std::vector<Pt> c = {{20.0f, 10}, {25.0f, 35}, {30.0f, 35}, {40.0f, 90}, {45.0f, 100}};
        bool mono = true;
        uint8_t prev = 0;
        for (int i = 0; i <= 700; ++i) {
            const uint8_t s = target_speed(c, 15.0f + 0.05f * static_cast<float>(i));
            if (s < prev) mono = false;
            prev = s;
        }
        CHECK(mono);
        CHECK(prev == 100);
    }
    {   // a stored speed above 100 % never leaves the 0-100 range
        const std::vector<Pt> bad = {{20.0f, 0}, {30.0f, 250}};
        CHECK(target_speed(bad, 35.0f) == 100);
    }

    // ---- duty per fan ----
    CHECK(speed_to_pwm(0) == 0);
    CHECK(speed_to_pwm(42) == 107);                          // matches the status line "42 % (PWM 107)"
    CHECK(speed_to_pwm(100) == 255);
    CHECK(speed_to_pwm(200) == 255);
    CHECK(target_pwm(def, 24.5f) == 127);
    CHECK(clamp_speed(-5) == 0 && clamp_speed(101) == 100 && clamp_speed(64) == 64);

    // ---- validation: sorted, unique, in range ----
    CHECK(validate_points(def) == CurveError::ok);
    CHECK(validate_points(none) == CurveError::ok);
    CHECK(validate_points(std::vector<Pt>{{30.0f, 10}, {20.0f, 50}}) == CurveError::unsorted);
    CHECK(validate_points(std::vector<Pt>{{30.0f, 10}, {30.0f, 50}}) == CurveError::duplicate);
    CHECK(validate_points(std::vector<Pt>{{30.0f, 10}, {30.005f, 50}}) == CurveError::duplicate);   // within SAME_TEMP
    CHECK(validate_points(std::vector<Pt>{{30.0f, 10}, {30.02f, 50}}) == CurveError::ok);
    CHECK(validate_points(std::vector<Pt>{{20.0f, 101}}) == CurveError::bad_speed);
    CHECK(validate_points(std::vector<Pt>{{250.0f, 10}}) == CurveError::bad_temp);
    CHECK(validate_points(std::vector<Pt>{{-41.0f, 10}}) == CurveError::bad_temp);
    CHECK(validate_points(std::vector<Pt>{{nan, 10}}) == CurveError::bad_temp);
    {
        std::vector<Pt> many;
        for (int i = 0; i < 16; ++i) many.push_back({static_cast<float>(i), 50});
        CHECK(validate_points(many) == CurveError::ok);
        many.push_back({16.0f, 50});
        CHECK(validate_points(many) == CurveError::too_many);
    }
    CHECK(std::string(error_text(CurveError::duplicate)) == "two points at the same temperature");

    // ---- schema version ----
    CHECK(schema_ok(SCHEMA));
    CHECK(SCHEMA == 1);
    CHECK(!schema_ok(0));
    CHECK(!schema_ok(2));
    CHECK(!schema_ok(255));
    CHECK(schema_message(2).find("schema 2") != std::string::npos);
    CHECK(schema_message(2).find("default curve") != std::string::npos);

    // ---- LED hysteresis around the first point (22 C, band 0.5 C) ----
    CHECK(leds_on(def, 22.0f, false));                       // at the first point: on
    CHECK(!leds_on(def, 21.0f, true));                       // clearly below: off
    CHECK(leds_on(def, 21.8f, true));                        // in the band: stays on
    CHECK(!leds_on(def, 21.8f, false));                      // in the band: stays off
    CHECK(leds_on(def, nan, false));                         // unreadable: on (hot)
    {   // noise of +-0.3 C around 21.9 C: no flapping once on, none once off
        const float noise[] = {21.9f, 22.1f, 21.6f, 22.0f, 21.7f, 21.95f, 21.62f, 22.05f};
        bool state = true;
        int  flips_on = 0;
        for (float t : noise) { const bool n = leds_on(def, t, state); flips_on += (n != state); state = n; }
        // starts on: 21.6-21.95 is inside the band, 22.0+ is on -> never off
        CHECK(flips_on == 0);
        state = false;
        int flips_off = 0;
        for (float t : noise) { const bool n = leds_on(def, t, state); flips_off += (n != state); state = n; }
        // starts off: the first 22.1 turns it on, then nothing in the band turns it off again
        CHECK(flips_off == 1);
    }
    {   // a slow sweep down and up crosses each threshold exactly once
        bool state = true;
        int  flips = 0;
        for (int i = 0; i <= 40; ++i) { const bool n = leds_on(def, 23.0f - 0.05f * i, state); flips += (n != state); state = n; }
        CHECK(!state && flips == 1);                         // off only after 21.5 C
        for (int i = 0; i <= 40; ++i) { const bool n = leds_on(def, 21.0f + 0.05f * i, state); flips += (n != state); state = n; }
        CHECK(state && flips == 2);                          // on again only at 22.0 C
    }

    // ---- colours: cold/hot selection (hex parsing is the core's xewe::str::parse_hex_color) ----
    {
        const Rgb cold{0, 255, 255}, hot{255, 0, 0};
        CHECK(same(led_colour(def, 10.0f, cold, hot), cold));
        CHECK(same(led_colour(def, 22.0f, cold, hot), cold));
        CHECK(same(led_colour(def, 32.0f, cold, hot), hot));
        CHECK(same(led_colour(def, 255.0f, cold, hot), hot));
        CHECK(same(led_colour(def, nan, cold, hot), hot));
        CHECK(same(led_colour(def, 27.0f, cold, hot), (Rgb{127, 127, 127})));   // midway, truncated
        CHECK(same(led_colour(none, 30.0f, cold, hot), hot));
        const std::vector<Pt> one = {{30.0f, 50}};
        CHECK(same(led_colour(one, 29.0f, cold, hot), cold));
        CHECK(same(led_colour(one, 30.0f, cold, hot), hot));
    }

    {
        // `$fan curve set` argument
        std::vector<Pt> v;
        CHECK(parse_curve_spec("22:0,27:100,32:100", v) && v.size() == 3 && v[1].temp == 27.0f && v[1].speed == 100);
        CHECK(parse_curve_spec("40.5:50", v) && v.size() == 1 && v[0].temp == 40.5f && v[0].speed == 50);
        CHECK(parse_curve_spec("-5:0,30:80", v) && v.size() == 2 && v[0].temp == -5.0f);
        CHECK(parse_curve_spec("none", v) && v.empty());
        CHECK(!parse_curve_spec("", v));
        CHECK(!parse_curve_spec("22:101", v));                   // speed above 100 %
        CHECK(!parse_curve_spec("22:-1", v));
        CHECK(!parse_curve_spec("22", v));
        CHECK(!parse_curve_spec("22:", v));
        CHECK(!parse_curve_spec(":50", v));
        CHECK(!parse_curve_spec("22:50,", v));                  // trailing comma
        CHECK(!parse_curve_spec("abc:50", v));
        CHECK(!parse_curve_spec("1:1,2:1,3:1,4:1,5:1,6:1,7:1,8:1,9:1,10:1,11:1,12:1,13:1,14:1,15:1,16:1,17:1", v));
        CHECK(parse_curve_spec("30:50,20:10", v) && validate_points(v) == CurveError::unsorted);   // caller sorts
    }

    std::printf("%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
