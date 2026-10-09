// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/LedWeb.h
//
// The xewe-led-os 2.3.x LED web page, served at /led on the web-interface module's server
// (web_interface.get_server(), the same pattern as the cooling pad's /pad page). JSON routes under
// /led/api call the public setters of led-strip and led-modes, the same code paths as `$led ...`
// and `$led_modes ...`. Changes are pushed to the page as server-sent events (GET /led/api/events,
// one `state` event per change, at most LED_WEB_SSE_CLIENTS streams) on the same WebServer: led-web
// is a LedListener of led-strip/led-modes. The page falls back to polling GET /led/api/state every
// 2 s when the stream closes. The 2.3.x WebSocket push needed the WebSockets fork (LM8).
#pragma once

#include <WebServer.h>
#include <cstdint>
#include <string>

#include <XeWeCore.h>
#include "../WebInterface/WebInterface.h"
#include "../LedStrip/LedStrip.h"
#include "../LedModes/LedModes.h"

#ifndef LED_WEB_SSE_CLIENTS
#define LED_WEB_SSE_CLIENTS 2               // open /led/api/events streams; a third gets 503 and polls
#endif

class LedWeb : public xewe::Module, public LedListener {
public:
                       LedWeb                  (xewe::Os&     host,
                                                WebInterface& web_interface_ref,
                                                LedStrip&     led_strip_ref,
                                                LedModes&     led_modes_ref);

    void               begin_routines_regular  ()                                  override;
    void               loop                    ()                                  override;   // SSE pushes, keep-alive
    std::string        status                  (const bool verbose = false) const  override;

    // LedListener (main loop): mark every stream pending; changes made by led-web itself (origin ==
    // this) are skipped here and pushed by the POST handler to the other streams only
    void               on_brightness           (uint8_t value, const void* origin)  override;
    void               on_state                (bool on, const void* origin)        override;
    void               on_mode                 (uint8_t mode_id, const void* origin) override;
    void               on_color                (uint32_t rrggbb, const void* origin) override;
    void               on_param                (uint8_t mode_id, const char* key, uint16_t value,
                                                const void* origin)                 override;

    std::string        get_url                 () const;      // http://<ip>/led
    std::string        state_json              () const;      // body of GET /led/api/state
    std::string        modes_json              () const;      // body of GET /led/api/modes

private:
    WebInterface&      web_interface;
    LedStrip&          led_strip;
    LedModes&          led_modes;

    WebServer*         server                  = nullptr;
    bool               routes_attached         = false;
    uint32_t           api_requests            = 0;
    uint32_t           api_errors              = 0;
    bool               listening               = false;

    // One open event stream. The NetworkClient copy shares the socket with the server's client, so
    // the connection stays open after handleClient() lets go of it (it never waits on this socket).
    struct SseClient {
        NetworkClient  client;
        char           id[17]                  = "";        // the page's random id (?client=)
        bool           active                  = false;
        bool           pending                 = false;     // a state event is due
    };
    SseClient          sse[LED_WEB_SSE_CLIENTS];
    uint32_t           sse_keepalive_ms        = 0;
    uint32_t           sse_events              = 0;
    uint32_t           sse_rejected            = 0;

    void               handle_events           ();
    void               mark_pending            (const void* origin);
    void               push_to_others          ();                                // after a POST from the page
    bool               sse_write               (SseClient& slot, const char* text, size_t length);
    void               sse_drop                (SseClient& slot);
    uint8_t            sse_count               () const;

    void               attach_routes           ();
    void               send_json               (int code, const std::string& body);
    void               send_error              (const char* message);
    bool               arg_long                (const char* name, long min, long max, long& out);

    void               handle_brightness       ();
    void               handle_power            ();
    void               handle_mode             ();
    void               handle_param            ();
    void               handle_color            ();
    void               handle_reset_params     ();
};
