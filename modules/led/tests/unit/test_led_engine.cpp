// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/test_led_engine.cpp
//
// Host tests of the led engine against the xewe-led-os 2.3.0 behaviour, with every reference value
// written out as a literal: the 30 `$led` commands (table, argument parsing, effects, NVS writes,
// console output, listener events and their order), the mode parameter tables and the /modes
// JSON, parameter normalisation, the chipset and colour-order tables, the mode registry, the status
// text, the three fixes, and the lock discipline (no frame inside a command, nothing slow under
// the lock). Built by test_led.py with FastLED's stub platform and host/Arduino.h.

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../src/Led/Chipsets.h"
#include "../../src/Led/LedCommands.h"
#include "../../src/Led/LedEngine.h"
#include "../../src/Led/ModeController.h"
#include "../../src/Led/ModeRegistry.h"

#include <XeWeCore/Utils/String.h>

// ---------------------------------------------------------------------------------------------
// Minimal check framework: one line per check, then the summary test_led.py parses
// ---------------------------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr) \
    do { \
        ++g_checks; \
        if (expr) { \
            std::printf("ok   %s\n", #expr); \
        } else { \
            ++g_failures; \
            std::printf("FAIL %s (line %d)\n", #expr, __LINE__); \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        ++g_checks; \
        const auto va_ = (a); \
        const auto vb_ = (b); \
        if (va_ == vb_) { \
            std::printf("ok   %s == %s\n", #a, #b); \
        } else { \
            ++g_failures; \
            std::printf("FAIL %s == %s (line %d): got [%s] want [%s]\n", #a, #b, __LINE__, \
                str(va_).c_str(), str(vb_).c_str()); \
        } \
    } while (0)

static std::string str(const std::string& s) {
    return s;
}
static std::string str(const char* s) {
    return s;
}
[[maybe_unused]] static std::string str(long long v) {
    return std::to_string(v);
}
static std::string str(int v) {
    return std::to_string(v);
}
static std::string str(unsigned v) {
    return std::to_string(v);
}
[[maybe_unused]] static std::string str(long v) {
    return std::to_string(v);
}
static std::string str(unsigned long v) {
    return std::to_string(v);
}
static std::string str(bool v) {
    return v ? "true" : "false";
}
static std::string str(uint8_t v) {
    return std::to_string(v);
}
static std::string str(uint16_t v) {
    return std::to_string(v);
}
static std::string str(const std::array<uint8_t, 3>& v) {
    return std::to_string(v[0]) + "," + std::to_string(v[1]) + "," + std::to_string(v[2]);
}
static std::string str(const std::vector<std::string>& v) {
    std::string o;
    for (const auto& s : v)
        o += (o.empty() ? "" : " | ") + s;
    return o;
}

// ---------------------------------------------------------------------------------------------
// Fixture: recording NVS/console, in-memory mode parameter store, recording listener
// ---------------------------------------------------------------------------------------------
struct RecIo : LedEngineIo {
    std::vector<std::string> writes; // "key=value" in write order
    std::vector<std::string> printed;
    void save_u8(const char* k, uint8_t v) override {
        writes.push_back(std::string(k) + "=" + std::to_string(v));
    }
    void save_u16(const char* k, uint16_t v) override {
        writes.push_back(std::string(k) + "=" + std::to_string(v) + "/u16");
    }
    void save_bool(const char* k, bool v) override {
        writes.push_back(std::string(k) + "=" + (v ? "1" : "0"));
    }
    void save_rgb(const std::array<uint8_t, 3>& rgb) override {
        writes.push_back("rgb=" + str(rgb));
    }
    void print(std::string_view t) override {
        printed.emplace_back(t);
    }
    int calibrations = 0;
    void calibrate_color_order() override {
        ++calibrations;
    }
};

struct MemStore : ModeParamStore {
    std::map<std::pair<uint8_t, std::string>, uint16_t> values;
    int commits = 0;
    uint16_t read(uint8_t id, std::string_view key, uint16_t def) override {
        auto it = values.find({id, std::string(key)});
        return it == values.end() ? def : it->second;
    }
    void write(uint8_t id, std::string_view key, uint16_t v) override {
        values[{id, std::string(key)}] = v;
    }
    void commit() override {
        ++commits;
    }
};

static const int kOther = 0; // an external origin
static const void* const OTHER = &kOther;

struct RecListener : LedListener {
    std::vector<std::string> ev;
    const void* self = nullptr;
    std::string o(const void* origin) const {
        return origin == nullptr ? "cli"
            : origin == self     ? "self"
            : origin == OTHER    ? "other"
                                 : "?";
    }
    void on_color(std::array<uint8_t, 3> rgb, const void* origin) override {
        ev.push_back("C" + str(rgb) + "@" + o(origin));
    }
    void on_brightness(uint8_t b, const void* origin) override {
        ev.push_back("B" + std::to_string(b) + "@" + o(origin));
    }
    void on_state(bool on, const void* origin) override {
        ev.push_back(std::string("S") + (on ? "1" : "0") + "@" + o(origin));
    }
    void on_mode(uint8_t m, const void* origin) override {
        ev.push_back("M" + std::to_string(m) + "@" + o(origin));
    }
    void on_length(uint16_t n, const void* origin) override {
        ev.push_back("L" + std::to_string(n) + "@" + o(origin));
    }
};

// a listener that sends one combined message for sync_all (like the web page's F frame)
struct AllListener : RecListener {
    void on_all(std::array<uint8_t, 3> rgb, uint8_t b, bool on, uint8_t m, uint16_t n,
        const void* origin) override {
        ev.push_back("F" + str(rgb) + "," + std::to_string(b) + "," + (on ? "1" : "0") + "," +
            std::to_string(m) + "," + std::to_string(n) + "@" + o(origin));
    }
};

struct Fx {
    RecIo io;
    MemStore store;
    LedEngine e{io};
    RecListener l;

    // a configured device after boot: Solid green, brightness 50, on, 60 LEDs, GRB
    explicit Fx(uint16_t n = 60, uint8_t mode = 0, std::array<uint8_t, 3> rgb = {0, 255, 0},
        uint8_t brightness = 50, bool on = true) {
        host::set_millis(10000);
        e.begin(n, store);
        e.boot(2, std::vector<uint8_t>(rgb.begin(), rgb.end()), brightness, mode, on);
        l.self = e.self_origin();
        e.listeners.add(&l);
        clear();
    }
    void clear() {
        io.writes.clear();
        io.printed.clear();
        l.ev.clear();
    }
    void run(const std::string& line) {
        std::vector<std::string> words;
        std::size_t i = 0;
        while (i < line.size()) {
            const std::size_t j = line.find(' ', i);
            words.push_back(line.substr(i, j == std::string::npos ? std::string::npos : j - i));
            if (j == std::string::npos)
                break;
            i = j + 1;
        }
        for (std::size_t c = 0; c < LED_COMMAND_COUNT; ++c) {
            if (words[0] == LED_COMMANDS[c].name) {
                if (words.size() - 1 != LED_COMMANDS[c].arg_count) {
                    std::printf("bad test line '%s'\n", line.c_str());
                    ++g_failures;
                    return;
                }
                const std::vector<std::string> args(words.begin() + 1, words.end());
                led_run_command(e, c, xewe::span<const std::string>(args.data(), args.size()));
                return;
            }
        }
        std::printf("unknown test command '%s'\n", line.c_str());
        ++g_failures;
    }
    // let every fade finish (500 ms brightness, 900 ms mode)
    void settle() {
        host::advance_millis(2000);
    }
};

static std::array<uint8_t, 3> h2r(uint8_t h, uint8_t s, uint8_t v) {
    return xewe::color::hsv_to_rgb({h, s, v});
}
static std::array<uint8_t, 3> r2h(std::array<uint8_t, 3> rgb) {
    return xewe::color::rgb_to_hsv(rgb);
}
static std::vector<std::string> V(std::initializer_list<std::string> l) {
    return std::vector<std::string>(l);
}

// ---------------------------------------------------------------------------------------------
// 1. Command table (names, argument counts, descriptions, sample usages)
// ---------------------------------------------------------------------------------------------
static void test_command_table() {
    struct Row {
        const char* name;
        unsigned args;
        const char* description;
        const char* sample;
    };
    static const Row rows[] = {
        {"set_rgb", 3, "Set RGB color", "$led set_rgb 255 0 0"},
        {"set_r", 1, "Set red channel", "$led set_r 127"},
        {"set_g", 1, "Set green channel", "$led set_g 255"},
        {"set_b", 1, "Set blue channel", "$led set_b 200"},
        {"adj_rgb", 3, "Adjust RGB color by deltas", "$led adj_rgb 10 -20 5"},
        {"adj_r", 1, "Adjust red channel by delta", "$led adj_r -10"},
        {"adj_g", 1, "Adjust green channel by delta", "$led adj_g 15"},
        {"adj_b", 1, "Adjust blue channel by delta", "$led adj_b -5"},
        {"set_hsv", 3, "Set HSV color", "$led set_hsv 75 255 0"},
        {"set_hue", 1, "Set hue channel", "$led set_hue 255"},
        {"set_sat", 1, "Set saturation channel", "$led set_sat 0"},
        {"set_val", 1, "Set value channel", "$led set_val 255"},
        {"adj_hsv", 3, "Adjust HSV color by deltas", "$led adj_hsv 10 -20 5"},
        {"adj_hue", 1, "Adjust hue channel by delta", "$led adj_h -10"},
        {"adj_sat", 1, "Adjust saturation channel by delta", "$led adj_s 15"},
        {"adj_val", 1, "Adjust value channel by delta", "$led adj_v -5"},
        {"set_brightness", 1, "Set global brightness", "$led set_brightness 255"},
        {"adj_brightness", 1, "Adjust global brightness by delta", "$led adj_brightness -10"},
        {"set_state", 1, "Set on/off state", "$led set_state 0"},
        {"toggle_state", 0, "If off->on, if on->off", "$led toggle_state"},
        {"turn_on", 0, "Turn strip on", "$led turn_on"},
        {"turn_off", 0, "Turn strip off", "$led turn_off"},
        {"set_mode", 1, "Set LED strip mode", "$led set_mode 0"},
        {"adj_mode", 1, "Adjust mode by delta (next/prev)", "$led adj_mode 1"},
        {"set_mode_param", 2, "Set a parameter for the current mode",
            "$led set_mode_param hue 128"},
        {"adj_mode_param", 2, "Adjust a parameter for the current mode by delta",
            "$led adj_mode_param hue -15"},
        {"get_mode_params", 0, "Get parameters of the current mode", "$led get_mode_params"},
        {"reset_current_mode", 0, "Reset current mode parameters to defaults",
            "$led reset_current_mode"},
        {"set_length", 1, "Set new number of LEDs", "$led set_length 500"},
        {"set_color_order", 1, "Set LED color order (RGB/RBG/GRB/GBR/BRG/BGR)",
            "$led set_color_order GRB"},
    };
    CHECK_EQ(LED_COMMAND_COUNT, sizeof(rows) / sizeof(rows[0]));
    CHECK_EQ(LED_COMMAND_COUNT, 30ul);
    for (std::size_t i = 0; i < LED_COMMAND_COUNT && i < 30; ++i) {
        CHECK_EQ(std::string(LED_COMMANDS[i].name), std::string(rows[i].name));
        CHECK_EQ(unsigned(LED_COMMANDS[i].arg_count), rows[i].args);
        CHECK_EQ(std::string(LED_COMMANDS[i].description), std::string(rows[i].description));
        CHECK_EQ(std::string(LED_COMMANDS[i].sample_usage), std::string(rows[i].sample));
        CHECK(LED_COMMANDS[i].run != nullptr);
    }
}

// ---------------------------------------------------------------------------------------------
// 2. Argument parsing (String::toInt, narrowing, no error text)
// ---------------------------------------------------------------------------------------------
static void test_arg_parsing() {
    CHECK_EQ(led_arg_to_int("abc"), 0);
    CHECK_EQ(led_arg_to_int("12x"), 12);
    CHECK_EQ(led_arg_to_int("-1"), -1);
    CHECK_EQ(led_arg_to_int("+5"), 5);
    CHECK_EQ(led_arg_to_int(" 7"), 7);
    CHECK_EQ(led_arg_to_int(""), 0);
    CHECK_EQ(led_arg_to_int("0x10"), 0);
    CHECK_EQ(led_arg_to_int("99999999999"), 2147483647);
    CHECK_EQ(led_arg_to_int("-99999999999"), -2147483647 - 1);

    Fx f;
    f.run("set_rgb 300 -1 abc"); // 300 -> 44, -1 -> 255, abc -> 0
    CHECK_EQ(f.l.ev, V({"C44,255,0@cli"}));
    CHECK_EQ(f.io.printed.size(), 0ul);
}

// ---------------------------------------------------------------------------------------------
// 3. Colour commands
// ---------------------------------------------------------------------------------------------
static void test_color_commands() {
    {
        Fx f;
        f.run("set_rgb 255 0 0");
        CHECK_EQ(f.l.ev, V({"C255,0,0@cli"}));
        CHECK_EQ(f.io.writes, V({"rgb=255,0,0"}));
        CHECK_EQ(f.e.get_current_mode_param("hue"), uint16_t(r2h({255, 0, 0})[0]));
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(r2h({255, 0, 0})[1]));
        CHECK_EQ(f.e.get_rgb(), h2r(r2h({255, 0, 0})[0], r2h({255, 0, 0})[1], 255));
        CHECK_EQ((f.store.values[{0, "hue"}]), uint16_t(r2h({255, 0, 0})[0]));
    }
    {
        Fx f;
        const auto g = f.e.get_rgb();
        f.run("set_r 127");
        CHECK_EQ(f.l.ev, V({"C127," + std::to_string(g[1]) + "," + std::to_string(g[2]) + "@cli"}));
        f.clear();
        const auto g2 = f.e.get_rgb();
        f.run("set_g 10");
        CHECK_EQ(
            f.l.ev, V({"C" + std::to_string(g2[0]) + ",10," + std::to_string(g2[2]) + "@cli"}));
        f.clear();
        const auto g3 = f.e.get_rgb();
        f.run("set_b 200");
        CHECK_EQ(
            f.l.ev, V({"C" + std::to_string(g3[0]) + "," + std::to_string(g3[1]) + ",200@cli"}));
    }
    {
        // adj_rgb: applied locally, then announced with the result (and applied again): 2 rgb
        // writes
        Fx f;
        f.run("set_rgb 100 100 100");
        f.clear();
        const auto before = f.e.get_rgb();
        f.run("adj_rgb 10 -20 5");
        auto clamp8 = [](int v) { return uint8_t(std::clamp(v, 0, 255)); };
        std::array<uint8_t, 3> adj = {
            clamp8(before[0] + 10), clamp8(before[1] - 20), clamp8(before[2] + 5)};
        // the local set_rgb rebuilds the mode from rgb -> hue/sat, so the announced colour is the
        // mode's colour after that round trip
        const auto hsv = r2h(adj);
        const auto announced = h2r(hsv[0], hsv[1], 255);
        CHECK_EQ(f.l.ev, V({"C" + str(announced) + "@cli"}));
        CHECK_EQ(f.io.writes.size(), 2ul);
        CHECK_EQ(f.io.writes[0], "rgb=" + str(adj));
        CHECK_EQ(f.io.writes[1], "rgb=" + str(announced));
    }
    {
        Fx f;
        f.run("adj_r -300"); // clamps at 0
        CHECK_EQ(f.e.get_rgb()[0], uint8_t(0));
        CHECK_EQ(f.l.ev.size(), 1ul);
        f.clear();
        f.run("adj_g 15");
        f.run("adj_b -5");
        CHECK_EQ(f.l.ev.size(), 2ul);
    }
    {
        // set_hsv V=0 -> black -> hue 0, sat 0 -> Solid white (the sample usage itself)
        Fx f;
        f.run("set_hsv 75 255 0");
        CHECK_EQ(f.l.ev, V({"C0,0,0@cli"}));
        CHECK_EQ(f.e.get_rgb(), (std::array<uint8_t, 3>{255, 255, 255}));
        CHECK_EQ(f.e.get_current_mode_param("hue"), uint16_t(0));
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(0));
        CHECK_EQ(f.io.writes, V({"rgb=0,0,0"}));
    }
    {
        Fx f;
        f.run("set_hsv 160 200 255");
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(160, 200, 255)) + "@cli"}));
        f.clear();
        const auto hsv = f.e.get_hsv();
        f.run("set_hue 10");
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(10, hsv[1], hsv[2])) + "@cli"}));
        f.clear();
        const auto hsv2 = f.e.get_hsv();
        f.run("set_sat 0");
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(hsv2[0], 0, hsv2[2])) + "@cli"}));
        f.clear();
        const auto hsv3 = f.e.get_hsv();
        f.run("set_val 0"); // V=0: white again
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(hsv3[0], hsv3[1], 0)) + "@cli"}));
        CHECK_EQ(f.e.get_rgb(), (std::array<uint8_t, 3>{255, 255, 255}));
    }
    {
        // adj_hsv: hue wraps, sat/val clamp; then announced as hsv_to_rgb(get_hsv())
        Fx f;
        f.run("set_hsv 5 200 255");
        f.clear();
        const auto hsv = f.e.get_hsv();
        f.run("adj_hsv -10 100 -5");
        const std::array<uint8_t, 3> want = {
            uint8_t((hsv[0] - 10 + 256) % 256), 255, uint8_t(std::max(0, int(hsv[2]) - 5))};
        const auto applied = h2r(want[0], want[1], 255); // V does not reach the mode
        const auto again = r2h(applied);
        CHECK_EQ(f.io.writes.size(), 2ul);
        CHECK_EQ(f.io.writes[0], "rgb=" + str(applied));
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(again[0], again[1], again[2])) + "@cli"}));
        f.clear();
        f.run("adj_hue -10");
        f.run("adj_sat 15");
        f.run("adj_val -5");
        CHECK_EQ(f.l.ev.size(), 3ul);
        CHECK_EQ(f.io.writes.size(), 6ul);
    }
    {
        // colour commands on Rainbow (no hue/sat): no visible change, still rewritten + announced
        Fx f(60, 5);
        f.run("set_rgb 1 2 3");
        CHECK_EQ(f.e.get_rgb(), (std::array<uint8_t, 3>{255, 255, 255}));
        CHECK_EQ(f.l.ev, V({"C1,2,3@cli"}));
        CHECK_EQ(f.io.writes, V({"rgb=1,2,3"}));
    }
}

