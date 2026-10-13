// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-web/tests/unit/test_led_web_api.cpp
//
// Host test of LedWebApi.h: /set, /state, /modes and /name against a fake led, the WebSocket
// frames, the frame listener, the page sessions, the status page address, the route table and the
// CORS headers. Built by test_led-web.py with
// g++ -std=c++17 -Wall -Wextra -Werror, the XeWeCore src/ and the led module's src/Led on the
// include path ("../Led/LedListener.h").

#include "../../src/LedWeb/LedWebApi.h"

#include <climits>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

#define CHECK(expr) \
    do { \
        ++checks; \
        if (expr) { \
            std::cout << "ok   " #expr "\n"; \
        } else { \
            ++failures; \
            std::cout << "FAIL " #expr " (line " << __LINE__ << ")\n"; \
        } \
    } while (0)

using Rgb = std::array<uint8_t, 3>;
using led_web_api::Response;

// Arduino String::toInt on the ESP32: atol with a 32-bit long (saturated), 0 without digits
long arduino_to_int(const std::string& text) {
    const long long v = std::strtoll(text.c_str(), nullptr, 10);
    if (v > INT32_MAX)
        return INT32_MAX;
    if (v < INT32_MIN)
        return INT32_MIN;
    return static_cast<long>(v);
}

struct FakeRequest {
    std::map<std::string, std::string> args;

    bool has(const char* name) const {
        return args.count(name) != 0;
    }
    std::string arg(const char* name) const {
        auto it = args.find(name);
        return it == args.end() ? std::string() : it->second;
    }
    long arg_int(const char* name) const {
        return arduino_to_int(arg(name));
    }
};

struct FakeLed {
    // what the handlers called, one entry per call
    std::vector<std::string> calls;
    const void* last_origin = nullptr;

    Rgb rgb{0, 255, 0};
    uint8_t brightness = 50;
    bool state = true;
    uint8_t mode = 0;
    std::string modes_json = "[]";

    void sync_color(Rgb c, const void* origin) {
        calls.push_back("color " + std::to_string(c[0]) + " " + std::to_string(c[1]) + " " +
            std::to_string(c[2]));
        last_origin = origin;
    }
    void sync_brightness(uint8_t b, const void* origin) {
        calls.push_back("brightness " + std::to_string(b));
        last_origin = origin;
    }
    void sync_state(bool s, const void* origin) {
        calls.push_back(std::string("state ") + (s ? "1" : "0"));
        last_origin = origin;
    }
    void sync_mode(uint8_t m, const void* origin) {
        calls.push_back("mode " + std::to_string(m));
        last_origin = origin;
    }
    void reset_current_mode() {
        calls.push_back("reset_current_mode");
    }
    void set_mode_param(std::string_view key, uint16_t value) {
        calls.push_back("param " + std::string(key) + " " + std::to_string(value));
    }

    Rgb get_rgb() const {
        return rgb;
    }
    uint8_t get_brightness() const {
        return brightness;
    }
    bool get_state() const {
        return state;
    }
    uint8_t get_current_mode_id() const {
        return mode;
    }
    std::string get_all_modes_json() const {
        return modes_json;
    }
};

const int ORIGIN_TAG = 0;
const void* const ORIGIN = &ORIGIN_TAG;

// runs /set with these arguments; returns the response, the led's calls in `calls`
Response set(std::map<std::string, std::string> args, std::vector<std::string>& calls,
    const void** origin = nullptr) {
    FakeLed led;
    FakeRequest req{std::move(args)};
    Response r = led_web_api::handle_set(req, led, ORIGIN);
    calls = led.calls;
    if (origin)
        *origin = led.last_origin;
    return r;
}

bool is_ok(const Response& r) {
    return r.code == 200 && std::string(r.type) == "text/plain" && r.body == "OK";
}

bool is_bad_color(const Response& r) {
    return r.code == 400 && std::string(r.type) == "text/plain" && r.body == "Invalid color";
}

