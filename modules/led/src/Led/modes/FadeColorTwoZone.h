// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/FadeColorTwoZone.h
#pragma once

#include <array>
#include <vector>

#include "Mode.h"

// id 2 "Color Fade Two Zone": inoise16 field between hue A and hue B, own integer spectrum,
// smoothed against the previous frame
class FadeColorTwoZone : public Mode {
public:
    explicit FadeColorTwoZone(const std::map<std::string, uint16_t>& params);

    void loop(CRGB* leds, uint16_t num_leds) override;
    std::array<uint8_t, 3> get_rgb() override;

private:
    static constexpr uint8_t MAX_BRIGHT = 255;
    static constexpr uint8_t MAX_SAT = 255;

    void ensure_buffer(uint16_t num_leds);
    uint16_t get_speed_step() const;
    uint32_t get_noise_spatial_step() const;
    uint8_t get_blend_amount() const;
    CRGB get_weighted_color(uint16_t val) const;
    CRGB ColorHSV(uint8_t hue, uint8_t sat, uint8_t val) const;
    CRGB blend_colors(const CRGB& color1, const CRGB& color2, uint8_t amount) const;

    uint32_t counter;
    std::array<uint8_t, 3> base_rgb;
    std::vector<CRGB> previous_frame;
};
