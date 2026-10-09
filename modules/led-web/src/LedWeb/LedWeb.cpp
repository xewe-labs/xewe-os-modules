// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/src/LedWeb/LedWeb.cpp

#include "LedWeb.h"

#include <WiFi.h>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>

// page assets, ported from xewe-led-os 2.3.x (see the header comment of each file for the edits)
#include "index_html.h"
#include "index_css.h"
#include "index_js.h"

namespace {

constexpr unsigned POLL_INTERVAL_S = 2;     // must match POLL_MS in index_js.h (fallback when the stream closes)
constexpr uint32_t SSE_KEEPALIVE_MS = 15000; // comment line on every open stream

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// the whole string must be a decimal integer
bool parse_long(const String& text, long& out) {
    if (text.length() == 0) return false;
    char* end = nullptr;
    errno     = 0;
    out       = std::strtol(text.c_str(), &end, 10);
    return errno == 0 && end != nullptr && *end == '\0';
}

// rrggbb, with or without a leading '#'
bool parse_hex_color(String text, LedRgb& out) {
    if (text.startsWith("#")) text = text.substring(1);
    if (text.length() != 6) return false;
    char*               end   = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') return false;
    out.r = static_cast<uint8_t>((value >> 16) & 0xFF);
    out.g = static_cast<uint8_t>((value >> 8) & 0xFF);
    out.b = static_cast<uint8_t>(value & 0xFF);
    return true;
}

}  // namespace


LedWeb::LedWeb(xewe::Os&     host,
               WebInterface& web_interface_ref,
               LedStrip&     led_strip_ref,
               LedModes&     led_modes_ref)
    : xewe::Module(host,
          /* id                  */ "led_web",
          /* name                */ "Led Web",
          /* description         */ "LED control page at /led on the web interface (2.3.x page, JSON API)",
          /* requires_init_setup */ false,
          /* can_be_disabled     */ false,     // true would add a first-boot y/n prompt (LM5), as in web-interface
          /* has_cli_commands    */ true)
    , web_interface(web_interface_ref)
    , led_strip(led_strip_ref)
    , led_modes(led_modes_ref)
{
    add_requirement(web_interface);
    add_requirement(led_strip);
    add_requirement(led_modes);

    register_command({"url", "Print the address of the LED page", "$led_web url", 0,
                      [this](xewe::span<const std::string>) {
                          const bool connected = WiFi.status() == WL_CONNECTED;
                          os.serial.print("Led Web: " + get_url() +
                                          (connected ? std::string() : std::string(" (WiFi not connected)")));
                      }});
}

void LedWeb::begin_routines_regular() {
    attach_routes();
    listening = led_strip.add_listener(this);
    if (!listening) os.serial.print("Led Web: led-strip listener table full; the page will poll");
}

std::string LedWeb::status(const bool verbose) const {
    std::string out = Module::status(false);
    if (is_enabled()) {
        char buf[352];
        std::snprintf(buf, sizeof(buf),
                      "\n  Page:     %s\n  Routes:   %s\n"
                      "  Sync:     server-sent events at /led/api/events (%u/%u streams, %lu events, %lu refused%s);"
                      " poll every %u s when the stream closes\n"
                      "  API:      %lu requests, %lu rejected",
                      get_url().c_str(),
                      routes_attached ? "attached to the web interface server" : "not attached",
                      static_cast<unsigned>(sse_count()), static_cast<unsigned>(LED_WEB_SSE_CLIENTS),
                      static_cast<unsigned long>(sse_events), static_cast<unsigned long>(sse_rejected),
                      listening ? "" : ", not a listener",
                      POLL_INTERVAL_S,
                      static_cast<unsigned long>(api_requests),
                      static_cast<unsigned long>(api_errors));
        out += buf;
    }
    if (verbose) os.serial.print(out);
    return out;
}

std::string LedWeb::get_url() const {
    return std::string("http://") + WiFi.localIP().toString().c_str() + "/led";
}

