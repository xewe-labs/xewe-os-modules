// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedListener.h
//
// Change notifications of the led module. Standard library plus the core's
// host-includable Utils/Listeners.h, so the fan-out is host-tested (led/tests/unit/test_listeners.cpp).
//
//   led.add_listener(&my_listener);     // up to XEWE_MODULE_LED_LISTENERS_MAX, no heap
//
// Led calls on_brightness/on_state from the strip setters and on_mode/on_color/on_param from the mode
// setters (Led::notify_listeners is the fan-out). Every callback carries the `origin` pointer the caller
// passed to the setter (nullptr from the CLI), so a listener can ignore its own echo:
//
//   void on_state(bool on, const void* origin) override { if (origin == this) return; ... }
//
// Callbacks run in the main loop (the task that called the setter), after the change is applied and
// outside the render mutex; never in the render task. Keep them short (set a flag, push later).
#pragma once

#include <cstddef>
#include <cstdint>

#include <XeWeCore/Utils/Listeners.h>

#include "Config.h"   // XEWE_MODULE_LED_LISTENERS_MAX

struct LedListener {
    virtual ~LedListener() = default;

    // strip
    virtual void on_brightness (uint8_t value, const void* origin)                                  { (void)value; (void)origin; }
    virtual void on_state      (bool on, const void* origin)                                        { (void)on; (void)origin; }
    // modes
    virtual void on_mode       (uint8_t mode_id, const void* origin)                                { (void)mode_id; (void)origin; }
    virtual void on_color      (uint32_t rrggbb, const void* origin)                                { (void)rrggbb; (void)origin; }
    virtual void on_param      (uint8_t mode_id, const char* key, uint16_t value, const void* origin) {
        (void)mode_id; (void)key; (void)value; (void)origin;
    }
};

// the core's fixed-size set (core >= 2.1): add (no duplicates, false when full or null), remove, fan-out
// in slot order; a slot freed by remove() is reused by the next add()
using LedListeners = xewe::ListenerSet<LedListener, XEWE_MODULE_LED_LISTENERS_MAX>;

// rrggbb as LedListener::on_color and Led::get_color() report it
constexpr uint32_t led_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
