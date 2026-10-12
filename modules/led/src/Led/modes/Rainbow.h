// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/modes/Rainbow.h
#pragma once

#include "Mode.h"

// id 5: FastLED fill_rainbow, start hue advanced by `speed` every frame
class Rainbow : public Mode {
public:
    explicit Rainbow(const std::map<std::string, uint16_t>& params);

    void loop(CRGB* leds, uint16_t num_leds) override;
    std::array<uint8_t, 3> get_rgb() override;

private:
    uint16_t current_hue;
};