// {"name":..,"on":bool,"brightness":0-255,"mode":id,"mode_name":..,"color":"RRGGBB",
//  "params":{"<key>":value,...},"length":n,"fps":n}
std::string LedWeb::state_json() const {
    const uint8_t          mode_id = led_modes.get_mode();
    const led_fx::ModeDef* mode    = led_fx::find_mode(mode_id);

    std::string params = "{";
    if (mode != nullptr) {
        for (uint8_t i = 0; i < mode->param_count; ++i) {
            const std::string name  = mode->params[i].key;
            const uint16_t    value = led_modes.get_param(mode_id, name);
            if (i) params += ',';
            params += "\"" + name + "\":" + std::to_string(value);
        }
    }
    params += '}';

    char color[8];   // LedModes::get_color(): the `Color:` line of `$led_modes status`
    std::snprintf(color, sizeof(color), "%06lX", static_cast<unsigned long>(led_modes.get_color() & 0xFFFFFFu));

    std::string out = "{\"name\":\"" + json_escape(os.system.get_device_name()) + "\"";
    out += std::string(",\"on\":") + (led_strip.get_state() ? "true" : "false");
    out += ",\"brightness\":" + std::to_string(led_strip.get_brightness());
    out += ",\"mode\":" + std::to_string(mode_id);
    out += ",\"mode_name\":\"" + json_escape(mode != nullptr ? mode->name : "") + "\"";
    out += std::string(",\"color\":\"") + color + "\"";
    out += ",\"params\":" + params;
    out += ",\"length\":" + std::to_string(led_strip.get_length());
    out += ",\"fps\":" + std::to_string(led_strip.get_fps());
    out += '}';
    return out;
}

// [{"id":0,"name":"Solid","params":[{"key":"hue","display_name":"Hue","min":0,"max":255,"step":1,
//   "default_value":0,"value":12,"type":"b"},...]},...]   (the 2.3.x /modes shape the page reads)
std::string LedWeb::modes_json() const {
    std::string out = "[";
    for (uint8_t m = 0; m < led_fx::MODE_COUNT; ++m) {
        const led_fx::ModeDef& mode = led_fx::MODES[m];
        if (m) out += ',';
        out += "{\"id\":" + std::to_string(mode.id) + ",\"name\":\"" + json_escape(mode.name) + "\",\"params\":[";
        for (uint8_t i = 0; i < mode.param_count; ++i) {
            const led_fx::ParamDef& p = mode.params[i];
            char nums[112];
            std::snprintf(nums, sizeof(nums),
                          ",\"min\":%u,\"max\":%u,\"step\":%u,\"default_value\":%u,\"value\":%u,\"type\":\"%c\"}",
                          static_cast<unsigned>(p.min_value), static_cast<unsigned>(p.max_value),
                          static_cast<unsigned>(p.step), static_cast<unsigned>(p.default_value),
                          static_cast<unsigned>(led_modes.get_param(mode.id, p.key)), p.type);
            if (i) out += ',';
            out += std::string("{\"key\":\"") + p.key + "\",\"display_name\":\"" + json_escape(p.display) + "\"" + nums;
        }
        out += "]}";
    }
    out += ']';
    return out;
}

