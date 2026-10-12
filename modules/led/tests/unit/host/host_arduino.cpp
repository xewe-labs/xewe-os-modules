// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/host/host_arduino.cpp
//
// The settable clock behind host/Arduino.h (replaces FastLED's stub time_stub.cpp).

#include <Arduino.h>

HostSerial Serial;

namespace {
uint32_t g_ms = 0;
}

extern "C" {
uint32_t millis(void) {
    return g_ms;
}
uint32_t micros(void) {
    return g_ms * 1000u;
}
void delay(int ms) {
    if (ms > 0)
        g_ms += static_cast<uint32_t>(ms);
}
void delayMicroseconds(int) {}
void yield(void) {}
}

namespace host {
void set_millis(uint32_t ms) {
    g_ms = ms;
}
void advance_millis(uint32_t ms) {
    g_ms += ms;
}
} // namespace host
