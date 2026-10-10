// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/scheduler/src/Scheduler/Scheduler.cpp

#include "Scheduler.h"


Scheduler::Scheduler(xewe::Os& host,
                     Time&     time_module_ref)
    : xewe::Module(host,
          /* id                  */ "schedule",
          /* name                */ "Scheduler",
          /* description         */ "Runs stored commands on a weekly schedule",
          /* requires_init_setup */ true,
          /* can_be_disabled     */ false,
          /* has_cli_cmds        */ true
    )
    , time_module(time_module_ref)
{
    add_requirement(time_module);

    register_command({
        "add",
        "Add schedule: <start> <end> <day> <RRGGBB> \"<cmd1|cmd2>\"",
        "$schedule add 480 1020 1 FF0000 \"$system status|$time status\"",
        5,
        [this](xewe::span<const std::string> args) { cli_add(args); }
    });

    register_command({
        "remove",
        "Remove schedule: <id>",
        "$schedule remove 1",
        1,
        [this](xewe::span<const std::string> args) { cli_remove(args); }
    });

    // no settings table, so the core registers no `schema`: ours prints the extra rows
    register_command({
        "schema",
        "Print the schedules as schema rows (JSON Lines)",
        "$schedule schema",
        0,
        [this](xewe::span<const std::string>) {
            xewe::SchemaOut out(os.serial);
            print_schema(out);
            os.serial.printf("{\"end\":\"%s\",\"count\":%u}", id.c_str(), static_cast<unsigned>(out.count()));
        }
    });
}

void Scheduler::schema_extra(xewe::SchemaOut& out) const {
    for (const ScheduleBlock& b : data.schedules) {
        out.row("\"key\":\"" + std::to_string(b.id) + "\",\"group\":\"schedule\",\"type\":\"schedule\",\"value\":" +
                b.as_json_str() + ",\"set\":\"$schedule add <start> <end> <day> <RRGGBB> \\\"<cmds>\\\" | remove <id>\"");
    }
}
void Scheduler::begin_routines_init() {
    os.serial.print("Scheduler: available after the automatic reboot");
}

void Scheduler::begin_routines_regular() {
    os.serial.printf("Scheduler: %u schedule blocks loaded", static_cast<unsigned>(load_from_nvs()));
}

void Scheduler::loop() {
    if (is_disabled() || data.schedules.empty()) return;

    // local time from the time module
    tm time_info = time_module.get_current_time();

    // not synced yet: tm_year counts from 1900, so before 1970 means no time
    if (time_info.tm_year < 70) return;

    // minute of the day, 0..1439
    int16_t current_minute_of_day = (time_info.tm_hour * 60) + time_info.tm_min;

    if (current_minute_of_day == last_processed_minute) return;
    last_processed_minute = current_minute_of_day;

    // tm_wday counts 0=Sunday..6=Saturday; schedule days count 0=Monday..6=Sunday
    uint8_t current_day   = (time_info.tm_wday + 6) % 7;

    for (const ScheduleBlock& schedule : data.schedules) {
        if (schedule.day == current_day && schedule.start_time == current_minute_of_day) {
            execute(schedule);
        }
    }
}

void Scheduler::reset(bool verbose,
                      bool do_restart,
                      bool keep_enabled) {
    data.schedules.clear();
    os.nvs.remove(id, "schedules");
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Scheduler::status(bool verbose) const {
    std::string text = Module::status(false);
    if (is_enabled()) text += "\n" + data.as_json_str();

    if (verbose) {
        os.serial.print(text);
    }
    return text;
}

bool Scheduler::add(uint16_t start_time,
                    uint16_t end_time,
                    uint8_t day,
                    std::string displayed_color,
                    std::vector<std::string> commands) {
    if (is_disabled()) return false;

    ScheduleBlock block;

    block.id = 0;
    for (const ScheduleBlock& s : data.schedules) {
        if (s.id >= block.id) {
            block.id = s.id + 1;
        }
    }

    block.start_time      = start_time;
    block.end_time        = end_time;
    block.day             = day;
    block.displayed_color = std::move(displayed_color);
    block.commands        = std::move(commands);

    data.schedules.push_back(std::move(block));
    save_to_nvs();

    return true;
}

bool Scheduler::remove(uint8_t sid) {
    const std::vector<ScheduleBlock>::iterator new_end = std::remove_if(
        data.schedules.begin(),
        data.schedules.end(),
        [sid](const ScheduleBlock& schedule) {
            return schedule.id == sid;
        }
    );

    if (new_end == data.schedules.end()) return false;

    data.schedules.erase(new_end, data.schedules.end());
    save_to_nvs();

    return true;
}

std::string Scheduler::get_all_json() const {
    return data.get_field("schedules");
}

uint16_t Scheduler::load_from_nvs() {
    if (!os.nvs.read_flex(id, "schedules", data))
        data.schedules.clear();
    return data.schedules.size();
}

void Scheduler::save_to_nvs() {
    if (is_disabled()) return;
    os.nvs.write_flex(id, "schedules", data);
}

void Scheduler::execute(const ScheduleBlock& schedule) {
    for (const std::string& cmd : schedule.commands) {
        os.cli.execute(cmd);
    }
}

void Scheduler::cli_add(xewe::span<const std::string> args) {
    std::optional<uint16_t>    start_val = xewe::validate<uint16_t>(args[0], 0, 1439);
    std::optional<uint16_t>    end_val   = xewe::validate<uint16_t>(args[1], 0, 1439);
    std::optional<uint8_t>     day_val   = xewe::validate<uint8_t>(args[2], 0, 6);
    std::optional<std::string> color_val = xewe::validate<std::string>(args[3], 6, 6);

    if (!start_val || !end_val || !day_val || !color_val) {
        os.serial.print("! Scheduler: invalid parameters or out of range (start/end 0-1439, day 0-6, color 6 chars)");
        return;
    }

    std::vector<std::string> commands;
    for (std::string& token : xewe::str::split_by_token(args[4], "|")) {
        if (!token.empty()) commands.push_back(std::move(token));
    }

    const bool added = add(
        start_val.value(),
        end_val.value(),
        day_val.value(),
        color_val.value(),
        std::move(commands)
    );

    os.serial.print(added ? "Scheduler: schedule saved" : "! Scheduler: invalid schedule");
}

void Scheduler::cli_remove(xewe::span<const std::string> args) {
    std::optional<uint8_t> target_id = xewe::validate<uint8_t>(args[0], 0, 255);

    if (!target_id) {
        os.serial.print("! Scheduler: invalid id or out of bounds");
        return;
    }

    if (remove(target_id.value())) {
        os.serial.print("Scheduler: schedule removed");
    } else {
        os.serial.print("! Scheduler: schedule not found");
    }
}