// ---------------------------------------------------------------------------------------------
// 4. Brightness and state commands
// ---------------------------------------------------------------------------------------------
static void test_brightness_state_commands() {
    {
        Fx f;
        f.run("set_brightness 255");
        CHECK_EQ(f.l.ev, V({"B255@cli"}));
        CHECK_EQ(f.io.writes, V({"brightness=255"}));
        CHECK_EQ(f.e.get_brightness(), uint8_t(255));
        f.clear();
        // 0 fades to black but keeps the state on and the last brightness
        f.run("set_brightness 0");
        CHECK_EQ(f.l.ev, V({"B0@cli"}));
        CHECK_EQ(f.io.writes, V({"brightness=255"}));
        CHECK(f.e.get_state());
        f.clear();
        f.run("set_brightness 300"); // 300 -> 44
        CHECK_EQ(f.l.ev, V({"B44@cli"}));
    }
    {
        Fx f;
        f.settle();
        f.run("adj_brightness -10"); // target 50 -> 40, three writes
        CHECK_EQ(f.l.ev, V({"B40@cli"}));
        CHECK_EQ(f.io.writes, V({"brightness=40", "brightness=40", "brightness=40"}));
        f.clear();
        f.settle();
        f.run("adj_brightness -100"); // reaches 0: fades down, then re-targeted to the last (40)
        CHECK_EQ(f.l.ev, V({"B40@cli"}));
        CHECK_EQ(f.e.get_brightness(), uint8_t(40));
    }
    {
        Fx f;
        f.run("set_state 0");
        CHECK_EQ(f.l.ev, V({"S0@cli"}));
        CHECK(!f.e.get_state());
        CHECK_EQ(f.io.writes, V({"state=0", "state=0"}));
        f.clear();
        f.run("set_state abc"); // 0 -> off, still announced
        CHECK_EQ(f.l.ev, V({"S0@cli"}));
        f.clear();
        f.run("set_state 7");
        CHECK_EQ(f.l.ev, V({"S1@cli"}));
        CHECK(f.e.get_state());
        f.clear();
        f.run("toggle_state");
        CHECK_EQ(f.l.ev, V({"S0@cli"}));
        f.clear();
        f.run("turn_on");
        CHECK_EQ(f.l.ev, V({"S1@cli"}));
        f.clear();
        f.run("turn_on"); // no-op on the strip, still announced
        CHECK_EQ(f.l.ev, V({"S1@cli"}));
        f.clear();
        f.run("turn_off");
        CHECK_EQ(f.l.ev, V({"S0@cli"}));
        f.clear();
        f.settle();
        f.run("adj_brightness 10"); // while off: from target 0
        CHECK_EQ(f.l.ev, V({"B10@cli"}));
        CHECK_EQ(f.e.get_brightness(), uint8_t(10));
    }
}

