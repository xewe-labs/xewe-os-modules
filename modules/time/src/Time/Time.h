// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/time/src/Time/Time.h
#pragma once

#include <optional>
#include <ctime>
#include <string>
#include <string_view>
#include <Arduino.h>
#include <atomic>

#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <XeWeCore.h>
#include "../Wifi/Wifi.h"


class Time : public xewe::Module {
public:
                          Time                    (xewe::Os&                   host,
                                                   Wifi&                        wifi_ref);

    void                  begin_routines_required ()  override;
    void                  begin_routines_init     ()  override;
    void                  begin_routines_regular  ()  override;

    std::string           status                  (bool verbose = false)     const override;
    // settings table: `tz_gmt_str` (str <= 9, "GMT+00:00"); an offset stored by an earlier version loads unchanged
    xewe::Settings        settings                ()                         const override;

    tm                    get_current_time        ()                         const;
    std::string           get_current_time_str    ()                         const;
    void                  print_current_time      ();

protected:
    // `tz_gmt_str` changed: normalise (GMT-8 -> GMT-08:00) and apply; an unparsable value is put back
    void                  on_setting_changed      (const xewe::SettingDef& def) override;

private:
    Wifi&                 wifi;
    bool                  time_set                {false};
    std::string           active_tz_string        {"GMT+00:00"};
    std::string           tz_gmt_str              {"GMT+00:00"};     // table row

    void                  get_time_from_web_init  ();
    bool                  get_time_from_web_wait  (const bool verbose = true);
    void                  apply_timezone          (std::string_view gmt_offset_str);

    void                  cli_set_timezone        (xewe::span<const std::string> args);
    // waits for SNTP, then prints the time or how to retry
    void                  sync_and_print          ();

    struct TzRace {
        std::atomic<bool> abort   {false};
        std::atomic<int>  claimed {0}; // CAS 0->1 selects the single winner
        char              result  [16]{};
        SemaphoreHandle_t winner; // binary, given once by the winner
        SemaphoreHandle_t done; // counting(3,0), given once per worker on exit
    };
    struct TzArg {
        const char*       url;
        const char*       key;
        TzRace*           ctx;
    };

    static void           fetch_tz_task           (void* pvParameters);
};