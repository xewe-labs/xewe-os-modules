// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/pins/src/Pins/Pins.h
#pragma once

#include <XeWeCore.h>

#include <Wire.h>


class Pins : public xewe::Module {
public:
    explicit                    Pins                        (xewe::Os& host);

    std::string                 status                      (const bool verbose = false) const override;

private:
    void                        gpio_read_cmd               (xewe::span<const std::string> args);
    void                        gpio_write_cmd              (xewe::span<const std::string> args);
    void                        gpio_toggle_cmd             (xewe::span<const std::string> args);
    void                        gpio_mode_cmd               (xewe::span<const std::string> args);
    void                        adc_read_cmd                (xewe::span<const std::string> args);
    void                        pwm_setup_cmd               (xewe::span<const std::string> args);
    void                        pwm_write_cmd               (xewe::span<const std::string> args);
    void                        pwm_stop_cmd                (xewe::span<const std::string> args);
    void                        i2c_scan_cmd                (xewe::span<const std::string> args);
};
