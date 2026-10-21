// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/Config.h
/*
The values of the led-web module, in the module Config.h form of XeWeCore's doc/config.md:
slug led-web, so every name starts with XEWE_LED_WEB_. Pure preprocessor and no includes. Each
value is a fallback: a line in the project's Config.h, a dotenv value behind
INJECTED_FROM_DOTENV or `xewe build --define` wins.
*/
#pragma once

/* ---- start default runtime values ---- */

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* WebSocket server TCP port; the page script connects to :81, so change both together */
#ifndef XEWE_LED_WEB_WS_PORT
#define XEWE_LED_WEB_WS_PORT 81
#endif

/* "H" heartbeat interval while a page is connected, ms; the page goes Offline after 2200 ms */
#ifndef XEWE_LED_WEB_HEARTBEAT_MS
#define XEWE_LED_WEB_HEARTBEAT_MS 1000
#endif

/* ---- end compile time defined constants ---- */
