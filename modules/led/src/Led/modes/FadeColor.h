// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/FadeColor.h
#pragma once

#include "Mode.h"

// id 1 "Color Fade": inoise8 field around a base hue, FastLED rainbow CHSV
class FadeColor : public Mode {
public:
    explicit FadeColor(const std::map<std::string, uint16_t>& params);

    void loop(CRGB* leds, uint16_t num_leds) override;
    std::array<uint8_t, 3> get_rgb() override;

private:
    CRGB get_fire_color(
        uint8_t noise_val, long base_hue_16bit, long hue_gap, uint8_t min_sat, uint8_t min_bright);
    CRGB ColorHSV(long hue, uint8_t sat, uint8_t val);

    uint32_t counter;
    CRGB base_rgb;
};
