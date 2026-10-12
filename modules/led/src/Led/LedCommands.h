// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/LedCommands.h
//
// The 30 `$led` commands: names, descriptions, sample usages and argument counts are an API
// (buttons and schedules store raw command strings), so they never change. Led registers each row
// with the core CLI and runs it through led_run_command; host tests do the same against a
// LedEngine.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <XeWeCore.h>

#include "LedEngine.h"

struct LedCommand {
    const char* name;
    const char* description;
    const char* sample_usage;
    uint8_t arg_count;
    // CLI events carry origin nullptr
    void (*run)(LedEngine& engine, xewe::span<const std::string> args);
    // true: the whole command is one atomic section (no frame in the middle of it). false: the
    // handler's own engine calls open their sections; set_color_order needs this, because its
    // calibration waits for answers while the render task shows the test colours
    bool atomic = true;
};

// Arduino String::toInt() on the ESP32 (atol, 32-bit long): optional whitespace and sign, leading
// decimal digits, the rest ignored, 0 when there are none, saturated to the int32 range. Callers
// narrow the result to the argument type (uint8_t modulo 256, uint16_t modulo 65536).
int32_t led_arg_to_int(const std::string& text);

inline constexpr std::size_t LED_COMMAND_COUNT = 30;
extern const LedCommand LED_COMMANDS[LED_COMMAND_COUNT];

// runs LED_COMMANDS[index] (as one atomic section when the row is atomic)
void led_run_command(LedEngine& engine, std::size_t index, xewe::span<const std::string> args);