// ---------------------------------------------------------------------------------------------
// 5. Mode commands (events, order, stored id, clamping through the CLI)
// ---------------------------------------------------------------------------------------------
static void test_mode_commands() {
    {
        // set_mode: the new mode's colour first (origin: the strip), then the mode
        Fx f;
        f.run("set_mode 5");
        CHECK_EQ(f.l.ev, V({"C255,255,255@self", "M5@cli"}));
        CHECK_EQ(f.e.get_current_mode_id(), uint8_t(5));
        CHECK_EQ(f.io.writes, V({"rgb=255,255,255", "mode_id=5"}));
        f.clear();
        f.run("set_mode 6");
        CHECK_EQ(f.l.ev, V({"C85,49,22@self", "M6@cli"}));
    }
    {
        // unknown id: mode 0 runs, the requested id is stored and announced
        Fx f;
        f.run("set_mode 9");
        CHECK_EQ(f.e.get_current_mode_id(), uint8_t(0));
        CHECK_EQ(f.l.ev.back(), "M9@cli");
        CHECK_EQ(f.io.writes.back(), "mode_id=9");
        f.clear();
        f.run("set_mode 256"); // -> 0
        CHECK_EQ(f.l.ev.back(), "M0@cli");
    }
    {
        // adj_mode: 6 + 1 = 7 (unknown, mode 0 runs, 7 stored), then sync_mode(0) rewrites 0
        Fx f(60, 6);
        f.run("adj_mode 1");
        CHECK_EQ(f.e.get_current_mode_id(), uint8_t(0));
        CHECK_EQ(f.l.ev.size(), 3ul);
        CHECK_EQ(f.l.ev[2], "M0@cli");
        CHECK_EQ(f.io.writes[1], "mode_id=7");
        CHECK_EQ(f.io.writes.back(), "mode_id=0");
        f.clear();
        f.run("adj_mode -1"); // 0 - 1 clamps at 0, no wrap down
        CHECK_EQ(f.e.get_current_mode_id(), uint8_t(0));
        CHECK_EQ(f.l.ev.back(), "M0@cli");
        f.clear();
        f.run("adj_mode 3");
        CHECK_EQ(f.e.get_current_mode_id(), uint8_t(3));
    }
    {
        // set_mode_param: unknown key: silently nothing
        Fx f;
        f.run("set_mode_param nope 1");
        CHECK(f.l.ev.empty());
        CHECK(f.io.writes.empty());
        CHECK(f.io.printed.empty());
        // known key: normalised (hue wraps), rebuilt, then the colour is re-applied and announced
        f.run("set_mode_param hue 300");
        const uint16_t sat = f.e.get_current_mode_param("sat");
        CHECK_EQ(f.l.ev, V({"C" + str(h2r(44, uint8_t(sat), 255)) + "@self"}));
        CHECK_EQ(f.e.get_current_mode_param("hue"), uint16_t(r2h(h2r(44, uint8_t(sat), 255))[0]));
        f.clear();
        f.run("set_mode_param sat 999"); // clamps to 255
        CHECK_EQ(f.l.ev.size(), 1ul);
    }
    {
        // Color Fade: sat max is 245; the colour applied again after set_mode (saturation 255)
        // is kept inside that range
        Fx f;
        f.run("set_mode 1");
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(245));
        f.clear();
        f.run("set_mode_param speed 999");
        CHECK_EQ(f.e.get_current_mode_param("speed"), uint16_t(50));
        f.run("set_mode_param h_gap 70000"); // u16 4464, in range
        CHECK_EQ(f.e.get_current_mode_param("h_gap"), uint16_t(4464));
        f.clear();
        // adj_mode_param: hue wraps, others clamp
        f.run("adj_mode_param speed -100");
        CHECK_EQ(f.e.get_current_mode_param("speed"), uint16_t(1));
        CHECK_EQ(f.l.ev.size(), 1ul);
        f.clear();
        // unknown key: false, nothing announced
        f.run("adj_mode_param nope 5");
        CHECK(f.l.ev.empty());
        CHECK(f.io.writes.empty());
    }
    {
        // Rainbow has no "hue": the key is unknown there (no wrap, nothing happens)
        Fx f(60, 5);
        f.run("set_mode_param hue 10");
        CHECK(f.l.ev.empty());
        f.run("set_mode_param speed 0");
        CHECK_EQ(f.e.get_current_mode_param("speed"), uint16_t(1));
    }
    {
        Fx f;
        f.run("set_mode_param hue 0");
        f.run("set_mode_param sat 255");
        f.clear();
        f.run("adj_mode_param hue -15");
        CHECK_EQ(f.l.ev.size(), 1ul);
        const uint8_t hue_after = uint8_t(f.e.get_current_mode_param("hue"));
        CHECK_EQ(hue_after, r2h(h2r(241, 255, 255))[0]);
    }
    {
        // get_mode_params text
        Fx f;
        f.run("set_mode_param hue 0");
        f.run("set_mode_param sat 255");
        f.clear();
        f.run("get_mode_params");
        CHECK_EQ(f.io.printed,
            V({"Mode 0 (Solid) Parameters:\n  - hue (Hue): 0 [Range: 0-255]\n  - sat "
               "(Saturation): 255 [Range: 0-255]\n"}));
        CHECK(f.l.ev.empty());
        f.clear();
        Fx g(60, 5);
        g.run("get_mode_params");
        CHECK_EQ(g.io.printed,
            V({"Mode 5 (Rainbow) Parameters:\n  - speed (Speed): 5 [Range: 1-20]\n  - "
               "density (Density): 10 [Range: 1-30]\n"}));
    }
    {
        // reset_current_mode: defaults, persisted, not announced
        Fx f(60, 1);
        f.run("set_mode_param speed 40");
        f.clear();
        f.run("reset_current_mode");
        CHECK(f.l.ev.empty());
        CHECK(f.io.writes.empty());
        CHECK_EQ(f.e.get_current_mode_param("speed"), uint16_t(4));
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(245));
        CHECK_EQ((f.store.values[{1, "speed"}]), uint16_t(4));
    }
}

