// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/web-interface/src/WebInterface/Config.h
/*
The values of the web-interface module, in the module Config.h form of XeWeCore's
doc/config.md: slug web-interface, so every name starts with XEWE_WEB_INTERFACE_. Pure
preprocessor and no includes. Each value is a fallback: a line in the project's Config.h, a
dotenv value behind INJECTED_FROM_DOTENV or `xewe build --define` wins.
*/
#pragma once

/* ---- start default runtime values ---- */

/* HTTP port (web_interface/port), 1 to 65535; takes effect after a restart */
#ifndef XEWE_WEB_INTERFACE_PORT
#define XEWE_WEB_INTERFACE_PORT 80
#endif

/* Path GET / redirects to (web_interface/root), string, up to 31 characters, "" = console */
#ifndef XEWE_WEB_INTERFACE_ROOT
#define XEWE_WEB_INTERFACE_ROOT ""
#endif

/* ---- end default runtime values ---- */

/* ---- start compile time defined constants ---- */

/* ---- end compile time defined constants ---- */
