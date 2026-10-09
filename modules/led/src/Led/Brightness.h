// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Brightness.h
#pragma once

#include <XeWeCore.h>

// Brightness and on/off with a linear fade (ported from xewe-led-os 2.3.x Brightness).
// Not thread-safe: Led calls it with its render mutex held.
class LedBrightness {
public:
    explicit                     LedBrightness   (uint16_t fade_ms);

    void                         set_brightness  (uint8_t value);   // remembered even while off
    void                         turn_on         ();
    void                         turn_off        ();

    uint8_t                      get_brightness  () const;          // last non-zero target
    bool                         get_state       () const;
    uint8_t                      get_frame_scale () const;          // 0..255, current fade value

private:
    xewe::AsyncTimer<uint8_t>    timer;
    bool                         state           = false;
    uint8_t                      last_brightness = 0;
};
