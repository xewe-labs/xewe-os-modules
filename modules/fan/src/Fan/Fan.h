// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/fan/src/Fan/Fan.h
//
// The fan module: 4-wire PWM fans (LEDC, 25 kHz, 8 bit) with optional tachometers, and a
// temperature -> speed curve (Curve.h, pure and host-tested) that drives every fan once a second
// while a temperature source feeds it (`fan.set_temperature(c, origin)`, e.g. a sensor module's
// listener wired in the project). Commands under `$fan`, curve commands under `$fan curve ...`.
// NVS namespace `fan`: key `data` (the fans), key `curve` (the curve points), and the settings table
// rows `curve_ms`, `stale_ms` (core 2.1: `$fan get|schema`, `$fan set <key> <value>`).
#pragma once

#include <XeWeCore.h>
// Generated per build by `xewe build` (FAN* values passed with --define). Unconditional on purpose:
// a __has_include() guard makes arduino-cli drop the generated library and the defaults below win.
#include <XeWeBuildInfo.h>
#include <sdkconfig.h>

#include <string>
#include <vector>

#include "Curve.h"

// ---- Build-time defaults: the fans set up on the module's first boot (no stored settings yet) -------
// 255 = no fan / no tachometer. Afterwards the fans live in NVS and change with `$fan add/remove`.
// Defaults are the XeWe laptop cooling pad's wiring: C3/C6 PWM 3/10, tach 0/1; the S3 gets free,
// non-strapping GPIOs (0 and 3 are strapping pins there). Override: xewe build --define FAN1_PWM=255 ...
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#  ifndef FAN1_PWM
#    define FAN1_PWM 4
#  endif
#  ifndef FAN1_TACH
#    define FAN1_TACH 5
#  endif
#  ifndef FAN2_PWM
#    define FAN2_PWM 6
#  endif
#  ifndef FAN2_TACH
#    define FAN2_TACH 7
#  endif
#endif
#ifndef FAN1_PWM
#define FAN1_PWM 3
#endif
#ifndef FAN1_TACH
#define FAN1_TACH 0
#endif
#ifndef FAN2_PWM
#define FAN2_PWM 10
#endif
#ifndef FAN2_TACH
#define FAN2_TACH 1
#endif


// One fan the module sets up on its first boot. pwm 255 = skip, tach 255 = no tachometer.
struct FanDefault {
    uint8_t                     pwm                         = 255;
    uint8_t                     tach                        = 255;
};

struct FanConfig {
    std::vector<FanDefault>     defaults                    {{FAN1_PWM, FAN1_TACH}, {FAN2_PWM, FAN2_TACH}};
    float                       ema_alpha                   = 0.3f;
    uint32_t                    absolute_max_rpm            = 10000;
    uint32_t                    ui_rounding                 = 10;
    // curve period and stale timeout are run-time settings (`curve_ms`, `stale_ms`)
};

// Curve events (core listener set: `fan.listeners.add(&l)`, up to 4). `origin` is whoever caused
// the change: the temperature source passed to set_temperature(), or the caller of a curve change
// (nullptr from the CLI). Called in the caller's task (curve target: from loop()); keep them short.
struct FanListener {
    virtual ~FanListener() = default;
    // the curve's target speed changed (0-100 %) at `celsius` (NaN: source offline or stale)
    virtual void on_curve_target(uint8_t, float, const void*) {}
    // the curve points changed (add, remove, set, reset)
    virtual void on_curve_changed(const void*) {}
};

// Stored settings (NVS namespace "fan", key "data"). Layout is frozen once flashed: append only,
// bump `schema` on a change.
struct FanEntry : xewe::FlexData<FanEntry> {
    uint8_t                     pwm                         = 0;
    uint8_t                     tach                        = 255;      // 255 = no tachometer
    uint8_t                     speed                       = 0;        // 0-255 duty

    static constexpr auto fields() {
        return std::make_tuple(fld("pwm",   &FanEntry::pwm),
                               fld("tach",  &FanEntry::tach),
                               fld("speed", &FanEntry::speed));
    }
};

struct FanStore : xewe::FlexData<FanStore> {
    static constexpr uint8_t    SCHEMA                      = 1;
    uint8_t                     schema                      = SCHEMA;
    std::vector<FanEntry>       fans;

    static constexpr auto fields() {
        return std::make_tuple(fld("schema", &FanStore::schema),
                               fld("fans",   &FanStore::fans));
    }
};

struct FanCurvePoint : xewe::FlexData<FanCurvePoint> {
    float                       temp                        = 0.0f;     // °C
    uint8_t                     speed                       = 0;        // 0-100 %

    static constexpr auto fields() {
        return std::make_tuple(fld("temp",  &FanCurvePoint::temp),
                               fld("speed", &FanCurvePoint::speed));
    }
};

// Stored curve (NVS namespace "fan", key "curve"). Append only; bump `schema` on a change.
struct FanCurveStore : xewe::FlexData<FanCurveStore> {
    static constexpr uint8_t    SCHEMA                      = curve_math::SCHEMA;
    uint8_t                     schema                      = SCHEMA;
    std::vector<FanCurvePoint>  points;

    static constexpr auto fields() {
        return std::make_tuple(fld("schema", &FanCurveStore::schema),
                               fld("points", &FanCurveStore::points));
    }
};


