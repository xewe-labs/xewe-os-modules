// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/time/src/Time/Config.h
//
// The values of the time module, in the module Config.h form of XeWeCore's doc/config.md: slug
// time, so every name starts with XEWE_TIME_. Pure preprocessor and no includes. Each value is a
// fallback: a line in the project's Config.h (an uncommented first-boot line pre-answers that
// question), a dotenv value behind INJECTED_FROM_DOTENV or `xewe build --define` wins.
#pragma once

// ---- runtime defaults ----

// ---- first-boot values ----

// Timezone offset (time/tz_gmt_str), string literal GMT+HH:MM; detected and confirmed at first boot
#ifndef XEWE_TIME_TZ_GMT_STR
#define XEWE_TIME_TZ_GMT_STR "GMT+00:00"
#define XEWE_TIME_TZ_GMT_STR_PROMPT 1
#endif

// ---- compile-time constants ----