// =============================================================================
// Routes
// =============================================================================
void LedWeb::attach_routes() {
    if (routes_attached) return;
    server = &web_interface.get_server();

    // page and assets
    server->on("/led", HTTP_GET, [this]() { server->send_P(200, "text/html", LED_WEB_INDEX_HTML); });
    server->on("/led/index.css", HTTP_GET, [this]() { server->send_P(200, "text/css", LED_WEB_INDEX_CSS); });
    server->on("/led/index.js", HTTP_GET, [this]() {
        server->send_P(200, "application/javascript", LED_WEB_INDEX_JS);
    });

    // read
    server->on("/led/api/state", HTTP_GET, [this]() { send_json(200, state_json()); });
    server->on("/led/api/modes", HTTP_GET, [this]() { send_json(200, modes_json()); });
    server->on("/led/api/events", HTTP_GET, [this]() { handle_events(); });

    // write: form fields (or query arguments); each calls the setter its CLI command calls
    server->on("/led/api/brightness",   HTTP_POST, [this]() { handle_brightness(); });
    server->on("/led/api/power",        HTTP_POST, [this]() { handle_power(); });
    server->on("/led/api/mode",         HTTP_POST, [this]() { handle_mode(); });
    server->on("/led/api/param",        HTTP_POST, [this]() { handle_param(); });
    server->on("/led/api/color",        HTTP_POST, [this]() { handle_color(); });
    server->on("/led/api/reset_params", HTTP_POST, [this]() { handle_reset_params(); });

    routes_attached = true;
}

void LedWeb::send_json(int code, const std::string& body) {
    ++api_requests;
    if (code != 200) ++api_errors;
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Cache-Control", "no-store");
    server->send(code, "application/json", body.c_str());
}

void LedWeb::send_error(const char* message) {
    send_json(400, std::string("{\"ok\":false,\"error\":\"") + json_escape(message) + "\"}");
}

bool LedWeb::arg_long(const char* name, long min, long max, long& out) {
    return server->hasArg(name) && parse_long(server->arg(name), out) && out >= min && out <= max;
}

// $led brightness <0-255>
void LedWeb::handle_brightness() {
    long value = 0;
    if (!arg_long("value", 0, 255, value)) return send_error("value must be 0..255");
    led_strip.set_brightness(static_cast<uint8_t>(value), this);
    push_to_others();
    send_json(200, state_json());
}

// $led on / $led off
void LedWeb::handle_power() {
    long value = 0;
    if (!arg_long("value", 0, 1, value)) return send_error("value must be 0 or 1");
    led_strip.set_state(value == 1, this);
    push_to_others();
    send_json(200, state_json());
}

// $led_modes set <id>
void LedWeb::handle_mode() {
    long value = 0;
    if (!arg_long("value", 0, 255, value) || !led_modes.set_mode(static_cast<int>(value), this)) {
        return send_error("unknown mode id");
    }
    push_to_others();
    send_json(200, state_json());
}

// $led_modes param <mode> <key> <value>   (mode defaults to the current one; the setter clamps)
void LedWeb::handle_param() {
    long mode  = led_modes.get_mode();
    long value = 0;
    if (server->hasArg("mode") && !arg_long("mode", 0, 255, mode)) return send_error("unknown mode id");
    if (!server->hasArg("key")) return send_error("key missing");
    if (!arg_long("value", INT32_MIN, INT32_MAX, value)) return send_error("value must be an integer");
    if (!led_modes.set_param(static_cast<int>(mode), server->arg("key").c_str(), static_cast<int32_t>(value), this)) {
        return send_error("unknown mode or parameter key");
    }
    push_to_others();
    send_json(200, state_json());
}

// $led_modes color <rrggbb>
void LedWeb::handle_color() {
    LedRgb color{0, 0, 0};
    if (!server->hasArg("value") || !parse_hex_color(server->arg("value"), color)) {
        return send_error("value must be rrggbb");
    }
    if (!led_modes.set_color(color, this)) return send_error("the current mode has no colour");
    push_to_others();
    send_json(200, state_json());
}

// $led_modes reset_params (2.3.x reset_current_mode): one pass, one cross-fade
void LedWeb::handle_reset_params() {
    if (!led_modes.reset_params(led_modes.get_mode(), this)) return send_error("unknown mode id");
    push_to_others();
    send_json(200, state_json());
}

