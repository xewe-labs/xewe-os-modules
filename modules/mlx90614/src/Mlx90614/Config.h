// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/mlx90614/src/Mlx90614/Config.h
//
// Compile-time values of the mlx90614 module. Pure preprocessor, no includes: `xewe modules generate`
// copies everything after `#pragma once` into the project's Config.h, which `xewe build` passes to
// every translation unit before this file, so a value set there wins. Mlx90614.h includes this file
// for the defaults.
#pragma once

// Listener slots of the temperature fan-out (xewe::ListenerSet, no heap).
#ifndef XEWE_MODULE_MLX90614_LISTENERS_MAX
#define XEWE_MODULE_MLX90614_LISTENERS_MAX 4
#endif