void test_set_color() {
    std::vector<std::string> calls;
    const void* origin = nullptr;

    CHECK(is_ok(set({{"color", "FF8000"}}, calls, &origin)));
    CHECK(calls == std::vector<std::string>{"color 255 128 0"});
    CHECK(origin == ORIGIN);

    CHECK(is_ok(set({{"color", "ff8000"}}, calls)));
    CHECK(calls == std::vector<std::string>{"color 255 128 0"});

    CHECK(is_ok(set({{"color", "000000"}}, calls)));
    CHECK(calls == std::vector<std::string>{"color 0 0 0"});

    CHECK(is_ok(set({{"color", "AbCdEf"}}, calls)));
    CHECK(calls == std::vector<std::string>{"color 171 205 239"});

    // the core parser accepts one leading '#'
    CHECK(is_ok(set({{"color", "#00FF00"}}, calls)));
    CHECK(calls == std::vector<std::string>{"color 0 255 0"});

    // malformed: 400, nothing applied
    const char* bad[] = {"", "#", "F", "FF", "FF00", "FFFFF", "#ff", "#FFFFF", "ff00zz", "1234567",
        "#1234567", "0xff0000", "0xFF00", " ff0000", "ff0000 ", "+f0000", "-00001", "##ff0000",
        "ff 000", "gggggg", "FF0000\n", "red"};
    for (const char* text : bad) {
        const Response r = set({{"color", text}}, calls);
        CHECK(is_bad_color(r));
        CHECK(calls.empty());
    }

    // color has precedence: a bad color is rejected even with a valid brightness beside it
    CHECK(is_bad_color(set({{"color", "zz"}, {"brightness", "10"}}, calls)));
    CHECK(calls.empty());
    CHECK(is_ok(set({{"color", "0000FF"}, {"brightness", "10"}, {"state", "1"}}, calls)));
    CHECK(calls == std::vector<std::string>{"color 0 0 255"});
}

void test_set_numbers() {
    std::vector<std::string> calls;
    const void* origin = nullptr;

    CHECK(is_ok(set({{"brightness", "128"}}, calls, &origin)));
    CHECK(calls == std::vector<std::string>{"brightness 128"});
    CHECK(origin == ORIGIN);
    set({{"brightness", "0"}}, calls);
    CHECK(calls == std::vector<std::string>{"brightness 0"});
    set({{"brightness", "255"}}, calls);
    CHECK(calls == std::vector<std::string>{"brightness 255"});
    set({{"brightness", "300"}}, calls); // modulo 256
    CHECK(calls == std::vector<std::string>{"brightness 44"});
    set({{"brightness", "-1"}}, calls);
    CHECK(calls == std::vector<std::string>{"brightness 255"});
    set({{"brightness", "abc"}}, calls); // toInt: 0
    CHECK(calls == std::vector<std::string>{"brightness 0"});
    set({{"brightness", "12abc"}}, calls); // leading digits
    CHECK(calls == std::vector<std::string>{"brightness 12"});
    set({{"brightness", "99999999999"}}, calls); // saturated to INT32_MAX, low byte 255
    CHECK(calls == std::vector<std::string>{"brightness 255"});

    set({{"state", "1"}}, calls);
    CHECK(calls == std::vector<std::string>{"state 1"});
    set({{"state", "0"}}, calls);
    CHECK(calls == std::vector<std::string>{"state 0"});
    set({{"state", "2"}}, calls); // only 1 is on
    CHECK(calls == std::vector<std::string>{"state 0"});
    set({{"state", "on"}}, calls);
    CHECK(calls == std::vector<std::string>{"state 0"});
    set({{"state", " 1"}}, calls);
    CHECK(calls == std::vector<std::string>{"state 1"});
    set({{"state", "257"}}, calls); // compared before narrowing
    CHECK(calls == std::vector<std::string>{"state 0"});

    set({{"mode_id", "5"}}, calls);
    CHECK(calls == std::vector<std::string>{"mode 5"});
    set({{"mode_id", "9"}}, calls); // unknown ids are the led's business
    CHECK(calls == std::vector<std::string>{"mode 9"});
    set({{"mode_id", "256"}}, calls);
    CHECK(calls == std::vector<std::string>{"mode 0"});

    CHECK(is_ok(set({{"reset_params", "1"}}, calls)));
    CHECK(calls == std::vector<std::string>{"reset_current_mode"});
    set({{"reset_params", ""}}, calls); // any value
    CHECK(calls == std::vector<std::string>{"reset_current_mode"});

    CHECK(is_ok(set({{"param", "speed"}, {"val", "20"}}, calls)));
    CHECK(calls == std::vector<std::string>{"param speed 20"});
    set({{"param", "hue_b"}, {"val", "70000"}}, calls); // modulo 65536
    CHECK(calls == std::vector<std::string>{"param hue_b 4464"});
    set({{"param", "speed"}}, calls); // val missing: nothing
    CHECK(calls.empty());
    set({{"val", "3"}}, calls);
    CHECK(calls.empty());

    // precedence: brightness, state, mode_id, reset_params, param
    set({{"brightness", "1"}, {"state", "1"}, {"mode_id", "2"}, {"reset_params", "1"},
            {"param", "speed"}, {"val", "4"}},
        calls);
    CHECK(calls == std::vector<std::string>{"brightness 1"});
    set({{"state", "1"}, {"mode_id", "2"}, {"reset_params", "1"}}, calls);
    CHECK(calls == std::vector<std::string>{"state 1"});
    set({{"mode_id", "2"}, {"reset_params", "1"}, {"param", "speed"}, {"val", "4"}}, calls);
    CHECK(calls == std::vector<std::string>{"mode 2"});
    set({{"reset_params", "1"}, {"param", "speed"}, {"val", "4"}}, calls);
    CHECK(calls == std::vector<std::string>{"reset_current_mode"});

    // no or unknown arguments: OK, nothing changes
    CHECK(is_ok(set({}, calls)));
    CHECK(calls.empty());
    CHECK(is_ok(set({{"colour", "FF0000"}}, calls)));
    CHECK(calls.empty());
}

