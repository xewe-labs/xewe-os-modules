// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Config.h
/*
The values of the led module, in the module Config.h form of XeWeCore's doc/config.md: slug led,
so every name starts with XEWE_LED_. Pure preprocessor and no includes. Each value is a fallback:
a line in the project's Config.h (an uncommented line of a value asked at first boot pre-answers
that question), a dotenv value behind INJECTED_FROM_DOTENV or `xewe build --define` wins.
The pins are FastLED template arguments, so they are compile-time constants; the defaults are 4
and 3 on every chip, and a release image or a project sets its own (the Led Dock V2 pins per chip
are in the README).
*/
#pragma once

/* ---- start default runtime values ---- */

/* LED has a clock line (led/use_clk), 0 or 1; asked at first boot */
#ifndef XEWE_LED_USE_CLK
#define XEWE_LED_USE_CLK 0
#define XEWE_LED_USE_CLK_PROMPT 1
#endif

/* LED chipset id (led/chip), 0 to 45 (README chipset table); asked at first boot */
#ifndef XEWE_LED_CHIP
#define XEWE_LED_CHIP 0
#define XEWE_LED_CHIP_PROMPT 1
#endif

/* Strip voltage (led/voltage), V, 5, 12 or 24; asked at first boot */
#ifndef XEWE_LED_VOLTAGE
#define XEWE_LED_VOLTAGE 5
#define XEWE_LED_VOLTAGE_PROMPT 1
#endif

/* Parallel strips (led/lines), 0 = one strip, to NUM_LEDS_MAX; asked at first boot */
#ifndef XEWE_LED_LINES
#define XEWE_LED_LINES 0
#define XEWE_LED_LINES_PROMPT 1
#endif

/* Number of LEDs, per line when lines > 0 (led/num_led), 0 to NUM_LEDS_MAX; asked at first boot */
#ifndef XEWE_LED_NUM_LED
#define XEWE_LED_NUM_LED 600
#define XEWE_LED_NUM_LED_PROMPT 1
#endif

/* Colour order (led/colorder), 0 to 5 = RGB RBG GRB GBR BRG BGR; asked at first boot */
#ifndef XEWE_LED_COLORDER
#define XEWE_LED_COLORDER 0
#define XEWE_LED_COLORDER_PROMPT 1
#endif

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* GPIO of the LED data line (every chipset); release images set it per board, see the README */
#ifndef XEWE_LED_PIN_DATA
#define XEWE_LED_PIN_DATA 4
#endif

/* GPIO of the LED clock line (used by the 14 clocked chipsets only) */
#ifndef XEWE_LED_PIN_CLOCK
#define XEWE_LED_PIN_CLOCK 3
#endif

/* Largest strip the firmware drives, LEDs; sizes the buffers, max fps about 1e6 / (value * 30) */
#ifndef XEWE_LED_NUM_LEDS_MAX
#define XEWE_LED_NUM_LEDS_MAX 600
#endif

/* Cross-fade between two modes, ms */
#ifndef XEWE_LED_MODE_FADE_MS
#define XEWE_LED_MODE_FADE_MS 900
#endif

/* Brightness and on/off fade, ms */
#ifndef XEWE_LED_BRIGHTNESS_FADE_MS
#define XEWE_LED_BRIGHTNESS_FADE_MS 500
#endif

/* Frame period of the render task, ms; 20 ms = 50 fps */
#ifndef XEWE_LED_FRAME_MS
#define XEWE_LED_FRAME_MS 20
#endif

/* Window of the FPS figure in `$led status`, s */
#ifndef XEWE_LED_FPS_WINDOW_S
#define XEWE_LED_FPS_WINDOW_S 3
#endif

/* Capacity of the listener set (colour, brightness, state, mode and length events) */
#ifndef XEWE_LED_LISTENERS
#define XEWE_LED_LISTENERS 4
#endif

/* Stack of the render task, bytes */
#ifndef XEWE_LED_TASK_STACK
#define XEWE_LED_TASK_STACK 4096
#endif

/* FreeRTOS priority of the render task (loopTask is 1) */
#ifndef XEWE_LED_TASK_PRIORITY
#define XEWE_LED_TASK_PRIORITY 3
#endif

/* CPU core of the render task (0 on single-core chips) */
#ifndef XEWE_LED_TASK_CORE
#define XEWE_LED_TASK_CORE 0
#endif

/* ---- end compile time defined constants ---- */
