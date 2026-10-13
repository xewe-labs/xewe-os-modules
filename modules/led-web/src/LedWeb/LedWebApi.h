// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/LedWebApi.h
//
// The HTTP and WebSocket surface of the LED page without the server: the route table, the CORS
// headers, the /set and /state handlers, the status page address, the WebSocket frames and the
// page sessions (client count, heartbeat, frame on connect, closing). Standard library and
// host-includable core headers only, so host tests run the same code the firmware runs.
//
// The page (pages/index_js.h) is the client of everything here; its parser fixes the formats:
//   frames  H | C<RRGGBB> | B<u> | S0|S1 | M<u> | P<key>:<u> | F<RRGGBB>,<bri>,<0|1>,<mode>
//   /state  the F frame of the current values
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include <XeWeCore/Utils/Color.h>

#include "../Led/LedListener.h"

namespace led_web_api {

// ---------------------------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------------------------

enum class Content : uint8_t {
    Page, // INDEX_HTML, text/html
    Css, // INDEX_CSS, text/css
    Js, // INDEX_JS, application/javascript
    Set, // handle_set
    State, // state_body
    Modes, // the led's modes JSON, application/json
    Name, // the device name, text/plain
};

struct Route {
    const char* path;
    const char* method;
    // query parameters the handler reads, in the order it checks them ("" = none)
    const char* params;
    Content content;
};

// Every route answers GET only. "/" is served through the web interface's set_root, not registered
// here. The two %7B%7B ... %7D%7D paths are the URL-encoded {{ url_for(...) }} template links; they
// serve the same stylesheet and script.
inline constexpr Route ROUTES[] = {
    {"/", "GET", "", Content::Page},
    {"/set", "GET", "color,brightness,state,mode_id,reset_params,param+val", Content::Set},
    {"/state", "GET", "", Content::State},
    {"/modes", "GET", "", Content::Modes},
    {"/name", "GET", "", Content::Name},
    {"/index.css", "GET", "", Content::Css},
    {"/index.js", "GET", "", Content::Js},
    {"/static/index.css", "GET", "", Content::Css},
    {"/static/index.js", "GET", "", Content::Js},
    {"/%7B%7B%20url_for('static',%20filename='index.css')%20%7D%7D", "GET", "", Content::Css},
    {"/%7B%7B%20url_for('static',%20filename='index.js')%20%7D%7D", "GET", "", Content::Js},
};
inline constexpr std::size_t ROUTE_COUNT = sizeof(ROUTES) / sizeof(ROUTES[0]);

// Sent before every answer of these routes.
inline constexpr const char* CORS_HEADERS[3][2] = {
    {"Access-Control-Allow-Origin", "*"},
    {"Access-Control-Allow-Methods", "POST, GET, OPTIONS, DELETE, PUT"},
    {"Access-Control-Allow-Headers", "Content-Type"},
};

inline const char* content_type(Content content) {
    switch (content) {
    case Content::Page:
        return "text/html";
    case Content::Css:
        return "text/css";
    case Content::Js:
        return "application/javascript";
    case Content::Modes:
        return "application/json";
    case Content::Set:
    case Content::State:
    case Content::Name:
        return "text/plain";
    }
    return "text/plain";
}

struct Response {
    int code;
    const char* type;
    std::string body;
};

// ---------------------------------------------------------------------------------------------
// Frames (WebSocket text frames; the F frame is also the /state body)
// ---------------------------------------------------------------------------------------------

inline constexpr const char* FRAME_HEARTBEAT = "H";

inline std::string frame_color(const std::array<uint8_t, 3>& rgb) {
    char buf[8];
    const int n = std::snprintf(buf, sizeof(buf), "C%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    return std::string(buf, static_cast<std::size_t>(n));
}

inline std::string frame_brightness(uint8_t brightness) {
    char buf[6];
    const int n = std::snprintf(buf, sizeof(buf), "B%u", static_cast<unsigned>(brightness));
    return std::string(buf, static_cast<std::size_t>(n));
}

inline std::string frame_state(bool on) {
    return on ? "S1" : "S0";
}

inline std::string frame_mode(uint8_t mode_id) {
    char buf[6];
    const int n = std::snprintf(buf, sizeof(buf), "M%u", static_cast<unsigned>(mode_id));
    return std::string(buf, static_cast<std::size_t>(n));
}

// the key is truncated so that the frame fits in 63 characters (a 64-byte buffer)
inline std::string frame_param(std::string_view key, uint16_t value) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "P%.*s:%u", static_cast<int>(key.size()),
        key.data(), static_cast<unsigned>(value));
    const std::size_t len = n < 0
        ? 0
        : (static_cast<std::size_t>(n) < sizeof(buf) ? static_cast<std::size_t>(n)
                                                     : sizeof(buf) - 1);
    return std::string(buf, len);
}

inline std::string frame_all(
    const std::array<uint8_t, 3>& rgb, uint8_t brightness, bool on, uint8_t mode_id) {
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf), "F%02X%02X%02X,%u,%u,%u", rgb[0], rgb[1], rgb[2],
        static_cast<unsigned>(brightness), on ? 1u : 0u, static_cast<unsigned>(mode_id));
    return std::string(buf, static_cast<std::size_t>(n));
}

// the F frame of the led's current values: colour, last non-zero brightness, state and mode id
template <typename LedT> std::string frame_current(const LedT& led) {
    return frame_all(
        led.get_rgb(), led.get_brightness(), led.get_state(), led.get_current_mode_id());
}

