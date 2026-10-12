// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Chipsets.h
//
// The 46 supported chipsets and the 6 colour orders. Pure header (standard library only). The id
// is what the `chip` setting stores and the wizard menu shows (1-based), so the order never
// changes. `clocked` chips use the data and the clock pin; the others only the data pin.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

enum class LEDChipset : std::uint8_t {
    APA102,
    APA102HD,
    APA104,
    APA106,
    DOTSTAR,
    DOTSTARHD,
    GE8822,
    GS1903,
    GW6205,
    GW6205_400KHZ,
    HD107,
    HD107HD,
    LPD1886,
    LPD1886_8BIT,
    LPD6803,
    LPD8806,
    NEOPIXEL,
    P9813,
    PL9823,
    SK6812,
    SK6822,
    SK9822,
    SK9822HD,
    SM16703,
    SM16716,
    SM16824E,
    TM1803,
    TM1804,
    TM1809,
    TM1812,
    TM1829,
    UCS1903,
    UCS1903B,
    UCS1904,
    UCS1912,
    UCS2903,
    WS2801,
    WS2803,
    WS2811,
    WS2811_400KHZ,
    WS2812,
    WS2812B,
    WS2813,
    WS2815,
    WS2816,
    WS2852,
};

struct LEDChipsetEntry {
    std::uint8_t id;
    LEDChipset value;
    const char* name;
    bool clocked;
};

inline constexpr LEDChipsetEntry LED_CHIPSET_TABLE[] = {
    {0, LEDChipset::APA102, "APA102", true},
    {1, LEDChipset::APA102HD, "APA102HD", true},
    {2, LEDChipset::APA104, "APA104", false},
    {3, LEDChipset::APA106, "APA106", false},
    {4, LEDChipset::DOTSTAR, "DOTSTAR", true},
    {5, LEDChipset::DOTSTARHD, "DOTSTARHD", true},
    {6, LEDChipset::GE8822, "GE8822", false},
    {7, LEDChipset::GS1903, "GS1903", false},
    {8, LEDChipset::GW6205, "GW6205", false},
    {9, LEDChipset::GW6205_400KHZ, "GW6205_400KHZ", false},
    {10, LEDChipset::HD107, "HD107", true},
    {11, LEDChipset::HD107HD, "HD107HD", true},
    {12, LEDChipset::LPD1886, "LPD1886", false},
    {13, LEDChipset::LPD1886_8BIT, "LPD1886_8BIT", false},
    {14, LEDChipset::LPD6803, "LPD6803", true},
    {15, LEDChipset::LPD8806, "LPD8806", true},
    {16, LEDChipset::NEOPIXEL, "NEOPIXEL", false},
    {17, LEDChipset::P9813, "P9813", true},
    {18, LEDChipset::PL9823, "PL9823", false},
    {19, LEDChipset::SK6812, "SK6812", false},
    {20, LEDChipset::SK6822, "SK6822", false},
    {21, LEDChipset::SK9822, "SK9822", true},
    {22, LEDChipset::SK9822HD, "SK9822HD", true},
    {23, LEDChipset::SM16703, "SM16703", false},
    {24, LEDChipset::SM16716, "SM16716", true},
    {25, LEDChipset::SM16824E, "SM16824E", false},
    {26, LEDChipset::TM1803, "TM1803", false},
    {27, LEDChipset::TM1804, "TM1804", false},
    {28, LEDChipset::TM1809, "TM1809", false},
    {29, LEDChipset::TM1812, "TM1812", false},
    {30, LEDChipset::TM1829, "TM1829", false},
    {31, LEDChipset::UCS1903, "UCS1903", false},
    {32, LEDChipset::UCS1903B, "UCS1903B", false},
    {33, LEDChipset::UCS1904, "UCS1904", false},
    {34, LEDChipset::UCS1912, "UCS1912", false},
    {35, LEDChipset::UCS2903, "UCS2903", false},
    {36, LEDChipset::WS2801, "WS2801", true},
    {37, LEDChipset::WS2803, "WS2803", true},
    {38, LEDChipset::WS2811, "WS2811", false},
    {39, LEDChipset::WS2811_400KHZ, "WS2811_400KHZ", false},
    {40, LEDChipset::WS2812, "WS2812", false},
    {41, LEDChipset::WS2812B, "WS2812B", false},
    {42, LEDChipset::WS2813, "WS2813", false},
    {43, LEDChipset::WS2815, "WS2815", false},
    {44, LEDChipset::WS2816, "WS2816", false},
    {45, LEDChipset::WS2852, "WS2852", false},
};

inline constexpr std::size_t LED_CHIPSET_COUNT =
    sizeof(LED_CHIPSET_TABLE) / sizeof(LED_CHIPSET_TABLE[0]);

// index = the `colorder` setting; applied in software after dimming (logical r,g,b -> wire order)
inline constexpr std::array<std::string_view, 6> LED_COLOR_ORDERS = {
    "RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};
