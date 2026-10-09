// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/mlx90614/src/Mlx90614/Convert.h
//
// MLX90614 value mapping as pure functions: no Arduino, no Wire. Mlx90614.cpp calls these;
// tests/unit/test_convert.cpp builds them with the host g++ (-Wall -Wextra -Werror).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace mlx90614_fx {

constexpr float   KELVIN_STEP   = 0.02f;      // one LSB of a RAM temperature register, K
constexpr float   KELVIN_ZERO   = 273.15f;
constexpr uint8_t DEFAULT_ADDR  = 0x5A;

// A RAM temperature word (LSB, MSB as read) -> °C. Bit 15 is the sensor's error flag: a glitched
// bus returns 0xFFFF, which would read as 1037.5 °C; reported as an error (false), never as a value.
inline bool raw_to_celsius(uint8_t lsb, uint8_t msb, float& out) {
    if (msb & 0x80) return false;
    const uint16_t raw = static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
    out = static_cast<float>(raw) * KELVIN_STEP - KELVIN_ZERO;
    return true;
}

// 7-bit I2C address the module accepts: 0x01-0x7F (0x00 is the general call).
inline bool valid_address(unsigned value) { return value >= 0x01 && value <= 0x7F; }

// "0x5A", "5a", "5A" -> 0x5A; the whole token must be hex and the value a valid address.
inline bool parse_address(const std::string& text, uint8_t& out) {
    if (text.empty() || text.size() > 4) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &end, 16);
    if (end == text.c_str() || *end != '\0' || text[0] == '-' || text[0] == '+' || text[0] == ' ') return false;
    if (!valid_address(static_cast<unsigned>(v))) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

// Bounded bus scan: the scan stops when the time budget is spent or after this many probes in a
// row that end in a bus error or timeout (a bus without pull-ups or with a stuck line), instead of
// waiting out every address (tens of seconds on a bare bus).
constexpr uint8_t MAX_BUS_ERRORS_IN_A_ROW = 3;

// Wire::endTransmission() result of a probe: 0 ack, 2 nack (no device: normal), 4 bus error,
// 5 timeout. Returns the updated error streak.
inline uint8_t scan_error_streak(uint8_t streak, uint8_t result) {
    return (result == 0 || result == 2 || result == 3) ? 0 : static_cast<uint8_t>(streak + 1);
}

} // namespace mlx90614_fx
