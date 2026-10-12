// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/host/XeWeCore.h
//
// Host stand-in for <XeWeCore.h> for the led engine tests: the module's engine headers include the
// umbrella header, which on a device pulls in ESP-IDF. On the host only the XeWeCore utilities the
// engine uses are needed, and they are host-includable.
#pragma once

#include <XeWeCore/Utils/AsyncTimer.h>
#include <XeWeCore/Utils/Color.h>
#include <XeWeCore/Utils/Listeners.h>
#include <XeWeCore/Utils/Span.h>
