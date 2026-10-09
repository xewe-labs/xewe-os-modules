// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-strip/src/LedStrip/Chipsets.h
#pragma once

#include <cstdint>
#include <cstring>
#include <strings.h>

// Supported chipsets. Ids are the ids of xewe-led-os 2.3.x (LED_CHIPSET_TABLE, NVS key led/chip), so a
// device keeps its stored chip. The old table had 46 entries; only the chips the release matrix ships
// (WS2812B on-board LEDs and Led Dock strips) plus the common WS2811/WS2812/SK6812/APA102 are compiled
// in, because every entry instantiates a FastLED driver. Adding one: a row here and a case in
// LedStrip::add_leds() (LedStrip.cpp). The full old list is in README.md.
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

inline const LedChipset* led_chipset_by_id(int id) {
    for (const LedChipset& c : LED_CHIPSETS) if (c.id == id) return &c;
    return nullptr;
}

inline const LedChipset* led_chipset_by_name(const char* name) {
    for (const LedChipset& c : LED_CHIPSETS) if (strcasecmp(c.name, name) == 0) return &c;
    return nullptr;
}

// Colour orders, indexed like NVS key led/colorder (same order as 2.3.x).
inline constexpr const char* LED_COLOR_ORDERS[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};

inline int led_color_order_index(const char* name) {
    for (int i = 0; i < 6; ++i) if (strcasecmp(LED_COLOR_ORDERS[i], name) == 0) return i;
    return -1;
}