// ---------------------------------------------------------------------------------------------
// Handlers. `Request` provides has(name), arg(name) -> std::string and arg_int(name) -> long
// (Arduino String::toInt on the firmware); `LedT` is the led module (or a host fake) with its
// public sync_* setters and getters.
// ---------------------------------------------------------------------------------------------

inline constexpr const char* SET_OK = "OK";
inline constexpr const char* SET_BAD_COLOR = "Invalid color";

// GET /set: exactly one change, the first present of color, brightness, state, mode_id,
// reset_params, param+val. `color` is six hex digits (an optional leading '#'); anything else is
// rejected with 400 and nothing changes. The numbers narrow like the CLI's (u8 modulo 256, u16
// modulo 65536); `state` is on only for 1. No or unknown arguments: 200 OK, nothing changes.
template <typename Request, typename LedT>
Response handle_set(const Request& req, LedT& led, const void* origin) {
    if (req.has("color")) {
        std::array<uint8_t, 3> rgb{};
        if (!xewe::color::parse_hex_color(req.arg("color"), rgb[0], rgb[1], rgb[2]))
            return {400, "text/plain", SET_BAD_COLOR};
        led.sync_color(rgb, origin);
    } else if (req.has("brightness")) {
        led.sync_brightness(static_cast<uint8_t>(req.arg_int("brightness")), origin);
    } else if (req.has("state")) {
        led.sync_state(req.arg_int("state") == 1, origin);
    } else if (req.has("mode_id")) {
        led.sync_mode(static_cast<uint8_t>(req.arg_int("mode_id")), origin);
    } else if (req.has("reset_params")) {
        led.reset_current_mode();
    } else if (req.has("param") && req.has("val")) {
        led.set_mode_param(req.arg("param"), static_cast<uint16_t>(req.arg_int("val")));
    }
    return {200, "text/plain", SET_OK};
}

// GET /state: the F frame of the live colour, the last non-zero brightness, state and mode id
template <typename LedT> Response state_response(const LedT& led) {
    return {200, "text/plain", frame_current(led)};
}

// GET /modes
template <typename LedT> Response modes_response(const LedT& led) {
    return {200, "application/json", led.get_all_modes_json()};
}

// GET /name
inline Response name_response(const std::string& device_name) {
    return {200, "text/plain", device_name};
}

// The page address `status` prints: http://<ip>/, with :<port> when the web interface's HTTP port
// (its `port` setting as text) is not 80, the same rule as the web interface's boot banner.
inline std::string page_url(const std::string& ip, const std::string& http_port) {
    const bool default_port = http_port.empty() || http_port == "80";
    return "http://" + ip + (default_port ? std::string() : ":" + http_port) + "/";
}

// ---------------------------------------------------------------------------------------------
// The led's change events as frames. Every event is sent, also the ones this page caused (the
// page relies on the echo of its own changes); the length is not shown on the page.
// ---------------------------------------------------------------------------------------------

class FrameListener : public LedListener {
public:
    // one text frame to every connected page
    virtual void send_frame(const std::string& frame) = 0;

    void on_color(std::array<uint8_t, 3> rgb, const void*) override {
        send_frame(frame_color(rgb));
    }
    void on_brightness(uint8_t brightness, const void*) override {
        send_frame(frame_brightness(brightness));
    }
    void on_state(bool on, const void*) override {
        send_frame(frame_state(on));
    }
    void on_mode(uint8_t mode_id, const void*) override {
        send_frame(frame_mode(mode_id));
    }
    void on_length(uint16_t, const void*) override {}
    // `$led reset` and the like: one F frame
    void on_all(std::array<uint8_t, 3> rgb, uint8_t brightness, bool on, uint8_t mode_id, uint16_t,
        const void*) override {
        send_frame(frame_all(rgb, brightness, on, mode_id));
    }
};

// ---------------------------------------------------------------------------------------------
// The open pages on the WebSocket: how many are connected, the heartbeat while any is, the
// current values to every page when one connects, and closing them all. The server glue
// (LedWeb.cpp) forwards the connect and disconnect events and implements disconnect_all.
// ---------------------------------------------------------------------------------------------

class PageSessions : public FrameListener {
public:
    // closes every WebSocket connection
    virtual void disconnect_all() = 0;

    // a page connected: it is counted, then the current values go to every page (the new one
    // included) as one F frame
    template <typename LedT> void page_connected(const LedT& led) {
        clients++;
        send_frame(frame_current(led));
    }
    // a page disconnected; the count never goes below 0
    void page_disconnected() {
        if (clients > 0)
            clients--;
    }
    // every page is disconnected and the count is 0 afterwards
    void close_pages() {
        disconnect_all();
        clients = 0;
    }

    // a heartbeat is due while a page is connected and interval_ms have passed since the last one
    // (the first one counts from 0)
    bool heartbeat_due(uint32_t now_ms, uint32_t interval_ms) const {
        return clients && (now_ms - last_heartbeat_ms >= interval_ms);
    }
    void heartbeat_sent(uint32_t now_ms) {
        last_heartbeat_ms = now_ms;
    }

    uint8_t client_count() const {
        return clients;
    }

private:
    uint8_t clients{0};
    uint32_t last_heartbeat_ms{0};
};

} // namespace led_web_api
