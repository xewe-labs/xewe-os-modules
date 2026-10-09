// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led-strip/tests/host/test_listeners.cpp
//
// Host test of src/LedStrip/LedListener.h (standard library only): built and run by test_led-strip.py
// with g++ -std=c++17 -Wall -Wextra -Werror. Prints one line per check, then
// "PASSED|FAILED: <n> check(s), <f> failure(s)"; exits non-zero on a failure.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/LedStrip/LedListener.h"

static int failures = 0;
static int checks   = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        ++checks;                                                                  \
        if (cond) { std::printf("ok   %s\n", #cond); }                             \
        else      { std::printf("FAIL %s (line %d)\n", #cond, __LINE__); ++failures; } \
    } while (0)

// Records every callback it does not originate itself (the led-web pattern).
struct Recorder : LedListener {
    std::vector<std::string> log;
    void on_brightness(uint8_t v, const void* origin) override { if (origin != this) log.push_back("b" + std::to_string(v)); }
    void on_state(bool on, const void* origin) override        { if (origin != this) log.push_back(on ? "on" : "off"); }
    void on_mode(uint8_t m, const void* origin) override       { if (origin != this) log.push_back("m" + std::to_string(m)); }
    void on_color(uint32_t c, const void* origin) override {
        if (origin == this) return;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "c%06x", static_cast<unsigned>(c));
        log.push_back(buf);
    }
    void on_param(uint8_t m, const char* k, uint16_t v, const void* origin) override {
        if (origin != this) log.push_back("p" + std::to_string(m) + ":" + k + "=" + std::to_string(v));
    }
};

// Only overrides the strip callbacks: the modes callbacks must fall through to the default no-ops.
struct StripOnly : LedListener {
    int calls = 0;
    void on_brightness(uint8_t, const void*) override { ++calls; }
};

// Global order of delivery across listeners.
static std::vector<int> order;
struct Ordered : LedListener {
    int tag;
    explicit Ordered(int t) : tag(t) {}
    void on_state(bool, const void*) override { order.push_back(tag); }
};

int main() {
    // capacity and registration
    {
        LedListeners set;
        Recorder a, b, c, d, e;
        CHECK(LedListeners::capacity() == 4);
        CHECK(set.size() == 0);
        CHECK(!set.add(nullptr));
        CHECK(set.add(&a) && set.add(&b) && set.add(&c) && set.add(&d));
        CHECK(set.size() == 4);
        CHECK(!set.add(&e));                    // full: no heap, no growth
        CHECK(set.add(&a));                     // already registered: true, no duplicate
        CHECK(set.size() == 4);
        CHECK(set.remove(&b));
        CHECK(!set.remove(&b));
        CHECK(!set.remove(&e));
        CHECK(set.size() == 3);
        CHECK(set.add(&e));                     // reuses the freed slot
        CHECK(set.size() == 4);
    }
    // fan-out reaches every listener exactly once, in registration order
    {
        LedListenerSet<LedListener, 4> set;
        Ordered one(1), two(2), three(3);
        set.add(&one); set.add(&two); set.add(&three);
        order.clear();
        set.notify([](LedListener& l) { l.on_state(true, nullptr); });
        CHECK((order == std::vector<int>{1, 2, 3}));
        set.remove(&two);
        order.clear();
        set.notify([](LedListener& l) { l.on_state(false, nullptr); });
        CHECK((order == std::vector<int>{1, 3}));
        Ordered four(4);
        set.add(&four);                         // takes slot 2: delivered between 1 and 3
        order.clear();
        set.notify([](LedListener& l) { l.on_state(true, nullptr); });
        CHECK((order == std::vector<int>{1, 4, 3}));
    }
    // echo suppression via origin, all five callbacks
    {
        LedListeners set;
        Recorder web, other;
        set.add(&web); set.add(&other);
        const void* cli = nullptr;
        set.notify([&](LedListener& l) { l.on_brightness(77, &web); });
        set.notify([&](LedListener& l) { l.on_state(false, cli); });
        set.notify([&](LedListener& l) { l.on_mode(5, &other); l.on_color(0xffffff, &other); });
        set.notify([&](LedListener& l) { l.on_param(0, "hue", 85, &web); l.on_color(led_pack_rgb(0, 255, 0), &web); });
        CHECK((web.log == std::vector<std::string>{"off", "m5", "cffffff"}));
        CHECK((other.log == std::vector<std::string>{"b77", "off", "p0:hue=85", "c00ff00"}));
    }
    // default bodies are no-ops (a strip-only listener can sit in the same set as the modes events)
    {
        LedListeners set;
        StripOnly s;
        set.add(&s);
        set.notify([](LedListener& l) {
            l.on_mode(1, nullptr); l.on_color(1, nullptr); l.on_param(1, "speed", 3, nullptr); l.on_state(true, nullptr);
        });
        CHECK(s.calls == 0);
        set.notify([](LedListener& l) { l.on_brightness(1, nullptr); });
        CHECK(s.calls == 1);
        LedListener base;
        base.on_param(0, "hue", 0, nullptr);    // callable on the base type
        CHECK(true);
    }
    // a listener removed by a callback is skipped later in the same pass, others still run
    {
        LedListeners set;
        Recorder a, b;
        struct Remover : LedListener {
            LedListeners* set = nullptr; LedListener* victim = nullptr;
            void on_state(bool, const void*) override { set->remove(victim); }
        } r;
        r.set = &set; r.victim = &b;
        set.add(&a); set.add(&r); set.add(&b);
        set.notify([](LedListener& l) { l.on_state(true, nullptr); });
        CHECK((a.log == std::vector<std::string>{"on"}));
        CHECK(b.log.empty());
        CHECK(set.size() == 2);
    }
    // rrggbb packing (LedModes::get_color, on_color)
    CHECK(led_pack_rgb(0x12, 0x34, 0x56) == 0x123456u);
    CHECK(led_pack_rgb(255, 0, 0) == 0xff0000u);
    CHECK(led_pack_rgb(0, 0, 0) == 0u);
    static_assert(led_pack_rgb(0, 255, 0) == 0x00ff00u, "constexpr packing");

    std::printf("%s: %d check(s), %d failure(s)\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
