// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/wifi/src/Wifi/Config.h
//
// Compile-time values of the wifi module. Pure preprocessor, no includes: `xewe modules generate`
// copies everything after `#pragma once` into the project's Config.h, which `xewe build` passes to
// every translation unit before this file, so a value set there wins. Wifi.h includes this file
// for the defaults.
#pragma once

// Debug output of the module (0 or 1). 1 prints every step, credentials included: bench only.
#ifndef XEWE_MODULE_WIFI_DEBUG
#define XEWE_MODULE_WIFI_DEBUG 0
#endif
