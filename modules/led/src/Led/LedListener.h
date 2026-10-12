// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedListener.h
//
// Change events of the led module, for the modules that mirror the strip (web page, smart-home
// bridges). Register with `led.listeners.add(&listener)`, before `os.begin()` to see the boot
// values. Every event carries the `origin` the caller passed to the Led setter (nullptr from the
// CLI, the Led object for changes the strip makes itself, e.g. the colour of a newly selected
// mode); a listener that also sets values ignores events whose origin is itself if it does not
// want its own echo. Callbacks run in the caller's task, outside the render lock; keep them short.
#pragma once

#include <array>
#include <cstdint>

struct LedListener {
    virtual ~LedListener() = default;

    // the colour applied (as passed to the setter, before the rgb -> hue/sat round trip)
    virtual void on_color(std::array<uint8_t, 3> rgb, const void* origin) = 0;
    // the brightness as passed to the setter (0 fades to black but keeps the state on)
    virtual void on_brightness(uint8_t brightness, const void* origin) = 0;
    virtual void on_state(bool on, const void* origin) = 0;
    // the requested mode id (an unknown id runs mode 0 but is reported as requested)
    virtual void on_mode(uint8_t mode_id, const void* origin) = 0;
    virtual void on_length(uint16_t length, const void* origin) {
        (void)length;
        (void)origin;
    }
    // `$led reset`: every value at once. The default forwards in the order state, length, mode,
    // brightness, colour; a listener that sends one combined message overrides it.
    virtual void on_all(std::array<uint8_t, 3> rgb, uint8_t brightness, bool on, uint8_t mode_id,
        uint16_t length, const void* origin) {
        on_state(on, origin);
        on_length(length, origin);
        on_mode(mode_id, origin);
        on_brightness(brightness, origin);
        on_color(rgb, origin);
    }
};
