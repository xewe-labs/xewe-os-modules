// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/mlx90614/tests/unit/test_convert.cpp
//
// Unit test of src/Mlx90614/Convert.h (no Arduino): built and run by
// tests/unit/test_mlx90614.py::test_convert_unit_gpp with g++ -std=c++17 -Wall -Wextra -Werror.
// Prints one line per check, then "PASSED|FAILED: N check(s), F failure(s)"; exits non-zero on a failure.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../../src/Mlx90614/Convert.h"

using namespace mlx90614_fx;

static int failures = 0;
static int checks   = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (cond) { std::printf("ok   %s\n", #cond); }                             \
        else      { std::printf("FAIL %s (line %d)\n", #cond, __LINE__); ++failures; } \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 0.011f; }

int main() {
    // ---- raw -> °C ----
    float t = -1000.0f;
    CHECK(raw_to_celsius(0x8D, 0x3A, t) && near(t, 26.63f));          // 0x3A8D = 14989 -> 299.78 K
    CHECK(raw_to_celsius(0x00, 0x00, t) && near(t, -273.15f));
    CHECK(raw_to_celsius(0xFF, 0x7F, t) && near(t, 382.19f));          // largest valid word
    CHECK(raw_to_celsius(0xAF, 0x3B, t) && near(t, 32.43f));           // 0x3BAF = 15279 -> 305.58 K
    t = 42.0f;
    CHECK(!raw_to_celsius(0xFF, 0xFF, t) && t == 42.0f);               // glitched bus: error, not 1037.5 C
    CHECK(!raw_to_celsius(0x00, 0x80, t) && t == 42.0f);               // error flag alone
    CHECK(!raw_to_celsius(0x8D, 0xBA, t) && t == 42.0f);               // flag over a plausible value

    // ---- addresses ----
    CHECK(valid_address(0x01) && valid_address(0x5A) && valid_address(0x7F));
    CHECK(!valid_address(0x00) && !valid_address(0x80) && !valid_address(0xFF));
    uint8_t a = 0;
    CHECK(parse_address("0x5A", a) && a == 0x5A);
    CHECK(parse_address("5a", a) && a == 0x5A);
    CHECK(parse_address("0X5B", a) && a == 0x5B);
    CHECK(parse_address("7F", a) && a == 0x7F);
    a = 0x33;
    CHECK(!parse_address("0x00", a) && a == 0x33);
    CHECK(!parse_address("0x80", a) && a == 0x33);
    CHECK(!parse_address("0xFF", a) && a == 0x33);
    CHECK(!parse_address("zz", a) && a == 0x33);
    CHECK(!parse_address("", a) && a == 0x33);
    CHECK(!parse_address("5Ag", a) && a == 0x33);
    CHECK(!parse_address("-1", a) && a == 0x33);
    CHECK(!parse_address("0x105A", a) && a == 0x33);
    CHECK(!parse_address(" 5A", a) && a == 0x33);

    // ---- bounded scan: error streak ----
    CHECK(scan_error_streak(0, 0) == 0);
    CHECK(scan_error_streak(2, 2) == 0);                               // nack resets: a normal empty address
    CHECK(scan_error_streak(0, 5) == 1);
    CHECK(scan_error_streak(1, 4) == 2);
    CHECK(scan_error_streak(scan_error_streak(scan_error_streak(0, 5), 5), 5) == MAX_BUS_ERRORS_IN_A_ROW);

    std::printf("%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
