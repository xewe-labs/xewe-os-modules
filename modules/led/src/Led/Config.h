// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Config.h
//
// Compile-time values of the led module. Pure preprocessor, no includes: `xewe modules generate`
// copies everything after `#pragma once` into the project's Config.h, which `xewe build` passes to
// every translation unit before this file, so a value set there wins. Led.h and LedListener.h
// include this file for the defaults.
#pragma once

// Data GPIO: FastLED's template pin for the built-in driver and for APA102; the default of the
// `pin_data` setting. XEWE_CHIP_* comes from the generated XeWeBuildInfo.h.
#ifndef XEWE_MODULE_LED_PIN_DATA
#  if defined(XEWE_CHIP_S3)
#    define XEWE_MODULE_LED_PIN_DATA 48      // S3 SuperMini / DevKitC-1 v1.0 on-board WS2812B
#  else
#    define XEWE_MODULE_LED_PIN_DATA 8       // C6 SuperMini on-board WS2812B; C3: any free GPIO
#  endif
#endif
// Clock GPIO (APA102 only); the default of the `pin_clock` setting.
#ifndef XEWE_MODULE_LED_PIN_CLOCK
#  if defined(XEWE_CHIP_S3)
#    define XEWE_MODULE_LED_PIN_CLOCK 12
#  elif defined(XEWE_CHIP_C6)
#    define XEWE_MODULE_LED_PIN_CLOCK 21
#  else
#    define XEWE_MODULE_LED_PIN_CLOCK 4
#  endif
#endif
// Pixel buffer size in LEDs (6 bytes of RAM per LED); `$led set num_led` accepts 1..this.
#ifndef XEWE_MODULE_LED_NUM_LEDS_MAX
#define XEWE_MODULE_LED_NUM_LEDS_MAX 2000
#endif
// Listener slots, no heap; an LED firmware typically needs 4 (web, HomeKit, Alexa, Home Assistant).
#ifndef XEWE_MODULE_LED_LISTENERS_MAX
#define XEWE_MODULE_LED_LISTENERS_MAX 6
#endif
