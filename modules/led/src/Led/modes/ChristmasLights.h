// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/ChristmasLights.h
#pragma once

#include "Mode.h"

// id 6: five-colour palette in blocks of `density` bulbs, each bulb flickering on its own inoise8
// track. Every construction draws XEWE_MODULE_LED_NUM_LEDS_MAX values from FastLED random16(),
// which is never seeded, so the offsets depend on how many instances were built since boot.
class ChristmasLights : public Mode {
public:
    explicit ChristmasLights(const std::map<std::string, uint16_t>& params);

    void loop(CRGB* leds, uint16_t num_leds) override;
    std::array<uint8_t, 3> get_rgb() override;

private:
    uint16_t z = 0;
    uint16_t noise_offsets[XEWE_MODULE_LED_NUM_LEDS_MAX];

    static constexpr CRGB palette[5] = {
        CRGB(255, 6, 0), CRGB(199, 61, 3), CRGB(6, 133, 3), CRGB(10, 10, 122), CRGB(119, 130, 30)};
};