void test_state_modes_name() {
    FakeLed led;
    led.rgb = {255, 0, 0};
    led.brightness = 128;
    led.state = true;
    led.mode = 3;
    Response r = led_web_api::state_response(led);
    CHECK(r.code == 200 && std::string(r.type) == "text/plain");
    CHECK(r.body == "FFF0000,128,1,3");

    led.rgb = {0, 10, 171};
    led.brightness = 255;
    led.state = false;
    led.mode = 6;
    CHECK(led_web_api::state_response(led).body == "F000AAB,255,0,6");

    led.rgb = {0, 255, 0};
    led.brightness = 50;
    led.state = true;
    led.mode = 0;
    CHECK(led_web_api::state_response(led).body == "F00FF00,50,1,0");
    // the same frame a connecting page gets
    CHECK(led_web_api::frame_current(led) == led_web_api::state_response(led).body);

    led.modes_json = "[{\"id\":0,\"name\":\"Solid\",\"params\":[{\"key\":\"hue\",\"display_name\":"
                     "\"Hue\",\"min\":0,\"max\":255,\"step\":1,\"value\":85,\"type\":\"b\"}]}]";
    r = led_web_api::modes_response(led);
    CHECK(r.code == 200 && std::string(r.type) == "application/json");
    CHECK(r.body == led.modes_json);

    r = led_web_api::name_response("Kitchen Strip");
    CHECK(r.code == 200 && std::string(r.type) == "text/plain" && r.body == "Kitchen Strip");
    CHECK(led_web_api::name_response("").body.empty());
}

void test_frames() {
    using namespace led_web_api;
    CHECK(frame_color({1, 2, 255}) == "C0102FF");
    CHECK(frame_color({0, 0, 0}) == "C000000");
    CHECK(frame_color({171, 205, 239}) == "CABCDEF");
    CHECK(frame_brightness(0) == "B0");
    CHECK(frame_brightness(7) == "B7");
    CHECK(frame_brightness(255) == "B255");
    CHECK(frame_state(true) == "S1");
    CHECK(frame_state(false) == "S0");
    CHECK(frame_mode(0) == "M0");
    CHECK(frame_mode(255) == "M255");
    CHECK(frame_all({255, 255, 255}, 255, true, 255) == "FFFFFFF,255,1,255");
    CHECK(frame_all({0, 0, 0}, 0, false, 0) == "F000000,0,0,0");
    CHECK(frame_param("speed", 20) == "Pspeed:20");
    CHECK(frame_param("hue_b", 65535) == "Phue_b:65535");
    CHECK(frame_param(std::string(100, 'k'), 1).size() == 63);
    CHECK(std::string(FRAME_HEARTBEAT) == "H");
}

