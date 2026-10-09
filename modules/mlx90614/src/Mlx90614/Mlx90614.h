// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/mlx90614/src/Mlx90614/Mlx90614.h
//
// The mlx90614 module: an MLX90614 contactless I2C temperature sensor (object and ambient), polled
// every 500 ms (5 s while it does not answer). Error reads (short read, the sensor's error flag) are
// reported as offline, never as a temperature. Listeners (Mlx90614Listener) get every poll result,
// e.g. to feed the fan module's curve. NVS namespace `mlx90614`, key `data`.
#pragma once

#include <XeWeCore.h>
// Generated per build by `xewe build` (MLX90614_* values passed with --define). Unconditional on
// purpose: a __has_include() guard makes arduino-cli drop the generated library.
#include <XeWeBuildInfo.h>
#include <sdkconfig.h>

#include <cmath>
#include <string>

#include "Convert.h"

// ---- Build-time defaults, used on the module's first boot only (then NVS, `$mlx90614 set_pins/set_addr`)
// 255 = pin not configured. C3/C6: the XeWe cooling pad's wiring; S3: free, non-strapping GPIOs.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#  ifndef MLX90614_SDA
#    define MLX90614_SDA 8
#  endif
#  ifndef MLX90614_SCL
#    define MLX90614_SCL 9
#  endif
#endif
#ifndef MLX90614_SDA
#define MLX90614_SDA 4
#endif
#ifndef MLX90614_SCL
#define MLX90614_SCL 5
#endif
#ifndef MLX90614_ADDR
#define MLX90614_ADDR 0x5A
#endif
#ifndef MLX90614_LISTENERS_MAX
#define MLX90614_LISTENERS_MAX 4
#endif


// Poll results. Called from the main loop after every poll (500 ms online, 5 s offline) and after
// `read`/`set_pins`/`set_addr`. Offline: online = false and both temperatures NaN. Keep it short.
struct Mlx90614Listener {
    virtual ~Mlx90614Listener() = default;
    virtual void on_temperature(float object_c, float ambient_c, bool online) = 0;
};

struct Mlx90614Config {
    uint8_t                     sda_pin                     = MLX90614_SDA;
    uint8_t                     scl_pin                     = MLX90614_SCL;
    uint8_t                     i2c_address                 = MLX90614_ADDR;
    uint32_t                    poll_interval_ms            = 500;
    uint32_t                    offline_poll_interval_ms    = 5000;     // back off while no sensor answers
    uint16_t                    scan_budget_ms              = 1500;     // `scan` stops after this long
    uint16_t                    scan_probe_timeout_ms       = 10;       // Wire timeout per probe during `scan`
};

// Stored settings (NVS namespace "mlx90614", key "data"). Append only; bump `schema` on a change.
struct Mlx90614Store : xewe::FlexData<Mlx90614Store> {
    static constexpr uint8_t    SCHEMA                      = 1;
    uint8_t                     schema                      = SCHEMA;
    uint8_t                     i2c_address                 = 0x5A;
    uint8_t                     sda_pin                     = 255;
    uint8_t                     scl_pin                     = 255;

    static constexpr auto fields() {
        return std::make_tuple(fld("schema",      &Mlx90614Store::schema),
                               fld("i2c_address", &Mlx90614Store::i2c_address),
                               fld("sda_pin",     &Mlx90614Store::sda_pin),
                               fld("scl_pin",     &Mlx90614Store::scl_pin));
    }
};


class Mlx90614 : public xewe::Module {
public:
    explicit                    Mlx90614                    (xewe::Os& host, Mlx90614Config config = {});

    void                        begin_routines_required     ()                              override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose      = false,
                                                             const bool do_restart   = true,
                                                             const bool keep_enabled = true) override;
    std::string                 status                      (const bool verbose = false)    const override;

    // object temperature in °C; NaN while disabled or offline (never an error value such as 1037 C)
    float                       get_temp                    ()                              const;
    float                       get_ambient_temp            ()                              const;
    bool                        is_online                   ()                              const;
    uint32_t                    get_error_count             ()                              const { return read_errors; }
    bool                        set_i2c_address             (uint8_t new_address);
    bool                        set_pins                    (uint8_t sda, uint8_t scl);
    // {"module","online","object_temp"|null,"ambient_temp"|null,"i2c_address","sda_pin","scl_pin","read_errors"}
    std::string                 get_json                    ()                              const;

    // up to MLX90614_LISTENERS_MAX, no heap; false when full or null
    bool                        add_listener                (Mlx90614Listener* listener);
    bool                        remove_listener             (Mlx90614Listener* listener);

private:
    bool                        pins_configured             ()                              const;
    void                        start_bus                   ();
    float                       read_i2c_temp               (uint8_t register_address);
    void                        poll                        ();
    void                        notify                      ();
    void                        save                        ()                              const;
    int                         scan                        ();

    void                        read_cmd                    (xewe::span<const std::string> args);
    void                        scan_cmd                    (xewe::span<const std::string> args);
    void                        set_addr_cmd                (xewe::span<const std::string> args);
    void                        set_pins_cmd                (xewe::span<const std::string> args);

    Mlx90614Config              config;

    float                       cached_object_temp          {NAN};
    float                       cached_ambient_temp         {NAN};
    bool                        sensor_online               {false};
    bool                        bus_started                 {false};
    uint32_t                    last_read_time              {0};
    uint32_t                    read_errors                 {0};        // error flag or short read (not a missing sensor)

    uint8_t                     sda_pin                     {255};
    uint8_t                     scl_pin                     {255};
    uint8_t                     i2c_address                 {0x5A};

    Mlx90614Listener*           listeners[MLX90614_LISTENERS_MAX] = {};

    static constexpr uint8_t    MLX_RAM_TA                  = 0x06;
    static constexpr uint8_t    MLX_RAM_TOBJ1               = 0x07;
    static constexpr uint8_t    NO_PIN                      = 255;
};
