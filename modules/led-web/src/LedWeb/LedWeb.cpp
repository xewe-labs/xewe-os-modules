// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/LedWeb.cpp

#include "LedWeb.h"

#include <WiFi.h>

#include "pages/index_css.h"
#include "pages/index_html.h"
#include "pages/index_js.h"

namespace {

// a query of the request being handled, as the handlers in LedWebApi.h read it
struct ServerRequest {
    WebServer& server;

    bool has(const char* name) const {
        return server.hasArg(name);
    }
    std::string arg(const char* name) const {
        return server.arg(name).c_str();
    }
    long arg_int(const char* name) const {
        return server.arg(name).toInt();
    }
};

} // namespace

LedWeb::LedWeb(xewe::Os& host, Led& led_ref, WebInterface& web_interface_ref)
    : xewe::Module(host,
          /* id                  */ "led_web",
          /* name                */ "Led Web",
          /* description         */ "Allows to control LED in web browser from\nany local device",
          /* requires_init_setup */ false,
          /* can_be_disabled     */ true,
          /* has_cli_cmds        */ true),
      led(led_ref), web_interface(web_interface_ref) {
    add_requirement(led);
    add_requirement(web_interface);
}

void LedWeb::begin_routines_common() {
    if (started)
        return;
    started = true;

    WebServer& server = web_interface.get_server();
    for (const led_web_api::Route& route : led_web_api::ROUTES) {
        if (route.content == led_web_api::Content::Page)
            web_interface.set_root([this, &route]() { serve(route); });
        else
            server.on(route.path, HTTP_GET, [this, &route]() { serve(route); });
    }

    web_socket.begin();
    web_socket.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
        on_web_socket_event(num, type, payload, length);
    });

    led.listeners.add(this);
}

void LedWeb::loop() {
    web_socket.loop();

    if (heartbeat_due(millis(), XEWE_LED_WEB_HEARTBEAT_MS)) {
        broadcast(led_web_api::FRAME_HEARTBEAT, 1);
        heartbeat_sent(millis());
    }
}

void LedWeb::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    close_pages();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string LedWeb::status(const bool verbose) const {
    std::string out = Module::status(false);
    if (is_enabled()) {
        const xewe::Settings web_settings = web_interface.settings();
        const xewe::SettingDef* port_row = web_settings.find("port");
        out += "\nPage: " +
            led_web_api::page_url(WiFi.localIP().toString().c_str(),
                port_row ? web_settings.value(*port_row) : std::string());
        out += "\nWebSocket Port: " + std::to_string(XEWE_LED_WEB_WS_PORT);
        out += "\nWebSocket Clients: " + std::to_string(client_count());
    }
    if (verbose)
        os.serial.print(out);
    return out;
}

void LedWeb::send_frame(const std::string& frame) {
    broadcast(frame.data(), frame.size());
}

void LedWeb::disconnect_all() {
    if (started)
        web_socket.disconnect();
}

// ---------------------------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------------------------
void LedWeb::serve(const led_web_api::Route& route) {
    if (is_disabled())
        return;
    WebServer& server = web_interface.get_server();
    apply_cors();

    switch (route.content) {
    case led_web_api::Content::Page:
        server.send_P(200, led_web_api::content_type(route.content), INDEX_HTML);
        break;
    case led_web_api::Content::Css:
        server.send_P(200, led_web_api::content_type(route.content), INDEX_CSS);
        break;
    case led_web_api::Content::Js:
        server.send_P(200, led_web_api::content_type(route.content), INDEX_JS);
        break;
    case led_web_api::Content::Set:
        send_response(led_web_api::handle_set(ServerRequest{server}, led, this));
        break;
    case led_web_api::Content::State:
        send_response(led_web_api::state_response(led));
        break;
    case led_web_api::Content::Modes:
        send_response(led_web_api::modes_response(led));
        break;
    case led_web_api::Content::Name:
        send_response(led_web_api::name_response(os.system.get_device_name()));
        break;
    }
}

void LedWeb::send_response(const led_web_api::Response& response) {
    web_interface.get_server().send(response.code, response.type, response.body.c_str());
}

void LedWeb::apply_cors() {
    WebServer& server = web_interface.get_server();
    for (const auto& header : led_web_api::CORS_HEADERS)
        server.sendHeader(header[0], header[1]);
}

// ---------------------------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------------------------
void LedWeb::on_web_socket_event(uint8_t, WStype_t type, uint8_t*, size_t) {
    if (is_disabled())
        return;

    switch (type) {
    case WStype_DISCONNECTED:
        page_disconnected();
        break;
    case WStype_CONNECTED:
        page_connected(led);
        break;
    default: // text from a page is ignored
        break;
    }
}

void LedWeb::broadcast(const char* payload, size_t length) {
    if (is_disabled() || !started)
        return;
    if (length > 0)
        web_socket.broadcastTXT(payload, length);
}