struct Recorder : led_web_api::FrameListener {
    std::vector<std::string> frames;
    void send_frame(const std::string& frame) override {
        frames.push_back(frame);
    }
};

void test_listener() {
    Recorder rec;
    LedListener& l = rec;
    const int self_tag = 0;
    const void* self = &self_tag;

    // own changes are echoed too
    l.on_color({255, 0, 0}, self);
    l.on_brightness(77, nullptr);
    l.on_state(false, self);
    l.on_mode(4, nullptr);
    l.on_length(300, nullptr);
    CHECK((rec.frames == std::vector<std::string>{"CFF0000", "B77", "S0", "M4"}));

    rec.frames.clear();
    l.on_all({0, 255, 0}, 50, true, 0, 600, nullptr);
    CHECK((rec.frames == std::vector<std::string>{"F00FF00,50,1,0"}));

    // events whose origin is the listener itself are sent too: the firmware's /set passes the
    // module (this) as origin and the page that sent the change relies on the echo
    const void* own[] = {static_cast<const void*>(&rec),
        static_cast<const void*>(static_cast<const LedListener*>(&rec)),
        static_cast<const void*>(static_cast<const led_web_api::FrameListener*>(&rec))};
    for (const void* origin : own) {
        rec.frames.clear();
        l.on_color({1, 2, 3}, origin);
        CHECK((rec.frames == std::vector<std::string>{"C010203"}));
        rec.frames.clear();
        l.on_brightness(9, origin);
        CHECK((rec.frames == std::vector<std::string>{"B9"}));
        rec.frames.clear();
        l.on_state(true, origin);
        CHECK((rec.frames == std::vector<std::string>{"S1"}));
        rec.frames.clear();
        l.on_mode(2, origin);
        CHECK((rec.frames == std::vector<std::string>{"M2"}));
        rec.frames.clear();
        l.on_all({4, 5, 6}, 7, false, 1, 60, origin);
        CHECK((rec.frames == std::vector<std::string>{"F040506,7,0,1"}));
    }

    // frame order follows the event order, one frame per event, sent at once. A mode change
    // reaches the listener as the new mode's colour (origin: the led) and then the mode, so the
    // pages get C then M; `$led reset` as the nested colour and then on_all, so C then F (the led
    // module's own tests pin the event order).
    const int led_tag = 0;
    const void* led_origin = &led_tag;
    rec.frames.clear();
    l.on_color({255, 255, 255}, led_origin);
    CHECK((rec.frames == std::vector<std::string>{"CFFFFFF"}));
    l.on_mode(5, &rec);
    CHECK((rec.frames == std::vector<std::string>{"CFFFFFF", "M5"}));
    rec.frames.clear();
    l.on_color({0, 255, 0}, led_origin);
    l.on_all({0, 255, 0}, 50, true, 0, 60, nullptr);
    CHECK((rec.frames == std::vector<std::string>{"C00FF00", "F00FF00,50,1,0"}));
}

// a page session recorder: frames and disconnects in one log, in call order
struct Sessions : led_web_api::PageSessions {
    std::vector<std::string> log;
    void send_frame(const std::string& frame) override {
        log.push_back(frame);
    }
    void disconnect_all() override {
        log.push_back("disconnect");
    }
};