// =============================================================================
// Server-sent events
// =============================================================================
// WebServer (esp32 core 3.3.12) serves one client at a time and drops its reference after the
// handler returns (unless NetworkClient::setSSE(true), which would park the whole server on this
// socket). The stream is a copy of server->client(): the socket stays open, handleClient() goes on
// accepting other requests, and loop() writes the events.
void LedWeb::handle_events() {
    for (SseClient& slot : sse) {
        if (slot.active && !slot.client.connected()) sse_drop(slot);
    }
    SseClient* free_slot = nullptr;
    for (SseClient& slot : sse) {
        if (!slot.active) { free_slot = &slot; break; }
    }
    if (free_slot == nullptr) {
        ++sse_rejected;
        server->sendHeader("Retry-After", "15");
        return send_json(503, "{\"ok\":false,\"error\":\"too many event streams; poll /led/api/state\"}");
    }

    ++api_requests;
    NetworkClient client = server->client();
    client.setNoDelay(true);
    static const char HEAD[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "retry: 5000\n\n";
    if (client.write(reinterpret_cast<const uint8_t*>(HEAD), sizeof(HEAD) - 1) != sizeof(HEAD) - 1) {
        ++api_errors;
        client.stop();
        return;
    }
    free_slot->client  = client;
    free_slot->active  = true;
    free_slot->pending = true;                  // first event: the full state
    std::snprintf(free_slot->id, sizeof(free_slot->id), "%s", server->arg("client").c_str());
}

void LedWeb::loop() {
    if (sse_count() == 0) return;
    const uint32_t now       = millis();
    const bool     keepalive = now - sse_keepalive_ms >= SSE_KEEPALIVE_MS;
    std::string    event;                       // built once per loop, only when a stream needs it
    for (SseClient& slot : sse) {
        if (!slot.active) continue;
        if (!slot.client.connected()) { sse_drop(slot); continue; }
        if (slot.pending) {
            if (event.empty()) event = "event: state\ndata: " + state_json() + "\n\n";
            if (sse_write(slot, event.data(), event.size())) {
                slot.pending = false;
                ++sse_events;
            }
        } else if (keepalive) {
            static const char PING[] = ": keep-alive\n\n";
            sse_write(slot, PING, sizeof(PING) - 1);
        }
    }
    if (keepalive) sse_keepalive_ms = now;
}

// A short write means the peer is gone or stopped reading: drop the stream (the page reconnects or polls).
bool LedWeb::sse_write(SseClient& slot, const char* text, size_t length) {
    if (slot.client.write(reinterpret_cast<const uint8_t*>(text), length) == length) return true;
    sse_drop(slot);
    return false;
}

void LedWeb::sse_drop(SseClient& slot) {
    slot.client.stop();
    slot.client  = NetworkClient();
    slot.active  = false;
    slot.pending = false;
    slot.id[0]   = '\0';
}

uint8_t LedWeb::sse_count() const {
    uint8_t n = 0;
    for (const SseClient& slot : sse) n += slot.active ? 1 : 0;
    return n;
}

void LedWeb::mark_pending(const void* origin) {
    if (origin == static_cast<const void*>(this)) return;   // our own POST: push_to_others() handles it
    for (SseClient& slot : sse) {
        if (slot.active) slot.pending = true;
    }
}

// The page that POSTed gets the new state in the response; its own stream (same ?client= id) is skipped.
void LedWeb::push_to_others() {
    const String sender = server->arg("client");
    for (SseClient& slot : sse) {
        if (!slot.active) continue;
        if (sender.length() != 0 && sender == slot.id) continue;
        slot.pending = true;
    }
}

void LedWeb::on_brightness(uint8_t, const void* origin)                     { mark_pending(origin); }
void LedWeb::on_state(bool, const void* origin)                             { mark_pending(origin); }
void LedWeb::on_mode(uint8_t, const void* origin)                           { mark_pending(origin); }
void LedWeb::on_color(uint32_t, const void* origin)                         { mark_pending(origin); }
void LedWeb::on_param(uint8_t, const char*, uint16_t, const void* origin)   { mark_pending(origin); }
