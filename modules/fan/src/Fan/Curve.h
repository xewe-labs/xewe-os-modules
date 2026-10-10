// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/fan/src/Fan/Curve.h
//
// The fan curve as pure functions: no Arduino, no XeWeCore, no I/O. Fan.cpp calls these;
// tests/unit/test_curve.cpp builds them with the host g++ (-Wall -Wextra -Werror).
// The point type is a template parameter: anything with `float temp` and `uint8_t speed` (0-100 %),
// i.e. the FlexData-backed FanCurvePoint on the device, a plain struct in the host test.
// The colour helpers (leds_on, led_colour) map a temperature along the same curve to a colour, for
// integrations that light LEDs from the curve (the cooling pad project). Hex colours: the core's
// xewe::str::parse_hex_color / to_hex_color (Utils/String.h, core 2.1).
#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace curve_math {

// Version of the stored FlexData blob (`fan/curve`, field `schema`). Bump on any layout change.
constexpr uint8_t  SCHEMA            = 1;
constexpr size_t   MAX_POINTS        = 16;
constexpr float    MIN_TEMP          = -40.0f;     // accepted point temperatures, °C
constexpr float    MAX_TEMP          = 200.0f;
constexpr float    SAME_TEMP         = 0.01f;      // two points closer than this are the same point
constexpr float    LED_HYSTERESIS    = 0.5f;       // strips go off only below first point - this

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

enum class CurveError : uint8_t { ok, too_many, bad_temp, bad_speed, unsorted, duplicate };

inline const char* error_text(CurveError e) {
    switch (e) {
        case CurveError::ok:        return "ok";
        case CurveError::too_many:  return "more than 16 points";
        case CurveError::bad_temp:  return "temperature outside -40..200 C";
        case CurveError::bad_speed: return "speed above 100 %";
        case CurveError::unsorted:  return "points not sorted by temperature";
        case CurveError::duplicate: return "two points at the same temperature";
    }
    return "unknown";
}

inline bool temp_ok(float temp) { return std::isfinite(temp) && temp >= MIN_TEMP && temp <= MAX_TEMP; }

inline uint8_t clamp_speed(int pct) { return static_cast<uint8_t>(pct < 0 ? 0 : (pct > 100 ? 100 : pct)); }

// 0-100 % -> 0-255 duty (truncated); above 100 % counts as 100 %.
inline uint8_t speed_to_pwm(uint8_t pct) {
    return static_cast<uint8_t>((static_cast<unsigned>(clamp_speed(pct)) * 255u) / 100u);
}

// A stored or submitted curve is usable when: at most MAX_POINTS, every temperature in range,
// every speed 0-100, temperatures strictly increasing with no two within SAME_TEMP.
// An empty curve is valid (the loop then does nothing).
template <class P>
CurveError validate_points(const std::vector<P>& points) {
    if (points.size() > MAX_POINTS) return CurveError::too_many;
    for (size_t i = 0; i < points.size(); ++i) {
        if (!temp_ok(points[i].temp)) return CurveError::bad_temp;
        if (points[i].speed > 100)    return CurveError::bad_speed;
        if (i == 0) continue;
        const float d = points[i].temp - points[i - 1].temp;
        if (std::fabs(d) < SAME_TEMP) return CurveError::duplicate;
        if (d < 0.0f)                 return CurveError::unsorted;
    }
    return CurveError::ok;
}

// Stored blob check: anything but SCHEMA is rejected (the caller keeps its defaults).
inline bool schema_ok(uint8_t schema) { return schema == SCHEMA; }

inline std::string schema_message(uint8_t schema) {
    return "stored settings have schema " + std::to_string(schema) + ", this firmware reads schema " +
           std::to_string(SCHEMA) + "; using the default curve (the stored blob is left untouched "
           "until the next change)";
}

