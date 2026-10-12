// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/ModeRegistry.h
//
// Mode ids and their factories. Ids are stored on devices (`mode_id`, mode parameters) and in
// button and schedule command strings, so an id is never renumbered.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "modes/Mode.h"

using ModeFactory = std::function<std::unique_ptr<Mode>(const std::map<std::string, uint16_t>&)>;

class ModeRegistry {
public:
    // id -> factory, ids 0..6 in id order. An explicit table: a self-registering static object in
    // each mode's .cpp would be dropped by the linker, because nothing references it.
    static std::map<uint8_t, ModeFactory>& get_registry();
};
