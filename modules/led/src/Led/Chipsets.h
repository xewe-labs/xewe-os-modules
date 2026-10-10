// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Chipsets.h
#pragma once

#include <cstddef>
#include <cstdint>

// Supported chipsets. Ids are the ids of xewe-led-os 2.3.x (LED_CHIPSET_TABLE, NVS key led/chip), so a
// device keeps its stored chip. The old table had 46 entries; only the chips the release matrix ships
// (WS2812B on-board LEDs and Led Dock strips) plus the common WS2811/WS2812/SK6812/APA102 are compiled
// in, because every entry instantiates a FastLED driver. Adding one: a row here and a case in
// Led::add_leds() (Led.cpp). The full old list is in README.md.
struct LedChipset {
    uint8_t     id;
    const char* name;
    bool        clocked;   // needs LED_PIN_CLOCK
};

inline constexpr LedChipset LED_CHIPSETS[] = {
    { 0, "APA102",  true },
    {19, "SK6812",  false},
    {38, "WS2811",  false},
    {40, "WS2812",  false},
    {41, "WS2812B", false},
};
inline constexpr std::size_t LED_CHIPSET_COUNT = sizeof(LED_CHIPSETS) / sizeof(LED_CHIPSETS[0]);

// ASCII case-insensitive equality; constexpr so the settings table can take its defaults from the
// LED_CHIPSET / LED_COLOR_ORDER strings at compile time
constexpr bool led_name_equal(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        const char x = (*a >= 'A' && *a <= 'Z') ? char(*a + 32) : *a;
        const char y = (*b >= 'A' && *b <= 'Z') ? char(*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

constexpr const LedChipset* led_chipset_by_id(int id) {
    for (const LedChipset& c : LED_CHIPSETS) if (c.id == id) return &c;
    return nullptr;
}

constexpr const LedChipset* led_chipset_by_name(const char* name) {
    for (const LedChipset& c : LED_CHIPSETS) if (led_name_equal(c.name, name)) return &c;
    return nullptr;
}

// Colour orders, indexed like NVS key led/colorder (same order as 2.3.x).
inline constexpr const char* LED_COLOR_ORDERS[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};

constexpr int led_color_order_index(const char* name) {
    for (int i = 0; i < 6; ++i) if (led_name_equal(LED_COLOR_ORDERS[i], name)) return i;
    return -1;
}
