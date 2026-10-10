// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/web-interface/src/WebInterface/WebInterface.h
#pragma once

#include <WebServer.h>
#include <functional>
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

    // settings table: `port` (u16, RESTART) and `root` (str <= 31: GET / redirects there, "" = console)
    xewe::Settings              settings                    ()                              const override;

    // Who owns `/` (the console is always at /console). Precedence: a handler set here (RAM only, set it
    // from setup() before os.begin()), then the `root` setting (a 302 redirect), then the console page.
    void                        set_root                    (std::function<void()> handler) { root_handler = std::move(handler); }
    // persisted form: `$web_interface set root /pad`; false when the path does not start with '/'
    bool                        redirect_root               (const std::string& path);

private:
    Wifi&                       wifi;
    WebServer                   http_server                  {80};
    uint16_t                    port                         {80};       // table row; applied at begin
    std::string                 root                         ;           // table row
    std::function<void()>       root_handler                 ;

    void                        serve_root                    ();
    void                        serve_main_page               ();
    void                        handle_command_request        ();

    static const char           INDEX_HTML                  [] PROGMEM;
};
