// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-modes/src/LedModes/LedModes.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <XeWeCore.h>
#include "../LedStrip/LedStrip.h"
#include "Effects.h"

// Effect catalogue for the led-strip module: selects one of the 7 xewe-led-os effects, clamps and
// persists its parameters (NVS namespace `led_modes`, keys `mode_id` and `m:<id>:<key>`), and
// cross-fades for 900 ms on every change. Registers itself as the strip's frame source.
class LedModes : public xewe::Module, public LedFrameSource {
public:
                     LedModes                (xewe::Os& host,
                                              LedStrip& led_strip_ref);

    void             begin_routines_regular  ()                                  override;
    void             reset                   (const bool verbose      = false,
                                              const bool do_restart   = true,
                                              const bool keep_enabled = true)    override;
    std::string      status                  (const bool verbose = false) const  override;

    // LedFrameSource: runs in the strip's render task with its render mutex held
    void             render                  (LedRgb* frame, uint16_t count, uint32_t now_ms) override;

    // Setters notify led-strip's listeners (LedListener.h: on_mode/on_color/on_param) with `origin`,
    // from the calling task (main loop), after the change is applied.
    bool             set_mode                (int mode_id, const void* origin = nullptr);
    bool             set_param               (int mode_id, const std::string& param, int32_t value,
                                              const void* origin = nullptr);
    bool             set_color               (LedRgb color, const void* origin = nullptr);    // hue/sat of the current mode
    bool             set_speed               (int32_t value, const void* origin = nullptr);   // `speed` of the current mode
    bool             reset_params            (int mode_id, const void* origin = nullptr);     // table defaults, one cross-fade
    uint8_t          get_mode                () const;
    uint16_t         get_param               (int mode_id, const std::string& param) const;
    uint32_t         get_color               () const;              // rrggbb of the current mode (status `Color:`)

private:
    struct Slot {
        uint8_t                 mode = 0;
        uint16_t                params[led_fx::MAX_PARAMS] = {};
        led_fx::EffectState     state;
    };

    LedStrip&                   led_strip;

    Slot                        current;                 // guarded by the strip's render mutex
    Slot                        previous;
    bool                        transitioning       = false;
    uint32_t                    transition_start_ms = 0;
    std::vector<led_fx::Rgb>    buffer_old;
    std::vector<led_fx::Rgb>    buffer_new;

    void             load_params             (uint8_t mode_id, uint16_t* out) const;
    void             persist_params          (uint8_t mode_id, const uint16_t* values);
    void             activate                (uint8_t mode_id, const uint16_t* values);   // with cross-fade
    int              parse_mode              (const std::string& text) const;
    std::string      nvs_param_name          (uint8_t mode_id, const char* param) const;

    void             cli_list                (xewe::span<const std::string> args);
    void             cli_set                 (xewe::span<const std::string> args);
    void             cli_param               (xewe::span<const std::string> args);
    void             cli_color               (xewe::span<const std::string> args);
    void             cli_speed               (xewe::span<const std::string> args);
    void             cli_reset_params        (xewe::span<const std::string> args);
    void             notify_params           (uint8_t mode_id, const uint16_t* old_values,
                                              const uint16_t* new_values, const void* origin);
};
