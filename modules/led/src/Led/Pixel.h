// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Pixel.h
#pragma once

#include <cstdint>
#include <cstddef>

// One pixel of a frame, in logical RGB order (colour order and brightness are applied by Led on
// output). Layout-compatible with FastLED's CRGB; the mode maths uses it as led_fx::Rgb (fx/Math.h).
// Standard library only: the host unit tests include it.
struct LedRgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
static_assert(sizeof(LedRgb) == 3, "LedRgb must be 3 packed bytes");

// CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) over a frame; `$led checksum` prints it.
inline uint32_t led_frame_crc32(const LedRgb* frame, std::size_t count) {
    const uint8_t* p   = reinterpret_cast<const uint8_t*>(frame);
    uint32_t       crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < count * 3; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
