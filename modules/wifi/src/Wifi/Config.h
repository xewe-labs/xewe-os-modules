// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/wifi/src/Wifi/Config.h
/*
The values of the wifi module, in the module Config.h form of XeWeCore's doc/config.md: slug wifi,
so every name starts with XEWE_WIFI_. Pure preprocessor and no includes. Each value is a fallback:
a line in the project's Config.h (an uncommented line of a value asked at first boot pre-answers
that question), a dotenv value behind INJECTED_FROM_DOTENV (the usual way for the password) or
`xewe build --define` wins.
*/
#pragma once

/* ---- start default runtime values ---- */

/* Network name (wifi/ssid), string literal, up to 32 characters; asked at first boot */
#ifndef XEWE_WIFI_SSID
#define XEWE_WIFI_SSID ""
#define XEWE_WIFI_SSID_PROMPT 1
#endif

/* Network password (wifi/psw), string literal, up to 63 characters; asked at first boot */
#ifndef XEWE_WIFI_PSW
#define XEWE_WIFI_PSW ""
#define XEWE_WIFI_PSW_PROMPT 1
#endif

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* Debug output of the module, 0 or 1; 1 prints every step, credentials included: bench only */
#ifndef XEWE_WIFI_DEBUG
#define XEWE_WIFI_DEBUG 0
#endif

/* ---- end compile time defined constants ---- */