// ---------------------------------------------------------------------------------------------
// 6. Length and colour order
// ---------------------------------------------------------------------------------------------
static void test_length_and_order() {
    {
        Fx f;
        f.run("set_length 601");
        CHECK_EQ(f.io.printed, V({"That's too many. Max supported: 600 LEDs"}));
        CHECK_EQ(f.l.ev, V({"L601@cli"})); // still announced
        CHECK_EQ(f.e.get_length(), uint16_t(60));
        CHECK(f.io.writes.empty());
        CHECK(!f.e.take_blank_frame());
        f.clear();
        f.run("set_length 30");
        CHECK_EQ(f.l.ev, V({"L30@cli"}));
        CHECK_EQ(f.io.writes, V({"num_led=30/u16"}));
        CHECK_EQ(f.e.get_length(), uint16_t(30));
        CHECK(f.e.take_blank_frame());
        CHECK(!f.e.take_blank_frame());
        f.clear();
        f.run("set_length 65566"); // -> 30
        CHECK_EQ(f.l.ev, V({"L30@cli"}));
        f.clear();
        f.run("set_length -1"); // -> 65535: rejected
        CHECK_EQ(f.io.printed.size(), 1ul);
    }
    {
        Fx f;
        f.run("set_color_order bgr");
        CHECK_EQ(f.io.printed, V({"Color order set to BGR"}));
        CHECK_EQ(f.io.writes, V({"colorder=5"}));
        CHECK_EQ(f.e.get_color_order_index(), uint8_t(5));
        CHECK(f.l.ev.empty());
        f.clear();
        // an empty name starts the interactive calibration (Led runs it); nothing else happens
        CHECK(f.e.set_color_order(""));
        CHECK_EQ(f.io.calibrations, 1);
        CHECK(f.io.printed.empty());
        CHECK(f.io.writes.empty());
        const std::vector<std::string> empty_arg{""};
        led_run_command(
            f.e, LED_COMMAND_COUNT - 1, xewe::span<const std::string>(empty_arg.data(), 1));
        CHECK_EQ(f.io.calibrations, 2);
        CHECK_EQ(f.e.get_color_order_index(), uint8_t(5));
        f.clear();
        f.run("set_color_order XYZ");
        CHECK_EQ(f.io.printed, V({"Invalid color order. Use RGB, RBG, GRB, GBR, BRG, or BGR"}));
        CHECK(f.io.writes.empty());
        CHECK_EQ(f.e.get_color_order_index(), uint8_t(5));
        // each order maps the logical r,g,b on the wire
        const char* names[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};
        const std::array<uint8_t, 3> wire[6] = {
            {255, 85, 0}, {255, 0, 85}, {85, 255, 0}, {85, 0, 255}, {0, 255, 85}, {0, 85, 255}};
        for (int i = 0; i < 6; ++i) {
            Fx g(4, 0, {255, 85, 0}, 255, true);
            g.run(std::string("set_color_order ") + names[i]);
            g.settle();
            g.e.render_frame();
            g.e.render_frame();
            const CRGB p = g.e.pixels()[0];
            const auto rgb = g.e.get_rgb();
            std::array<uint8_t, 3> want;
            for (int c = 0; c < 3; ++c)
                want[c] = rgb[(wire[i][c] == 255) ? 0 : (wire[i][c] == 85) ? 1 : 2];
            CHECK_EQ((std::array<uint8_t, 3>{p.r, p.g, p.b}), want);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// 7. Fan-out API: origins, sync_all order, a combined-message listener
// ---------------------------------------------------------------------------------------------
static void test_fanout() {
    {
        Fx f;
        f.e.sync_color({1, 2, 3}, OTHER);
        f.e.sync_brightness(9, OTHER);
        f.e.sync_state(false, OTHER);
        f.e.sync_length(20, OTHER);
        CHECK_EQ(f.l.ev, V({"C1,2,3@other", "B9@other", "S0@other", "L20@other"}));
    }
    {
        // `$led reset` values: the strip applies state, length, mode (which announces its colour),
        // brightness, colour; listeners get one sync_all (default: state, length, mode, brightness,
        // colour)
        Fx f;
        f.e.sync_all({0, 255, 0}, 50, true, 0, 60, OTHER);
        CHECK_EQ(f.l.ev.size(), 6ul);
        CHECK(f.l.ev[0].rfind("C", 0) == 0 && f.l.ev[0].find("@self") != std::string::npos);
        CHECK_EQ(std::vector<std::string>(f.l.ev.begin() + 1, f.l.ev.end()),
            V({"S1@other", "L60@other", "M0@other", "B50@other", "C0,255,0@other"}));
        AllListener a;
        a.self = f.e.self_origin();
        f.e.listeners.add(&a);
        f.e.sync_all({0, 255, 0}, 50, true, 0, 60, nullptr);
        CHECK_EQ(a.ev.back(), "F0,255,0,50,1,0,60@cli");
    }
    {
        // a listener calling back into the engine from a callback (no lock held)
        struct Echo : LedListener {
            LedEngine* e = nullptr;
            int n = 0;
            void on_color(std::array<uint8_t, 3>, const void*) override {
                e->get_rgb();
                ++n;
            }
            void on_brightness(uint8_t, const void*) override {}
            void on_state(bool, const void*) override {}
            void on_mode(uint8_t, const void*) override {
                e->get_all_modes_json();
            }
        } echo;
        Fx f;
        echo.e = &f.e;
        f.e.listeners.add(&echo);
        f.run("set_mode 2");
        CHECK_EQ(echo.n, 1);
    }
}

// ---------------------------------------------------------------------------------------------
// 8. Parameter normalisation table (ModeController::set_mode_param / adj_mode_param)
// ---------------------------------------------------------------------------------------------
static void test_param_clamping() {
    MemStore store;
    std::vector<CRGB> buf(XEWE_LED_NUM_LEDS_MAX);
    host::set_millis(10000);
    ModeController mc(buf.data(), 60, 900, store);
    for (const auto& [id, factory] : ModeRegistry::get_registry()) {
        mc.set_mode(id);
        const auto params = mc.get_current_mode_params();
        const bool has_hue = std::any_of(
            params.begin(), params.end(), [](const ModeParam& p) { return p.key == "hue"; });
        for (const auto& p : params) {
            const bool wraps = p.key == "hue";
            // max + 1 (as the CLI's uint16_t)
            const uint16_t over = uint16_t(p.max_value + 1);
            CHECK(mc.set_mode_param(p.key, over));
            const uint16_t want_over =
                wraps ? uint16_t(over % 256) : std::clamp<uint16_t>(over, p.min_value, p.max_value);
            CHECK_EQ(mc.get_current_mode_param(p.key), want_over);
            // min - 1 wraps to 65535 as uint16_t: clamps to max (hue: 65535 % 256 = 255)
            CHECK(mc.set_mode_param(p.key, uint16_t(p.min_value - 1)));
            const uint16_t under = uint16_t(p.min_value - 1);
            const uint16_t want_under = wraps
                ? uint16_t(under % 256)
                : std::clamp<uint16_t>(under, p.min_value, p.max_value);
            CHECK_EQ(mc.get_current_mode_param(p.key), want_under);
            // adj: below the minimum clamps (hue wraps)
            mc.set_mode_param(p.key, p.min_value);
            CHECK(mc.adj_mode_param(p.key, -1));
            CHECK_EQ(mc.get_current_mode_param(p.key), wraps ? uint16_t(255) : p.min_value);
            CHECK(mc.adj_mode_param(p.key, 1000000));
            CHECK_EQ(mc.get_current_mode_param(p.key),
                wraps ? uint16_t((255 + 1000000) % 256) : p.max_value);
            // stored after every change
            CHECK_EQ((store.values[{id, p.key}]), mc.get_current_mode_param(p.key));
        }
        CHECK(!mc.set_mode_param("no_such_key", 1));
        CHECK(!mc.adj_mode_param("no_such_key", 1));
        if (!has_hue) {
            CHECK(!mc.set_mode_param("hue", 300)); // "hue" is unknown in modes without it
        }
        mc.reset_current_mode();
    }
    // explicit rows of the normalisation table
    mc.set_mode(1);
    mc.set_mode_param("sat", 255);
    CHECK_EQ(mc.get_current_mode_param("sat"), uint16_t(245));
    mc.set_mode(2);
    mc.set_mode_param("blend", 0);
    CHECK_EQ(mc.get_current_mode_param("blend"), uint16_t(2));
    mc.set_mode_param("hue_b", 300); // hue_b clamps, it does not wrap
    CHECK_EQ(mc.get_current_mode_param("hue_b"), uint16_t(255));
    mc.set_mode(4);
    mc.set_mode_param("speed", 256);
    CHECK_EQ(mc.get_current_mode_param("speed"), uint16_t(255));
    mc.set_mode(6);
    mc.set_mode_param("speed", 0);
    CHECK_EQ(mc.get_current_mode_param("speed"), uint16_t(0));
    mc.set_mode_param("density", 0);
    CHECK_EQ(mc.get_current_mode_param("density"), uint16_t(1));
    // stored values are used as they are (not clamped on load)
    store.values[{1, "sat"}] = 255;
    mc.set_mode(1);
    CHECK_EQ(mc.get_current_mode_param("sat"), uint16_t(255));
    // resolution order: defaults < stored < overrides
    mc.set_mode(4, {{"speed", 200}});
    CHECK_EQ(mc.get_current_mode_param("speed"), uint16_t(200));
    mc.set_mode(5);
    mc.set_mode(4);
    CHECK_EQ(mc.get_current_mode_param("speed"), uint16_t(200));
    mc.set_mode(9);
    CHECK_EQ(mc.get_current_mode_id(), uint8_t(0));
    CHECK_EQ(mc.get_mode_transition_delay(), uint16_t(900));
}

// ---------------------------------------------------------------------------------------------
// 9. Chipsets, colour orders, registry
// ---------------------------------------------------------------------------------------------
static void test_tables() {
    static const char* names[46] = {"APA102", "APA102HD", "APA104", "APA106", "DOTSTAR",
        "DOTSTARHD", "GE8822", "GS1903", "GW6205", "GW6205_400KHZ", "HD107", "HD107HD", "LPD1886",
        "LPD1886_8BIT", "LPD6803", "LPD8806", "NEOPIXEL", "P9813", "PL9823", "SK6812", "SK6822",
        "SK9822", "SK9822HD", "SM16703", "SM16716", "SM16824E", "TM1803", "TM1804", "TM1809",
        "TM1812", "TM1829", "UCS1903", "UCS1903B", "UCS1904", "UCS1912", "UCS2903", "WS2801",
        "WS2803", "WS2811", "WS2811_400KHZ", "WS2812", "WS2812B", "WS2813", "WS2815", "WS2816",
        "WS2852"};
    static const int clocked[] = {0, 1, 4, 5, 10, 11, 14, 15, 17, 21, 22, 24, 36, 37};
    CHECK_EQ(LED_CHIPSET_COUNT, 46ul);
    int n_clocked = 0;
    for (std::size_t i = 0; i < LED_CHIPSET_COUNT; ++i) {
        CHECK_EQ(unsigned(LED_CHIPSET_TABLE[i].id), unsigned(i));
        CHECK_EQ(unsigned(LED_CHIPSET_TABLE[i].value), unsigned(i));
        CHECK_EQ(std::string(LED_CHIPSET_TABLE[i].name), std::string(names[i]));
        const bool want =
            std::find(std::begin(clocked), std::end(clocked), int(i)) != std::end(clocked);
        CHECK_EQ(LED_CHIPSET_TABLE[i].clocked, want);
        n_clocked += LED_CHIPSET_TABLE[i].clocked;
    }
    CHECK_EQ(n_clocked, 14);
    CHECK_EQ(LED_COLOR_ORDERS.size(), 6ul);
    const char* orders[6] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};
    for (int i = 0; i < 6; ++i)
        CHECK_EQ(std::string(LED_COLOR_ORDERS[i]), std::string(orders[i]));

    const char* modes[7] = {"Solid", "Color Fade", "Color Fade Two Zone", "Brightness Fade",
        "Pulse", "Rainbow", "Christmas Lights"};
    auto& reg = ModeRegistry::get_registry();
    CHECK_EQ(reg.size(), 7ul);
    uint8_t expect_id = 0;
    for (const auto& [id, factory] : reg) {
        CHECK_EQ(id, expect_id);
        auto m = factory({});
        CHECK_EQ(m->get_id(), id);
        CHECK_EQ(std::string(m->get_name()), std::string(modes[id]));
        ++expect_id;
    }
}

// ---------------------------------------------------------------------------------------------
// 10. Fixes: Two Zone colour, pixels past num_led, missing rgb blob, cleared mode parameters
// ---------------------------------------------------------------------------------------------
static void test_fixes() {
    // Two Zone: get_rgb() is Hue A at full saturation and value
    auto& reg = ModeRegistry::get_registry();
    for (int h = 0; h < 256; h += 15) {
        auto m = reg.at(2)({{"hue", uint16_t(h)}});
        CHECK_EQ(m->get_rgb(), h2r(uint8_t(h), 255, 255));
    }
    {
        // entering mode 2 through the strip: Hue A comes from the rgb round trip of its own colour
        Fx f;
        f.run("set_mode 2");
        CHECK_EQ(f.e.get_current_mode_param("hue"), uint16_t(r2h(h2r(81, 255, 255))[0]));
        CHECK_EQ(f.l.ev[0], "C" + str(h2r(81, 255, 255)) + "@self");
    }
    {
        // pixels past num_led stay black after boot
        Fx f(60, 5, {0, 255, 0}, 255, true);
        for (int i = 0; i < 100; ++i) {
            host::advance_millis(20);
            f.e.render_frame();
        }
        bool tail_black = true;
        for (int i = 60; i < XEWE_LED_NUM_LEDS_MAX; ++i)
            tail_black = tail_black && f.e.pixels()[i] == CRGB(0, 0, 0);
        CHECK(tail_black);
        bool head_lit = false;
        for (int i = 0; i < 60; ++i)
            head_lit = head_lit || !(f.e.pixels()[i] == CRGB(0, 0, 0));
        CHECK(head_lit);
        // shrinking: the command only asks for the black frame (the buffer is untouched until the
        // render task takes it), the black frame covers the old length, then the mode frame
        // renders the new one
        f.run("set_length 10");
        bool untouched = false;
        for (int i = 0; i < 60; ++i)
            untouched = untouched || !(f.e.pixels()[i] == CRGB(0, 0, 0));
        CHECK(untouched);
        CHECK(f.e.take_blank_frame());
        bool black = true;
        for (int i = 0; i < XEWE_LED_NUM_LEDS_MAX; ++i)
            black = black && f.e.pixels()[i] == CRGB(0, 0, 0);
        CHECK(black);
        CHECK(!f.e.take_blank_frame());
        f.e.render_frame();
        bool cleared = true;
        for (int i = 10; i < 60; ++i)
            cleared = cleared && f.e.pixels()[i] == CRGB(0, 0, 0);
        CHECK(cleared);
    }
    {
        // a missing rgb blob at boot leaves mode 0 as built (no out-of-range read)
        RecIo io;
        MemStore store;
        LedEngine e(io);
        e.begin(60, store);
        e.boot(0, {}, 0, 0, false);
        CHECK_EQ(e.get_rgb(), h2r(0, 255, 255));
        CHECK(!e.get_state());
    }
    {
        // mode parameters cleared (as `$led reset` wipes the namespace): defaults come back
        Fx f(60, 1);
        f.run("set_mode_param speed 33");
        CHECK_EQ((f.store.values[{1, "speed"}]), uint16_t(33));
        f.store.values.clear();
        f.e.set_mode(1);
        CHECK_EQ(f.e.get_current_mode_param("speed"), uint16_t(4));
    }
}

// ---------------------------------------------------------------------------------------------
// 11. Status text
// ---------------------------------------------------------------------------------------------
static void test_status_text() {
    Fx f(60, 0, {255, 0, 0}, 50, false);
    LedStatusInput in;
    in.chip = 41;
    in.voltage = 5;
    const std::string want = "Hardware:\n"
                             "    Chip:             WS2812B\n"
                             "    Data Pin:         GPIO_4\n"
                             "    Voltage:          5 V\n"
                             "    Color Order:      GRB\n"
                             "    Length:           60\n"
                             "    Max Length:       600\n\n"
                             "Live State:\n"
                             "    FPS:              0\n"
                             "    Brightness:       50/255\n"
                             "    Power State:      OFF\n"
                             "    Color (RGB):      (255, 0, 0)\n\n"
                             "Power:\n"
                             "    Voltage:          5 V\n"
                             "    Line 1:           0 W, 0 A\n"
                             "    Total:            0 W, 0 A\n\n"
                             "Mode: [0] Solid\n"
                             "    - hue: 0 [0-255] (Hue)\n"
                             "    - sat: 255 [0-255] (Saturation)\n";
    CHECK_EQ(f.e.status_text(in), want);

    in.use_clk = true;
    in.chip = 99;
    in.voltage = 12;
    in.lines = 2;
    in.line_lengths = {30, 40};
    in.fps = 49;
    const std::string want2 = "Hardware:\n"
                              "    Chip:             Unknown\n"
                              "    Data Pin:         GPIO_4\n"
                              "    Clock Pin:        GPIO_3\n"
                              "    Voltage:          12 V\n"
                              "    Color Order:      GRB\n"
                              "    Parallel Lines:   2\n"
                              "    Line 1 Length:    30\n"
                              "    Line 2 Length:    40\n"
                              "    Total LEDs:       70\n"
                              "    Max LEDs/line:    600\n\n"
                              "Live State:\n"
                              "    FPS:              49\n"
                              "    Brightness:       50/255\n"
                              "    Power State:      OFF\n"
                              "    Color (RGB):      (255, 0, 0)\n\n"
                              "Power:\n"
                              "    Voltage:          12 V\n"
                              "    Line 1:           0 W, 0 A\n"
                              "    Line 2:           0 W, 0 A\n"
                              "    Total:            0 W, 0 A\n\n"
                              "Mode: [0] Solid\n"
                              "    - hue: 0 [0-255] (Hue)\n"
                              "    - sat: 255 [0-255] (Saturation)\n";
    CHECK_EQ(f.e.status_text(in), want2);

    // on, full brightness, Solid red on 10 LEDs in GRB order: the power model reads the reordered
    // output buffer, so the red becomes the green channel: 10 * 255 * 55 / 256 + 10 * 5 = 597 mW
    Fx g(10, 0, {255, 0, 0}, 255, true);
    g.settle();
    g.e.render_frame();
    LedStatusInput in3;
    const std::string s = g.e.status_text(in3);
    CHECK(s.find("    Line 1:           0.597 W, 0.1194 A\n") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// 12. Mode parameter tables and the /modes JSON, as literals (id, name; key, display name, min,
//     max, default, step, type)
// ---------------------------------------------------------------------------------------------
static void test_mode_tables_literal() {
    struct P {
        const char* key;
        const char* display;
        unsigned min, max, def, step;
        char type;
    };
    struct M {
        unsigned id;
        const char* name;
        std::vector<P> params;
    };
    const std::vector<M> modes = {
        {0, "Solid",
            {{"hue", "Hue", 0, 255, 0, 1, 'b'}, {"sat", "Saturation", 0, 255, 255, 1, 'b'}}},
        {1, "Color Fade",
            {{"hue", "Hue", 0, 255, 195, 1, 'b'}, {"sat", "Min Saturation", 0, 245, 245, 1, 'b'},
                {"speed", "Speed", 1, 50, 4, 1, 'a'}, {"fire_step", "Density", 1, 255, 20, 1, 'a'},
                {"h_gap", "Color Variance", 0, 65535, 15000, 100, 'a'},
                {"min_bright", "Depth", 0, 255, 150, 1, 'a'}}},
        {2, "Color Fade Two Zone",
            {{"hue", "Hue A", 0, 255, 81, 1, 'b'}, {"hue_b", "Hue B", 0, 255, 225, 1, 'b'},
                {"blend", "Blend", 2, 255, 150, 1, 'a'}, {"speed", "Speed", 1, 50, 3, 1, 'a'},
                {"fire_step", "Density", 1, 255, 10, 1, 'a'},
                {"min_bright", "Depth", 0, 255, 245, 1, 'a'},
                {"min_sat", "Min Sat", 0, 255, 215, 1, 'a'}}},
        {3, "Brightness Fade",
            {{"hue", "Hue", 0, 255, 0, 1, 'b'}, {"sat", "Saturation", 0, 255, 255, 1, 'b'},
                {"speed", "Speed", 1, 50, 5, 1, 'a'}, {"noise_step", "Density", 1, 255, 10, 1, 'a'},
                {"min_bright", "Min Brightness", 0, 255, 10, 1, 'a'}}},
        {4, "Pulse",
            {{"hue", "Hue", 0, 255, 0, 1, 'b'}, {"sat", "Saturation", 0, 255, 255, 1, 'b'},
                {"speed", "Speed", 1, 255, 30, 1, 'a'}}},
        {5, "Rainbow",
            {{"speed", "Speed", 1, 20, 5, 1, 'b'}, {"density", "Density", 1, 30, 10, 1, 'a'}}},
        {6, "Christmas Lights",
            {{"density", "Density", 1, 10, 1, 1, 'b'}, {"speed", "Flicker", 0, 20, 5, 1, 'a'}}},
    };
    auto& reg = ModeRegistry::get_registry();
    CHECK_EQ(reg.size(), modes.size());
    unsigned n_params = 0;
    for (const M& want : modes) {
        CHECK(reg.count(uint8_t(want.id)) == 1);
        if (!reg.count(uint8_t(want.id)))
            continue;
        const auto mode = reg.at(uint8_t(want.id))({});
        const ModeConfig& got = mode->get_config();
        CHECK_EQ(unsigned(got.id), want.id);
        CHECK_EQ(got.name, std::string(want.name));
        CHECK_EQ(got.params.size(), want.params.size());
        for (std::size_t i = 0; i < want.params.size() && i < got.params.size(); ++i) {
            const P& w = want.params[i];
            const ModeParam& g = got.params[i];
            const std::string row = std::string(want.name) + "/" + w.key;
            CHECK_EQ(row + " key " + g.key, row + " key " + w.key);
            CHECK_EQ(row + " display " + g.display_name, row + " display " + w.display);
            CHECK_EQ(
                row + " min " + std::to_string(g.min_value), row + " min " + std::to_string(w.min));
            CHECK_EQ(
                row + " max " + std::to_string(g.max_value), row + " max " + std::to_string(w.max));
            CHECK_EQ(row + " default " + std::to_string(g.default_value),
                row + " default " + std::to_string(w.def));
            CHECK_EQ(row + " step " + std::to_string(g.step_value),
                row + " step " + std::to_string(w.step));
            CHECK_EQ(
                row + " type " + std::string(1, g.type), row + " type " + std::string(1, w.type));
            ++n_params;
        }
    }
    CHECK_EQ(n_params, 27u);

    // the /modes JSON of a device after boot (Solid green: hue 85 and sat 255 live; the other
    // modes show their defaults)
    Fx f;
    const std::string want_json =
        "[{\"id\":0,\"name\":\"Solid\",\"params\":["
        "{\"key\":\"hue\",\"display_name\":\"Hue\",\"min\":0,\"max\":255,\"step\":1,\"value\":85,"
        "\"type\":\"b\"},"
        "{\"key\":\"sat\",\"display_name\":\"Saturation\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":255,\"type\":\"b\"}]},"
        "{\"id\":1,\"name\":\"Color Fade\",\"params\":["
        "{\"key\":\"hue\",\"display_name\":\"Hue\",\"min\":0,\"max\":255,\"step\":1,\"value\":195,"
        "\"type\":\"b\"},"
        "{\"key\":\"sat\",\"display_name\":\"Min Saturation\",\"min\":0,\"max\":245,\"step\":1,"
        "\"value\":245,\"type\":\"b\"},"
        "{\"key\":\"speed\",\"display_name\":\"Speed\",\"min\":1,\"max\":50,\"step\":1,\"value\":4,"
        "\"type\":\"a\"},"
        "{\"key\":\"fire_step\",\"display_name\":\"Density\",\"min\":1,\"max\":255,\"step\":1,"
        "\"value\":20,\"type\":\"a\"},"
        "{\"key\":\"h_gap\",\"display_name\":\"Color "
        "Variance\",\"min\":0,\"max\":65535,\"step\":100,"
        "\"value\":15000,\"type\":\"a\"},"
        "{\"key\":\"min_bright\",\"display_name\":\"Depth\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":150,\"type\":\"a\"}]},"
        "{\"id\":2,\"name\":\"Color Fade Two Zone\",\"params\":["
        "{\"key\":\"hue\",\"display_name\":\"Hue A\",\"min\":0,\"max\":255,\"step\":1,\"value\":81,"
        "\"type\":\"b\"},"
        "{\"key\":\"hue_b\",\"display_name\":\"Hue B\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":225,\"type\":\"b\"},"
        "{\"key\":\"blend\",\"display_name\":\"Blend\",\"min\":2,\"max\":255,\"step\":1,"
        "\"value\":150,\"type\":\"a\"},"
        "{\"key\":\"speed\",\"display_name\":\"Speed\",\"min\":1,\"max\":50,\"step\":1,\"value\":3,"
        "\"type\":\"a\"},"
        "{\"key\":\"fire_step\",\"display_name\":\"Density\",\"min\":1,\"max\":255,\"step\":1,"
        "\"value\":10,\"type\":\"a\"},"
        "{\"key\":\"min_bright\",\"display_name\":\"Depth\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":245,\"type\":\"a\"},"
        "{\"key\":\"min_sat\",\"display_name\":\"Min Sat\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":215,\"type\":\"a\"}]},"
        "{\"id\":3,\"name\":\"Brightness Fade\",\"params\":["
        "{\"key\":\"hue\",\"display_name\":\"Hue\",\"min\":0,\"max\":255,\"step\":1,\"value\":0,"
        "\"type\":\"b\"},"
        "{\"key\":\"sat\",\"display_name\":\"Saturation\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":255,\"type\":\"b\"},"
        "{\"key\":\"speed\",\"display_name\":\"Speed\",\"min\":1,\"max\":50,\"step\":1,\"value\":5,"
        "\"type\":\"a\"},"
        "{\"key\":\"noise_step\",\"display_name\":\"Density\",\"min\":1,\"max\":255,\"step\":1,"
        "\"value\":10,\"type\":\"a\"},"
        "{\"key\":\"min_bright\",\"display_name\":\"Min Brightness\",\"min\":0,\"max\":255,"
        "\"step\":1,\"value\":10,\"type\":\"a\"}]},"
        "{\"id\":4,\"name\":\"Pulse\",\"params\":["
        "{\"key\":\"hue\",\"display_name\":\"Hue\",\"min\":0,\"max\":255,\"step\":1,\"value\":0,"
        "\"type\":\"b\"},"
        "{\"key\":\"sat\",\"display_name\":\"Saturation\",\"min\":0,\"max\":255,\"step\":1,"
        "\"value\":255,\"type\":\"b\"},"
        "{\"key\":\"speed\",\"display_name\":\"Speed\",\"min\":1,\"max\":255,\"step\":1,"
        "\"value\":30,\"type\":\"a\"}]},"
        "{\"id\":5,\"name\":\"Rainbow\",\"params\":["
        "{\"key\":\"speed\",\"display_name\":\"Speed\",\"min\":1,\"max\":20,\"step\":1,\"value\":5,"
        "\"type\":\"b\"},"
        "{\"key\":\"density\",\"display_name\":\"Density\",\"min\":1,\"max\":30,\"step\":1,"
        "\"value\":10,\"type\":\"a\"}]},"
        "{\"id\":6,\"name\":\"Christmas Lights\",\"params\":["
        "{\"key\":\"density\",\"display_name\":\"Density\",\"min\":1,\"max\":10,\"step\":1,"
        "\"value\":1,\"type\":\"b\"},"
        "{\"key\":\"speed\",\"display_name\":\"Flicker\",\"min\":0,\"max\":20,\"step\":1,"
        "\"value\":5,\"type\":\"a\"}]}]";
    CHECK_EQ(f.e.get_all_modes_json(), want_json);
}

// ---------------------------------------------------------------------------------------------
// 13. Lock discipline: a render-task frame may come at any release of the lock; commands stay
//     atomic and the slow effects (NVS, store commit, console, listeners) run without the lock
// ---------------------------------------------------------------------------------------------
struct LockIo : RecIo {
    LedEngine* engine = nullptr;
    bool render_on_unlock = false; // the render task takes the lock as soon as it is free
    int depth = 0;
    int effects_under_lock = 0;
    std::vector<std::string> frames;
    void lock() override {
        ++depth;
    }
    void unlock() override {
        if (--depth == 0 && render_on_unlock && engine != nullptr)
            frame();
    }
    // one pass of the render task: the black frame of a length change, then the mode frame
    void frame() {
        engine->take_blank_frame();
        engine->render_frame();
        uint32_t h = 2166136261u;
        for (int i = 0; i < XEWE_LED_NUM_LEDS_MAX; ++i) {
            const CRGB p = engine->pixels()[i];
            for (uint8_t c : {p.r, p.g, p.b})
                h = (h ^ c) * 16777619u;
        }
        frames.push_back(std::to_string(millis()) + ":" + std::to_string(h));
    }
    void save_u8(const char* k, uint8_t v) override {
        effects_under_lock += depth > 0;
        RecIo::save_u8(k, v);
    }
    void save_u16(const char* k, uint16_t v) override {
        effects_under_lock += depth > 0;
        RecIo::save_u16(k, v);
    }
    void save_bool(const char* k, bool v) override {
        effects_under_lock += depth > 0;
        RecIo::save_bool(k, v);
    }
    void save_rgb(const std::array<uint8_t, 3>& rgb) override {
        effects_under_lock += depth > 0;
        RecIo::save_rgb(rgb);
    }
    void print(std::string_view t) override {
        effects_under_lock += depth > 0;
        RecIo::print(t);
    }
};

struct LockStore : MemStore {
    const LockIo* io = nullptr;
    int commits_under_lock = 0;
    void commit() override {
        commits_under_lock += io->depth > 0;
        MemStore::commit();
    }
};

struct LockFx {
    LockIo io;
    LockStore store;
    LedEngine e{io};
    RecListener l;
    int callbacks_under_lock = 0;
    struct Probe : LedListener {
        LockFx* fx = nullptr;
        void hit() {
            fx->callbacks_under_lock += fx->io.depth > 0;
        }
        void on_color(std::array<uint8_t, 3>, const void*) override {
            hit();
        }
        void on_brightness(uint8_t, const void*) override {
            hit();
        }
        void on_state(bool, const void*) override {
            hit();
        }
        void on_mode(uint8_t, const void*) override {
            hit();
        }
        void on_length(uint16_t, const void*) override {
            hit();
        }
    } probe;

    explicit LockFx(bool render_on_unlock) {
        host::set_millis(10000);
        io.engine = &e;
        store.io = &io;
        e.begin(60, store);
        e.boot(2, {0, 255, 0}, 50, 0, true);
        l.self = e.self_origin();
        e.listeners.add(&l);
        probe.fx = this;
        e.listeners.add(&probe);
        io.render_on_unlock = render_on_unlock;
    }
    void run(const std::string& name, std::vector<std::string> args) {
        for (std::size_t c = 0; c < LED_COMMAND_COUNT; ++c)
            if (name == LED_COMMANDS[c].name)
                led_run_command(e, c, xewe::span<const std::string>(args.data(), args.size()));
    }
};

using LockScript = std::vector<std::pair<std::string, std::vector<std::string>>>;

// one run from the same clock and random seed: with injected=true a frame comes at every release
// of the lock, otherwise one frame after each whole command
static std::unique_ptr<LockFx> play_lock_script(bool injected, const LockScript& script) {
    random16_set_seed(1337);
    auto fx = std::make_unique<LockFx>(injected);
    host::advance_millis(2000); // boot fades done: frames show the modes, not black
    for (const auto& [name, args] : script) {
        host::advance_millis(37);
        fx->run(name, args);
        if (!injected)
            fx->io.frame();
    }
    // sync_all through the API, as `$led reset` does
    host::advance_millis(37);
    fx->e.sync_all({0, 255, 0}, 50, true, 0, 60, OTHER);
    if (!injected)
        fx->io.frame();
    return fx;
}

static void test_lock_discipline() {
    const LockScript script = {{"set_mode", {"1"}}, {"adj_r", {"40"}},
        {"set_mode_param", {"speed", "9"}}, {"adj_mode_param", {"fire_step", "-3"}},
        {"set_mode", {"2"}}, {"adj_hsv", {"10", "-20", "5"}}, {"set_length", {"30"}},
        {"set_mode", {"5"}}, {"set_mode", {"6"}}, {"adj_mode", {"-3"}},
        {"set_rgb", {"1", "2", "3"}}, {"set_brightness", {"200"}}, {"toggle_state", {}},
        {"set_length", {"45"}}, {"reset_current_mode", {}}, {"set_color_order", {"BGR"}}};
    {
        Fx warm(60, 6);
    } // the mode config cache is filled once per process (random16 draws)
    const auto inj = play_lock_script(true, script);
    const auto ref = play_lock_script(false, script);
    CHECK_EQ(inj->io.frames.size(), script.size() + 1);
    std::vector<std::string> distinct = ref->io.frames;
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    CHECK(distinct.size() > script.size() / 2); // the frames differ from each other
    CHECK_EQ(inj->io.frames, ref->io.frames);
    CHECK_EQ(inj->l.ev, ref->l.ev);
    CHECK_EQ(inj->io.writes, ref->io.writes);
    CHECK_EQ(inj->io.printed, ref->io.printed);
    // nothing slow ran under the lock
    CHECK_EQ(inj->io.effects_under_lock, 0);
    CHECK_EQ(inj->store.commits_under_lock, 0);
    CHECK_EQ(inj->callbacks_under_lock, 0);
    CHECK(inj->store.commits > 0);
    CHECK(!inj->io.writes.empty());
    CHECK_EQ(inj->io.depth, 0);
}

// ---------------------------------------------------------------------------------------------
// 14. Values taken from a colour stay inside the mode's ranges
// ---------------------------------------------------------------------------------------------
static void test_colour_params_in_range() {
    {
        // Color Fade's "Min Saturation" is 0..245: a colour or HSV with more stores 245
        Fx f(60, 1);
        f.run("set_rgb 255 0 0");
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(245));
        CHECK_EQ(f.e.get_current_mode_param("hue"), uint16_t(r2h({255, 0, 0})[0]));
        CHECK_EQ((f.store.values[{1, "sat"}]), uint16_t(245));
        CHECK_EQ(f.io.writes, V({"rgb=255,0,0"}));
        CHECK_EQ(f.l.ev, V({"C255,0,0@cli"}));
        f.run("set_hsv 10 250 255");
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(245));
        f.run("set_rgb 255 30 30"); // saturation 225: in range, kept as it is
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(r2h({255, 30, 30})[1]));
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(225));
    }
    {
        // the full-range modes keep saturation 255
        Fx f;
        f.run("set_rgb 255 0 0");
        CHECK_EQ(f.e.get_current_mode_param("sat"), uint16_t(255));
    }
    // ModeController directly, set_rgb and set_hsv, every mode and a spread of colours: every
    // parameter stays inside its declared range and is stored as it is shown
    MemStore store;
    std::vector<CRGB> buf(XEWE_LED_NUM_LEDS_MAX);
    host::set_millis(10000);
    ModeController mc(buf.data(), 60, 900, store);
    mc.set_mode(1);
    mc.set_rgb({255, 0, 0});
    CHECK_EQ(mc.get_current_mode_param("sat"), uint16_t(245));
    mc.set_hsv({10, 250, 255});
    CHECK_EQ(mc.get_current_mode_param("sat"), uint16_t(245));
    mc.set_hsv({10, 200, 255});
    CHECK_EQ(mc.get_current_mode_param("sat"), uint16_t(200));
    CHECK_EQ(mc.get_current_mode_param("hue"), uint16_t(10));
    int out_of_range = 0;
    for (const auto& [id, factory] : ModeRegistry::get_registry()) {
        mc.set_mode(id);
        for (int c = 0; c < 64; ++c) {
            const uint8_t a = uint8_t(c * 37), b = uint8_t(c * 101 + 7), d = uint8_t(255 - c * 3);
            if (c % 2)
                mc.set_rgb({a, b, d});
            else
                mc.set_hsv({a, b, d});
            for (const auto& p : mc.get_current_mode_params()) {
                const uint16_t v = mc.get_current_mode_param(p.key);
                out_of_range += (v < p.min_value || v > p.max_value) ? 1 : 0;
                out_of_range += (store.values[{id, p.key}] != v) ? 1 : 0;
            }
        }
    }
    CHECK_EQ(out_of_range, 0);
}

