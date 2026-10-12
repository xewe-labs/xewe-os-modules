// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Config.h
//
// Compile-time values of the led module. Pure preprocessor, no includes: `xewe modules generate`
// copies everything after `#pragma once` into the project's Config.h, which `xewe build` passes to
// every translation unit before this file, so a value set there (or `xewe build --define`) wins.
// The pins are FastLED template arguments, so they cannot be runtime settings. The chip defaults
// are the Led Dock V2 pinout (c3 4/3, c6 20/19, s3 13/12); any other build sets them with
// `xewe build --define XEWE_MODULE_LED_PIN_DATA=<gpio> --define XEWE_MODULE_LED_PIN_CLOCK=<gpio>`.
#pragma once

// GPIO of the LED data line (every chipset).
#ifndef XEWE_MODULE_LED_PIN_DATA
#if defined(XEWE_CHIP_C3)
#define XEWE_MODULE_LED_PIN_DATA 4
#elif defined(XEWE_CHIP_C6)
#define XEWE_MODULE_LED_PIN_DATA 20
#elif defined(XEWE_CHIP_S3)
#define XEWE_MODULE_LED_PIN_DATA 13
#else
#define XEWE_MODULE_LED_PIN_DATA 48
#endif
#endif

// GPIO of the LED clock line (used by the 14 clocked chipsets only).
#ifndef XEWE_MODULE_LED_PIN_CLOCK
#if defined(XEWE_CHIP_C3)
#define XEWE_MODULE_LED_PIN_CLOCK 3
#elif defined(XEWE_CHIP_C6)
#define XEWE_MODULE_LED_PIN_CLOCK 19
#elif defined(XEWE_CHIP_S3)
#define XEWE_MODULE_LED_PIN_CLOCK 12
#else
#define XEWE_MODULE_LED_PIN_CLOCK 12
#endif
#endif

// Largest strip the firmware drives (LEDs). Sizes the pixel and cross-fade buffers; every frame
// sends this many pixels, so max fps is about 1e6 / (value * 30) for one-wire chips.
#ifndef XEWE_MODULE_LED_NUM_LEDS_MAX
#define XEWE_MODULE_LED_NUM_LEDS_MAX 600
#endif

// Cross-fade between two modes (ms).
#ifndef XEWE_MODULE_LED_MODE_FADE_MS
#define XEWE_MODULE_LED_MODE_FADE_MS 900
#endif

// Brightness and on/off fade (ms).
#ifndef XEWE_MODULE_LED_BRIGHTNESS_FADE_MS
#define XEWE_MODULE_LED_BRIGHTNESS_FADE_MS 500
#endif

// Frame period of the render task (ms); 20 ms = 50 fps.
#ifndef XEWE_MODULE_LED_FRAME_MS
#define XEWE_MODULE_LED_FRAME_MS 20
#endif

// Window of the FPS figure in `$led status` (s).
#ifndef XEWE_MODULE_LED_FPS_WINDOW_S
#define XEWE_MODULE_LED_FPS_WINDOW_S 3
#endif

// Capacity of the listener set (other modules notified of colour, brightness, state, mode, length).
#ifndef XEWE_MODULE_LED_LISTENERS
#define XEWE_MODULE_LED_LISTENERS 4
#endif

// Stack of the render task (bytes).
#ifndef XEWE_MODULE_LED_TASK_STACK
#define XEWE_MODULE_LED_TASK_STACK 4096
#endif

// FreeRTOS priority of the render task (loopTask is 1).
#ifndef XEWE_MODULE_LED_TASK_PRIORITY
#define XEWE_MODULE_LED_TASK_PRIORITY 3
#endif

// CPU core of the render task (0 on single-core chips).
#ifndef XEWE_MODULE_LED_TASK_CORE
#define XEWE_MODULE_LED_TASK_CORE 0
#endif
