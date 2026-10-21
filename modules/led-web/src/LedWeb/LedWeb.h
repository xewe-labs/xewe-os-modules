// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/LedWeb.h
//
// The LED control page of the led module in a browser. The page, its stylesheet and script and
// the /set, /state, /modes and /name routes are served by the web interface module's server (the
// page takes `/` through set_root); every LED change, from any source, is pushed to the open pages
// as WebSocket text frames on port 81 (WS_PORT). Route and frame formats:
// LedWebApi.h.
#pragma once

#include <WebServer.h>
#include <WebSocketsServer.h>

#include <cstdint>
#include <string>

#include <XeWeCore.h>

#include "../Led/Led.h"
#include "../WebInterface/WebInterface.h"
#include "Config.h"
#include "LedWebApi.h"

class LedWeb : public xewe::Module, public led_web_api::PageSessions {
public:
    // WebSocket port; the page script connects to :81, so the two stay equal
    static constexpr uint16_t WS_PORT = 81;
    // "H" interval, ms; the page shows Offline after 2200 ms, so this stays under half of that
    static constexpr uint32_t HEARTBEAT_MS = 1000;

    LedWeb(xewe::Os& host, Led& led_ref, WebInterface& web_interface_ref);

    // routes on the web interface server, `/` through set_root, WebSocket server, led listener
    void begin_routines_common() override;
    // WebSocket clients and the heartbeat
    void loop() override;
    // disconnects every page, then the core reset (no stored state of its own)
    void reset(const bool verbose = false, const bool do_restart = true,
        const bool keep_enabled = true) override;
    // `Led Web module enabled|disabled`, then the page address and the WebSocket clients
    std::string status(const bool verbose = false) const override;

    // PageSessions: broadcast one frame to every connected page; close every connection
    void send_frame(const std::string& frame) override;
    void disconnect_all() override;

private:
    Led& led;
    WebInterface& web_interface;
    WebSocketsServer web_socket{WS_PORT};

    bool started{false};

    void serve(const led_web_api::Route& route);
    void send_response(const led_web_api::Response& response);
    void apply_cors();
    void on_web_socket_event(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    void broadcast(const char* payload, size_t length);
};
