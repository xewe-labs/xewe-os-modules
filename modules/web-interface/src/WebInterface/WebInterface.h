// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/web-interface/src/WebInterface/WebInterface.h
#pragma once

#include <WebServer.h>
#include <string>
#include <sstream>
#include <iomanip>

#include <XeWeCore.h>
#include "../Wifi/Wifi.h"


class WebInterface : public xewe::Module {
public:
                                WebInterface                (xewe::Os&                   host,
                                                             Wifi&                        wifi_ref);

    void                        begin_routines_regular      ()       override;

    void                        loop                        ()                              override;
    std::string                 status                      (const bool verbose=false)      const override;

    WebServer&                  get_server                  ()                              { return http_server; }
private:
    Wifi&                       wifi;
    WebServer                   http_server                  {80};

    void                        serve_main_page               ();
    void                        handle_command_request        ();

    static const char           INDEX_HTML                  [] PROGMEM;
};
