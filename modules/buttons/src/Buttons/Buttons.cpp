// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/buttons/src/Buttons/Buttons.cpp

#include "Buttons.h"


Buttons::Buttons(xewe::Os& host)
    : xewe::Module(host,
          /* id                  */ "buttons",
          /* name                */ "Buttons",
          /* description         */ "Allows to bind CLI cmds to physical buttons",
          /* requires_init_setup */ false,
          /* can_be_disabled     */ true,
          /* has_cli_cmds        */ true
    )
{
    register_command({
        "add",
        "Add a button mapping: <pin> \"<$cmd ...>\" "
        "<pullup|pulldown> <on_press|on_release|on_change> <debounce_ms>",
        "$buttons add 9 \"$system reboot\" pullup on_press 50",
        5,
        [this](xewe::span<const std::string> args) {
            button_add_cmd(args);
        }
    });

    register_command({
        "remove",
        "Remove a button mapping by its ID",
        "$buttons remove 0",
        1,
        [this](xewe::span<const std::string> args) {
            button_remove_cmd(args);
        }
    });
}

void Buttons::begin_routines_regular() {
    load_from_nvs();
}

void Buttons::loop() {
    for (auto& button : data.buttons) {
        const uint32_t now           = millis();
        const int      current_state = digitalRead(button.pin);

        if (current_state != button.last_flicker_state) {
            button.last_debounce_time = now;
        }

        button.last_flicker_state = current_state;

        if ((now - button.last_debounce_time) <= button.debounce_interval) continue;
        if (current_state == button.last_steady_state) continue;

        button.last_steady_state  = current_state;

        const auto type           = static_cast<ButtonInputMode>(button.type);

        const auto event          = static_cast<ButtonTriggerEvent>(button.event);

        const bool is_pressed     = type == ButtonInputMode::PULL_UP
                                        ? current_state == LOW
                                        : current_state == HIGH;

        const bool should_trigger = event == ButtonTriggerEvent::ON_CHANGE ||
                                    (event == ButtonTriggerEvent::ON_PRESS && is_pressed) ||
                                    (event == ButtonTriggerEvent::ON_RELEASE && !is_pressed);

        if (should_trigger) os.cli.execute(button.command);
    }
}