// ---------------------------------------------------------------------------------------------
// 15. Colour-order calibration, twice, on a GRB strip
// ---------------------------------------------------------------------------------------------
// What a GRB pixel lights for the bytes on the wire: its first byte drives the green LED, the
// second the red one. Returns the calibration menu's answer: 1 red, 2 green, 3 blue, 4 other.
static int grb_pixel_answer(const CRGB& wire) {
    const uint8_t red = wire.g, green = wire.r, blue = wire.b;
    if (red > green && red > blue)
        return 1;
    if (green > red && green > blue)
        return 2;
    if (blue > red && blue > green)
        return 3;
    return 4;
}

// Led::calibrate_color_order's engine steps and answer handling, answered truthfully by a GRB
// pixel; with_reset = the plain RGB order is set first (without it: the earlier sequence)
static std::string calibrate_grb(Fx& f, bool with_reset) {
    if (with_reset)
        f.e.set_color_order_index(0);
    std::string value = "BBB";
    f.e.set_rgb({0, 255, 0});
    f.e.set_brightness(50);
    f.e.set_mode(0);
    f.e.turn_on();
    f.settle();
    f.e.render_frame();
    int answer = grb_pixel_answer(f.e.pixels()[0]);
    if (answer == 4)
        return "other";
    value[answer - 1] = 'G';
    f.e.set_rgb({255, 0, 0});
    f.settle();
    f.e.render_frame();
    answer = grb_pixel_answer(f.e.pixels()[0]);
    if (answer == 4)
        return "other";
    value[answer - 1] = 'R';
    const auto it = std::find(LED_COLOR_ORDERS.begin(), LED_COLOR_ORDERS.end(), value);
    if (it == LED_COLOR_ORDERS.end())
        return "incorrect";
    f.e.set_color_order_index(uint8_t(it - LED_COLOR_ORDERS.begin()));
    return value;
}