void test_sessions() {
    FakeLed led; // green, 50, on, mode 0
    Sessions s;
    const uint32_t beat = 1000;

    // no page: no heartbeat, however long
    CHECK(s.client_count() == 0);
    CHECK(!s.heartbeat_due(0, beat));
    CHECK(!s.heartbeat_due(100000, beat));

    // a page connects: counted, then one F with the current values to every page
    s.page_connected(led);
    CHECK(s.client_count() == 1);
    CHECK((s.log == std::vector<std::string>{"F00FF00,50,1,0"}));

    // heartbeat while connected: first due 1000 ms after 0, then 1000 ms after the last one
    CHECK(!s.heartbeat_due(999, beat));
    CHECK(s.heartbeat_due(1000, beat));
    s.heartbeat_sent(1000);
    CHECK(!s.heartbeat_due(1999, beat));
    CHECK(s.heartbeat_due(2000, beat));
    s.heartbeat_sent(2000);
    // millis() wrapping past 2^32 keeps the interval
    s.heartbeat_sent(UINT32_MAX - 499);
    CHECK(!s.heartbeat_due(499, beat));
    CHECK(s.heartbeat_due(500, beat));
    s.heartbeat_sent(500);

    // a second page: F to every page with the values of now
    led.rgb = {1, 2, 3};
    led.brightness = 9;
    led.state = false;
    led.mode = 4;
    s.log.clear();
    s.page_connected(led);
    CHECK(s.client_count() == 2);
    CHECK((s.log == std::vector<std::string>{"F010203,9,0,4"}));

    // disconnects count down and stop at 0; no heartbeat without a page
    s.page_disconnected();
    CHECK(s.client_count() == 1);
    CHECK(s.heartbeat_due(1500, beat));
    s.page_disconnected();
    CHECK(s.client_count() == 0);
    CHECK(!s.heartbeat_due(5000, beat));
    s.page_disconnected();
    CHECK(s.client_count() == 0);

    // close_pages: the server disconnects every page and the count is 0 afterwards, also when the
    // server reports no disconnect events
    s.page_connected(led);
    s.page_connected(led);
    s.log.clear();
    s.close_pages();
    CHECK((s.log == std::vector<std::string>{"disconnect"}));
    CHECK(s.client_count() == 0);
    CHECK(!s.heartbeat_due(100000, beat));

    // the count is a uint8_t: 256 connects wrap to 0
    Sessions w;
    for (int i = 0; i < 256; ++i)
        w.page_connected(led);
    CHECK(w.client_count() == 0);
    CHECK(w.log.size() == 256);
}

void test_page_url() {
    using led_web_api::page_url;
    CHECK(page_url("192.168.1.20", "80") == "http://192.168.1.20/");
    CHECK(page_url("192.168.1.20", "") == "http://192.168.1.20/");
    CHECK(page_url("192.168.1.20", "8080") == "http://192.168.1.20:8080/");
    CHECK(page_url("10.0.0.5", "1") == "http://10.0.0.5:1/");
    CHECK(page_url("0.0.0.0", "81") == "http://0.0.0.0:81/");
}

void test_routes() {
    using namespace led_web_api;
    CHECK(ROUTE_COUNT == 11);
    std::set<std::string> paths;
    int pages = 0;
    for (const Route& r : ROUTES) {
        paths.insert(r.path);
        CHECK(std::string(r.method) == "GET");
        CHECK(r.path[0] == '/');
        pages += r.content == Content::Page;
    }
    CHECK(paths.size() == ROUTE_COUNT);
    CHECK(pages == 1);
    CHECK(std::string(ROUTES[0].path) == "/" && ROUTES[0].content == Content::Page);

    CHECK(std::string(content_type(Content::Page)) == "text/html");
    CHECK(std::string(content_type(Content::Css)) == "text/css");
    CHECK(std::string(content_type(Content::Js)) == "application/javascript");
    CHECK(std::string(content_type(Content::Modes)) == "application/json");
    CHECK(std::string(content_type(Content::State)) == "text/plain");
    CHECK(std::string(content_type(Content::Set)) == "text/plain");
    CHECK(std::string(content_type(Content::Name)) == "text/plain");

    CHECK(std::string(CORS_HEADERS[0][0]) == "Access-Control-Allow-Origin");
    CHECK(std::string(CORS_HEADERS[0][1]) == "*");
    CHECK(std::string(CORS_HEADERS[1][0]) == "Access-Control-Allow-Methods");
    CHECK(std::string(CORS_HEADERS[1][1]) == "POST, GET, OPTIONS, DELETE, PUT");
    CHECK(std::string(CORS_HEADERS[2][0]) == "Access-Control-Allow-Headers");
    CHECK(std::string(CORS_HEADERS[2][1]) == "Content-Type");
}

} // namespace

int main() {
    test_set_color();
    test_set_numbers();
    test_state_modes_name();
    test_frames();
    test_listener();
    test_sessions();
    test_page_url();
    test_routes();

    if (failures == 0)
        std::cout << "PASSED: " << checks << " check(s), 0 failure(s)\n";
    else
        std::cout << "FAILED: " << checks << " check(s), " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