void Buttons::reset(const bool verbose,
                    const bool do_restart,
                    const bool keep_enabled) {
    data.buttons.clear();
    os.nvs.remove(id, "data");
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Buttons::status(const bool verbose) const {
    if (is_disabled()) return Module::status(verbose);

    const std::string result = Module::status(false) + "\n" +
                               std::to_string(data.buttons.size()) + " button(s) active.";
    if (verbose) os.serial.print(result);
    if (!verbose || data.buttons.empty()) return result;

    std::vector<std::vector<std::string>> cells = {{"ID", "Pin", "Command", "Debounce (ms)", "Type", "Event"}};

    cells.reserve(data.buttons.size() + 1);

    for (const decltype(data.buttons)::value_type& button : data.buttons) {
        const char* type  = "invalid";
        const char* event = "invalid";

        switch (static_cast<ButtonInputMode>(button.type)) {
            case ButtonInputMode::PULL_UP: type = "pullup"; break;
            case ButtonInputMode::PULL_DOWN: type = "pulldown"; break;
            default: break;
        }

        switch (static_cast<ButtonTriggerEvent>(button.event)) {
            case ButtonTriggerEvent::ON_PRESS: event = "on_press"; break;
            case ButtonTriggerEvent::ON_RELEASE: event = "on_release"; break;
            case ButtonTriggerEvent::ON_CHANGE: event = "on_change"; break;
            default: break;
        }

        cells.push_back({
            std::to_string(button.id),
            std::to_string(static_cast<unsigned>(button.pin)),
            button.command,
            std::to_string(button.debounce_interval),
            type,
            event
        });
    }

    std::vector<std::vector<std::string_view>> table;
    table.reserve(cells.size());

    for (const std::vector<std::string>& row : cells) {
        table.emplace_back(row.begin(), row.end());
    }

    os.serial.print_table(table, "Active Buttons");

    return result;
}

void Buttons::add(uint8_t pin,
                  std::string command,
                  ButtonInputMode type,
                  ButtonTriggerEvent event,
                  uint32_t debounce_interval) {
    if (is_disabled()) return;

    uint32_t next_id = 0;

    for (const auto& button : data.buttons) next_id = std::max(next_id, button.id + 1);

    ButtonData button;

    button.id                = next_id;
    button.pin               = pin;
    button.command           = std::move(command);
    button.debounce_interval = debounce_interval;
    button.type              = static_cast<uint8_t>(type);
    button.event             = static_cast<uint8_t>(event);

    pinMode(button.pin, type == ButtonInputMode::PULL_UP ? INPUT_PULLUP : INPUT_PULLDOWN);

    button.last_steady_state  = digitalRead(button.pin);
    button.last_flicker_state = button.last_steady_state;
    button.last_debounce_time = 0;

    data.buttons.push_back(std::move(button));

    save_to_nvs();
}

void Buttons::remove(uint32_t button_id) {
    if (is_disabled()) return;

    const auto old_size = data.buttons.size();

    data.buttons.erase(
        std::remove_if(
            data.buttons.begin(),
            data.buttons.end(),
            [button_id](const ButtonData& button) {
                return button.id == button_id;
            }
        ),
        data.buttons.end()
    );

    if (data.buttons.size() != old_size) save_to_nvs();
}

void Buttons::load_from_nvs() {
    if (is_disabled()) return;

    if (!os.nvs.read_flex(id, "data", data))
        data.buttons.clear();

    for (auto& button : data.buttons) {
        const auto type = static_cast<ButtonInputMode>(button.type);

        pinMode(button.pin, type == ButtonInputMode::PULL_UP ? INPUT_PULLUP : INPUT_PULLDOWN);

        button.last_steady_state  = digitalRead(button.pin);
        button.last_flicker_state = button.last_steady_state;
        button.last_debounce_time = 0;
    }
}

void Buttons::save_to_nvs() {
    if (is_disabled()) return;

    os.nvs.write_flex(id, "data", data);
}

void Buttons::button_add_cmd(xewe::span<const std::string> args) {
    if (is_disabled()) return;

    ButtonInputMode type;

    if (args[2] == "pullup") {
        type = ButtonInputMode::PULL_UP;
    } else if (args[2] == "pulldown") {
        type = ButtonInputMode::PULL_DOWN;
    } else {
        os.serial.print(
            "Error: input mode must be pullup or pulldown."
        );
        return;
    }

    ButtonTriggerEvent event;

    if (args[3] == "on_press") {
        event = ButtonTriggerEvent::ON_PRESS;
    } else if (args[3] == "on_release") {
        event = ButtonTriggerEvent::ON_RELEASE;
    } else if (args[3] == "on_change") {
        event = ButtonTriggerEvent::ON_CHANGE;
    } else {
        os.serial.print(
            "Error: event must be on_press, on_release, or on_change."
        );
        return;
    }

    try {
        const unsigned long pin_value = std::stoul(args[0]);
        const unsigned long debounce  = std::stoul(args[4]);

        if (pin_value > std::numeric_limits<uint8_t>::max()) {
            os.serial.print("Error: invalid pin.");
            return;
        }

        if (debounce > std::numeric_limits<uint32_t>::max()) {
            os.serial.print("Error: invalid debounce value.");
            return;
        }

        add(
            static_cast<uint8_t>(pin_value),
            args[1],
            type,
            event,
            static_cast<uint32_t>(debounce)
        );

        os.serial.print(
            "Successfully added button mapping."
        );
    } catch (...) {
        os.serial.print(
            "Error: invalid pin or debounce value."
        );
    }
}

void Buttons::button_remove_cmd(xewe::span<const std::string> args) {
    if (is_disabled()) return;

    try {
        const unsigned long value = std::stoul(args[0]);

        if (value > std::numeric_limits<uint32_t>::max()) {
            os.serial.print("Error: invalid button ID.");
            return;
        }

        const uint32_t button_id = static_cast<uint32_t>(value);

        const bool     exists    = std::any_of(
            data.buttons.begin(),
            data.buttons.end(),
            [button_id](const ButtonData& button) {
                return button.id == button_id;
            }
        );

        if (!exists) {
            os.serial.print(
                "Error: button ID not found.\nMake sure you are removing by ID, not by pin."
            );
            return;
        }

        remove(button_id);

        os.serial.print(
            "Successfully removed button mapping."
        );
    } catch (...) {
        os.serial.print(
            "Error: invalid button ID."
        );
    }
}