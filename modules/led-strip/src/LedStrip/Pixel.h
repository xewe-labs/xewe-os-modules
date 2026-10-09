// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-strip/src/LedStrip/Pixel.h
#pragma once

#include <cstdint>
#include <cstddef>

// One pixel of a frame, in logical RGB order (colour order and brightness are applied by LedStrip on
// output). Layout-compatible with FastLED's CRGB and with led_fx::Rgb of the led-modes module.
struct LedRgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
static_assert(sizeof(LedRgb) == 3, "LedRgb must be 3 packed bytes");

// Something that produces frames for the strip (led-modes registers itself as one).
// render() runs in the strip's render task with the render mutex held: no blocking, no NVS, no serial.
class LedFrameSource {
public:
    virtual      ~LedFrameSource () = default;
    virtual void render          (LedRgb* frame, uint16_t count, uint32_t now_ms) = 0;
};

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