class Fan : public xewe::Module {
public:
    explicit                    Fan                         (xewe::Os& host, FanConfig config = {});
                                ~Fan                        () override;

    void                        begin_routines_required     ()                              override;
    void                        loop                        ()                              override;
    void                        reset                       (const bool verbose      = false,
                                                             const bool do_restart   = true,
                                                             const bool keep_enabled = true) override;
    std::string                 status                      (const bool verbose = false)    const override;
    xewe::Settings              settings                    ()                              const override;
    // the fans and the curve points (not plain rows): "group":"fans"/"curve" with a "set" hint
    void                        schema_extra                (xewe::SchemaOut& out)          const override;

    xewe::ListenerSet<FanListener> listeners;

    // ---- fans ---------------------------------------------------------------------------------------
    bool                        add                         (uint8_t pwm_pin, uint8_t tach_pin = 255);
    bool                        remove                      (uint8_t pwm_pin);
    // persist = false for control loops: no NVS write per call
    bool                        set                         (uint8_t pwm_pin, uint8_t speed, bool persist = true);
    bool                        set_all                     (uint8_t speed, bool persist = true);
    uint32_t                    get_rpm                     (uint8_t pwm_pin)               const;
    size_t                      count                       ()                              const { return fans.size(); }

    // ---- curve --------------------------------------------------------------------------------------
    // The temperature input of the curve (°C; NaN = source offline, counts as hot). The first call
    // activates the curve: from then on it sets every fan once per `curve_ms` (RAM only), and a
    // source that stops calling for `stale_ms` is treated as offline. `origin` identifies the
    // caller (nullptr from the CLI); shown in status.
    void                        set_temperature             (float celsius, const void* origin = nullptr);
    float                       get_temperature             ()                              const { return temperature; }
    bool                        curve_active                ()                              const;
    uint8_t                     get_curve_target            ()                              const { return curve_target_pct; }
    const std::vector<FanCurvePoint>& get_curve             ()                              const { return curve.points; }
    // add or replace the point at `temp` (speed 0-100 %)
    bool                        curve_add                   (float temp, uint8_t speed_pct, const void* origin = nullptr);
    bool                        curve_remove                (float temp, const void* origin = nullptr);
    // replace the whole curve (sorted here, then validated); persist = false: RAM only, then save_curve()
    bool                        set_curve                   (std::vector<FanCurvePoint> points, bool persist = true,
                                                             const void* origin = nullptr);
    void                        save_curve                  ();

    // {"fans":[{"pin_pwm","has_tach","speed"(0-100),"pin_tach","displayed_rpm","ema_rpm"}],
    //  "curve":[{"temp","speed"}],"curve_target":0-100}
    std::string                 get_json                    ()                              const;
    // [{"temp","speed"}]
    std::string                 get_curve_json              ()                              const;

    static constexpr size_t     MAX_CURVE_POINTS            = curve_math::MAX_POINTS;

private:
    struct FanData {
        uint8_t             pin_pwm         = 0;
        uint8_t             pin_tach        = 255;
        bool                has_tach        = false;
        uint8_t             speed           = 0;

        volatile uint32_t   pulse_count     = 0;
        uint32_t            last_calc_time  = 0;

        uint32_t            raw_history[3]  = {0, 0, 0};
        uint8_t             history_idx     = 0;
        uint8_t             history_count   = 0;
        uint32_t            ema_rpm         = 0;
        uint32_t            displayed_rpm   = 0;
    };

    static void                 tach_isr_handler            (void* arg);

    FanData*                    get_fan                     (uint8_t pwm_pin)               const;
    void                        free_fan                    (FanData* fan);
    FanData*                    create_fan                  (uint8_t pwm, uint8_t tach, uint8_t speed);
    void                        clear_fans                  ();

    void                        load                        ();
    void                        save                        ();
    void                        load_curve                  ();
    void                        default_curve               ();
    void                        run_curve                   (uint32_t now, bool force = false);
    void                        curve_changed               (const void* origin);
    std::string                 curve_lines                 ()                              const;

    void                        add_cmd                     (xewe::span<const std::string> args);
    void                        add_w_tach_cmd              (xewe::span<const std::string> args);
    void                        set_cmd                     (xewe::span<const std::string> args);
    void                        set_all_cmd                 (xewe::span<const std::string> args);
    void                        remove_cmd                  (xewe::span<const std::string> args);
    void                        temp_cmd                    (xewe::span<const std::string> args);
    void                        curve_cmd                   (xewe::span<const std::string> args);

    FanConfig                   config;
    std::vector<FanData*>       fans;

    FanCurveStore               curve;
    float                       temperature                 {NAN};
    bool                        temperature_seen            {false};
    uint32_t                    temperature_ms              {0};
    const void*                 temperature_origin          {nullptr};
    uint32_t                    last_curve_ms               {0};
    uint8_t                     curve_target_pct            {0};
    uint16_t                    curve_ms                    {1000};     // table row: curve -> fans period
    uint32_t                    stale_ms                    {10000};    // table row: no update this long -> NaN

    static constexpr uint32_t   PWM_FREQ                    = 25000;
    static constexpr uint8_t    PWM_RES                     = 8;
    static constexpr uint8_t    NO_PIN                      = 255;
};