static void test_calibration_twice_on_grb() {
    {
        Fx f; // a new device: RGB order
        f.e.set_color_order_index(0);
        CHECK_EQ(calibrate_grb(f, true), std::string("GRB"));
        CHECK_EQ(f.e.get_color_order_index(), uint8_t(2));
        CHECK_EQ(calibrate_grb(f, true), std::string("GRB")); // calibrated again: same order
        CHECK_EQ(f.e.get_color_order_index(), uint8_t(2));
        CHECK_EQ(calibrate_grb(f, true), std::string("GRB"));
    }
    {
        // without the plain RGB order first, the second run reads the answers through GRB: RGB
        Fx f;
        f.e.set_color_order_index(0);
        CHECK_EQ(calibrate_grb(f, false), std::string("GRB"));
        CHECK_EQ(calibrate_grb(f, false), std::string("RGB"));
    }
}

// ---------------------------------------------------------------------------------------------
// 16. `$led set voltage <v>`
// ---------------------------------------------------------------------------------------------
// The `$led set` handler as Led registers it: led_voltage_refusal first; otherwise the core's set
// (Module::apply_setting, verbose), modelled here: it stores the value (one NVS write), echoes
// `<key>=<value>`, then calls on_setting_changed, which accepts 5, 12 and 24.
struct VoltageSet {
    std::vector<std::string> out;
    int nvs_writes = 0;
    uint8_t voltage = 5;
    uint8_t active_voltage = 5;
    void run(const std::string& key, const std::string& value) {
        const std::string refusal = led_voltage_refusal(key, value, active_voltage);
        if (!refusal.empty()) {
            out.push_back(refusal);
            return;
        }
        long long v = 0;
        if (!xewe::str::parse_int(std::string_view(value), v) || v < 5 || v > 24) {
            out.push_back("! $led set voltage: expected u8 5..24");
            return;
        }
        voltage = uint8_t(v);
        ++nvs_writes;
        out.push_back(key + "=" + std::to_string(voltage));
        if (led_voltage_refusal(key, std::to_string(voltage), active_voltage).empty())
            active_voltage = voltage;
    }
};

