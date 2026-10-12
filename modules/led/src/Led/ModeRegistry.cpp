// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/ModeRegistry.cpp

#include "ModeRegistry.h"

#include "modes/ChristmasLights.h"
#include "modes/FadeBrightness.h"
#include "modes/FadeColor.h"
#include "modes/FadeColorTwoZone.h"
#include "modes/Pulse.h"
#include "modes/Rainbow.h"
#include "modes/Solid.h"

namespace {
template <typename T> ModeFactory factory_of() {
    return
        [](const std::map<std::string, uint16_t>& params) { return std::make_unique<T>(params); };
}
} // namespace

std::map<uint8_t, ModeFactory>& ModeRegistry::get_registry() {
    static std::map<uint8_t, ModeFactory> registry = {
        {0, factory_of<Solid>()},
        {1, factory_of<FadeColor>()},
        {2, factory_of<FadeColorTwoZone>()},
        {3, factory_of<FadeBrightness>()},
        {4, factory_of<Pulse>()},
        {5, factory_of<Rainbow>()},
        {6, factory_of<ChristmasLights>()},
    };
    return registry;
}