// points valid (see validate_points); returns 0-100 %. Empty curve: 0. At or below the first /
// at or above the last point: that point's speed. In between: linear interpolation, truncated.
template <class P>
uint8_t target_speed(const std::vector<P>& points, float temp) {
    if (points.empty()) return 0;
    if (!(temp > points.front().temp)) {
        // NaN counts as hot: an unreadable temperature must never stop the fans
        if (std::isnan(temp)) return clamp_speed(points.back().speed);
        return clamp_speed(points.front().speed);
    }
    if (temp >= points.back().temp) return clamp_speed(points.back().speed);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (temp >= a.temp && temp <= b.temp) {
            if (b.temp == a.temp) return clamp_speed(b.speed);
            const float v = a.speed + (static_cast<float>(b.speed) - a.speed) * ((temp - a.temp) / (b.temp - a.temp));
            return clamp_speed(static_cast<int>(v));
        }
    }
    return 0;
}

// A source that never reported: once `stale_ms` has passed since begin() with a curve configured and
// no temperature yet, the fans go to the fail-safe speed, target_speed(points, NaN) (the last point,
// full cooling with the default curve). The first reading ends it.
inline bool never_received(bool seen, bool has_points, uint32_t since_begin_ms, uint32_t stale_ms) {
    return !seen && has_points && since_begin_ms > stale_ms;
}

// Same duty for every fan: curve speed -> 0-255 PWM.
template <class P>
uint8_t target_pwm(const std::vector<P>& points, float temp) {
    return speed_to_pwm(target_speed(points, temp));
}

// LED on/off with hysteresis around the first point: on at or above it, off only once the
// temperature drops LED_HYSTERESIS below it, unchanged in the band. NaN keeps the strips on (hot).
template <class P>
bool leds_on(const std::vector<P>& points, float temp, bool was_on) {
    if (points.empty() || std::isnan(temp)) return true;
    const float first = points.front().temp;
    if (temp >= first) return true;
    if (temp < first - LED_HYSTERESIS) return false;
    return was_on;
}

// `$fan curve set` argument: "T:P,T:P,..." (temperature in °C, speed 0-100 %; any order) or "none"
// (empty curve). Fills `out` in the given order (the caller sorts and validates); false on a syntax
// error, a speed outside 0-100 or more than MAX_POINTS points.
template <class P>
bool parse_curve_spec(const std::string& spec, std::vector<P>& out) {
    out.clear();
    if (spec == "none") return true;
    if (spec.empty()) return false;
    size_t pos = 0;
    while (pos <= spec.size()) {
        const size_t comma = spec.find(',', pos);
        const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const size_t colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size()) return false;
        const std::string t = item.substr(0, colon), v = item.substr(colon + 1);
        char* end = nullptr;
        const float temp = std::strtof(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0') return false;
        for (const char c : v) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        if (v.size() > 3) return false;
        const int speed = std::atoi(v.c_str());
        if (speed > 100 || out.size() >= MAX_POINTS) return false;
        P p{};
        p.temp  = temp;
        p.speed = static_cast<uint8_t>(speed);
        out.push_back(p);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}

// Strip colour at a temperature: cold at or below the first point, hot at or above the last
// (and for NaN), a linear blend in between (truncated per channel).
template <class P>
Rgb led_colour(const std::vector<P>& points, float temp, Rgb cold, Rgb hot) {
    if (points.empty() || std::isnan(temp)) return hot;
    const float min_t = points.front().temp;
    const float max_t = points.back().temp;
    if (temp >= max_t || !(max_t > min_t)) return temp < min_t ? cold : hot;
    if (temp <= min_t) return cold;
    const float t = (temp - min_t) / (max_t - min_t);
    Rgb out;
    out.r = static_cast<uint8_t>(cold.r + (static_cast<float>(hot.r) - cold.r) * t);
    out.g = static_cast<uint8_t>(cold.g + (static_cast<float>(hot.g) - cold.g) * t);
    out.b = static_cast<uint8_t>(cold.b + (static_cast<float>(hot.b) - cold.b) * t);
    return out;
}

} // namespace curve_math