static void test_voltage_refusal() {
    const std::string bad = "! Led: voltage must be 5, 12 or 24; kept ";
    CHECK_EQ(led_voltage_refusal("voltage", "9", 5), bad + "5");
    CHECK_EQ(led_voltage_refusal("voltage", "23", 12), bad + "12");
    CHECK_EQ(led_voltage_refusal("voltage", " 6 ", 24), bad + "24"); // parsed like the core
    for (const char* ok : {"5", "12", "24"})
        CHECK_EQ(led_voltage_refusal("voltage", ok, 5), std::string());
    // outside 5..24 or not a whole number: the core's own error, not this one
    for (const char* core_error : {"4", "25", "300", "-1", "abc", "", "9.5"})
        CHECK_EQ(led_voltage_refusal("voltage", core_error, 5), std::string());
    // the same reading of the number as the core's parser (xewe::str::parse_int)
    int disagree = 0;
    for (const char* text : {"+9", "09", "9 ", "\t9", " 13\n", "0x9", "9a", "- 9", "+", "-", "007",
             "18", "99999999999999999999", "  ", "1e1"}) {
        long long n = 0;
        const bool core_ok = xewe::str::parse_int(std::string_view(text), n);
        const bool refuse = core_ok && n >= 5 && n <= 24 && n != 5 && n != 12 && n != 24;
        disagree += refuse != !led_voltage_refusal("voltage", text, 5).empty() ? 1 : 0;
    }
    CHECK_EQ(disagree, 0);
    // other keys are never refused here
    CHECK_EQ(led_voltage_refusal("chip", "9", 5), std::string());
    CHECK_EQ(led_voltage_refusal("Voltage", "9", 5), std::string());
    {
        VoltageSet s;
        s.run("voltage", "9"); // refused: nothing echoed, nothing stored
        CHECK_EQ(s.out, V({"! Led: voltage must be 5, 12 or 24; kept 5"}));
        CHECK_EQ(s.nvs_writes, 0);
        CHECK_EQ(s.voltage, uint8_t(5));
        s.out.clear();
        s.run("voltage", "12"); // accepted: one echo, one NVS write
        CHECK_EQ(s.out, V({"voltage=12"}));
        CHECK_EQ(s.nvs_writes, 1);
        CHECK_EQ(s.active_voltage, uint8_t(12));
        s.out.clear();
        s.run("voltage", "9"); // the kept value is the last accepted one
        CHECK_EQ(s.out, V({"! Led: voltage must be 5, 12 or 24; kept 12"}));
        CHECK_EQ(s.nvs_writes, 1);
    }
}

int main() {
    test_command_table();
    test_arg_parsing();
    test_color_commands();
    test_brightness_state_commands();
    test_mode_commands();
    test_length_and_order();
    test_fanout();
    test_param_clamping();
    test_tables();
    test_fixes();
    test_status_text();
    test_mode_tables_literal();
    test_lock_discipline();
    test_colour_params_in_range();
    test_calibration_twice_on_grb();
    test_voltage_refusal();
    if (g_failures == 0)
        std::printf("PASSED: %d check(s), 0 failure(s)\n", g_checks);
    else
        std::printf("FAILED: %d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
