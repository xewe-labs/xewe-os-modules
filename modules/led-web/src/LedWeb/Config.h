// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/Config.h
//
// Compile-time values of the led-web module. Pure preprocessor, no includes: `xewe modules
// generate` copies everything after `#pragma once` into the project's Config.h, which `xewe build`
// passes to every translation unit before this file, so a value set there (or
// `xewe build --define`) wins.
#pragma once

// TCP port of the WebSocket server that pushes LED changes to the page. The page connects to
// ws://<host>:81/ (the port is written in its script), so change both together.
#ifndef XEWE_MODULE_LED_WEB_WS_PORT
#define XEWE_MODULE_LED_WEB_WS_PORT 81
#endif

// Interval of the "H" heartbeat frame while a page is connected (ms). The page shows Offline and
// reloads itself when no heartbeat arrives for 2200 ms.
#ifndef XEWE_MODULE_LED_WEB_HEARTBEAT_MS
#define XEWE_MODULE_LED_WEB_HEARTBEAT_MS 1000
#endif
