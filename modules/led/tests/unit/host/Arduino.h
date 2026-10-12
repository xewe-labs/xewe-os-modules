// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/host/Arduino.h
//
// Host stand-in for <Arduino.h> for the led engine tests (g++ with FastLED's stub platform): a
// settable clock, the arduino-esp32 map()/constrain() semantics the effects depend on, and a
// Serial that discards output (XeWeCore's DBG_* macros). Nothing else, so a new hardware or
// randomness dependency of the engine fails to compile here.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>

// the prototypes of FastLED's stub platform (platforms/stub/time_stub.h)
extern "C" {
uint32_t millis(void);
uint32_t micros(void);
void delay(int ms);
void delayMicroseconds(int us);
void yield(void);
}

namespace host {
void set_millis(uint32_t ms);
void advance_millis(uint32_t ms);
} // namespace host

// arduino-esp32 cores/esp32/Arduino.h
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
using std::abs;
using std::max;
using std::min;

// arduino-esp32 cores/esp32/WMath.cpp map(); `long` is 32-bit on the ESP32, so the arithmetic is
// done in 32 bits with wrap-around to give the device result on a 64-bit host
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    const int32_t run = (int32_t)((uint32_t)(int32_t)in_max - (uint32_t)(int32_t)in_min);
    if (run == 0) {
        return -1;
    }
    const int32_t rise = (int32_t)((uint32_t)(int32_t)out_max - (uint32_t)(int32_t)out_min);
    const int32_t delta = (int32_t)((uint32_t)(int32_t)x - (uint32_t)(int32_t)in_min);
    const int32_t prod = (int32_t)((uint32_t)delta * (uint32_t)rise);
    return (long)(int32_t)((uint32_t)(prod / run) + (uint32_t)(int32_t)out_min);
}

struct HostSerial {
    template <typename T> std::size_t print(const T&) {
        return 0;
    }
    template <typename T> std::size_t println(const T&) {
        return 0;
    }
};
extern HostSerial Serial;
