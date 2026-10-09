// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedListener.h
//
// Change notifications of the led module (MIGRATION-SURVEY 2.4). Standard library only, so the
// fan-out is host-tested (led/tests/unit/test_listeners.cpp).
//
//   led.add_listener(&my_listener);     // up to LED_LISTENERS_MAX, no heap
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

#ifndef LED_LISTENERS_MAX
#define LED_LISTENERS_MAX 4
#endif

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

// Fixed-size listener set: add (no duplicates, false when full or null), remove, and fan-out in
// registration order. A slot freed by remove() is reused by the next add().
template <typename Listener, std::size_t N>
class LedListenerSet {
public:
    bool add(Listener* listener) {
        if (listener == nullptr) return false;
        for (Listener* l : items) if (l == listener) return true;   // already registered
        for (Listener*& l : items) {
            if (l == nullptr) { l = listener; return true; }
        }
        return false;
    }

    bool remove(Listener* listener) {
        for (Listener*& l : items) {
            if (l != nullptr && l == listener) { l = nullptr; return true; }
        }
        return false;
    }

    std::size_t size() const {
        std::size_t n = 0;
        for (Listener* l : items) n += (l != nullptr);
        return n;
    }

    static constexpr std::size_t capacity() { return N; }

    template <typename F>
    void notify(F&& call) const {
        for (Listener* l : items) {
            if (l != nullptr) call(*l);
        }
    }

private:
    Listener* items[N] = {};
};

using LedListeners = LedListenerSet<LedListener, LED_LISTENERS_MAX>;

// rrggbb as LedListener::on_color and Led::get_color() report it
constexpr uint32_t led_pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
