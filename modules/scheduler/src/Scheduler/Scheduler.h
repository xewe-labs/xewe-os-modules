// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/scheduler/src/Scheduler/Scheduler.h
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <sstream>
#include <algorithm>
#include <optional>

#include <XeWeCore.h>
#include "../Time/Time.h"


class Scheduler : public xewe::Module {
public:
                                   Scheduler              (xewe::Os&                   host,
                                                           Time&                        time_module_ref);

    void                           begin_routines_init    ()  override;
    void                           begin_routines_regular ()  override;

    void                           loop                   ()                         override;
    void                           reset                  (bool verbose      = false,
                                                           bool do_restart   = true,
                                                           bool keep_enabled = true) override;
    std::string                    status                 (bool verbose = false)     const override;

    bool                           add                    (uint16_t                 start_time,
                                                           uint16_t                 end_time,
                                                           uint8_t                  day,
                                                           std::string              displayed_color,
                                                           std::vector<std::string> commands);

    bool                           remove                 (uint8_t schedule_id);

    std::string                    get_all_json           ()                         const;

    uint16_t                       load_from_nvs          ();
    void                           save_to_nvs            ();

private:
    Time&                          time_module;

    struct ScheduleBlock : xewe::FlexData<ScheduleBlock> {
        uint8_t                    id              = 0;
        uint16_t                   start_time      = 0; // minutes from midnight
        uint16_t                   end_time        = 0; // minutes from midnight
        uint8_t                    day             = 0; // 0=Monday ... 6=Sunday
        std::string                displayed_color = "000000";
        std::vector<std::string>   commands;

        static constexpr auto    fields() {
            return std::make_tuple(
                fld("id", &ScheduleBlock::id),
                fld("start_time", &ScheduleBlock::start_time),
                fld("end_time", &ScheduleBlock::end_time),
                fld("day", &ScheduleBlock::day),
                fld("displayed_color", &ScheduleBlock::displayed_color),
                fld("commands", &ScheduleBlock::commands)
            );
        }
    };

    struct SchedulerData : xewe::FlexData<SchedulerData> {
        std::vector<ScheduleBlock> schedules;

        static constexpr auto      fields() {
            return std::make_tuple(
                fld("schedules", &SchedulerData::schedules)
            );
        }
    };

    SchedulerData                  data;
    int16_t                        last_processed_minute  = -1;

    void                           execute                (const ScheduleBlock& schedule);

    // CLI callbacks
    void                           cli_add                (xewe::span<const std::string> args);
    void                           cli_remove             (xewe::span<const std::string> args);
};