// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/tests/unit/golden_driver.cpp
//
// Copy of harness/oracle.cpp of the 2.3.0 golden oracle (folder XEWE_LED_GOLDEN_DIR), compiled
// against the led module through golden_port_adapter.h. Every change is marked "PORT EDIT n":
//   1  include the adapter instead of the 2.3.0 ModuleController
//   2  LedStrip::leds            -> LedStrip::port_leds()
//   3  LedStrip::mode_controller -> LedStrip::port_mode_controller()
//   4  ModeController(buf, n, ms, nvs, "mc") -> ModeController(buf, n, ms, mc_store): the module's
//      ModeController takes a ModeParamStore; the adapter's store keeps the 2.3.0 "mc" key layout
// Everything else (scenarios, timing, seeds, CRCs, JSON) is the oracle's, unchanged.
//
// xewe-led-os 2.3.0 golden oracle driver.
//
// Links against the UNMODIFIED 2.3.0 LED engine (legacy_src/) + FastLED 3.10.3 (stub platform)
// + the host shims (shims/). One scenario file per process invocation so that every scenario
// starts from pristine static state (FastLED rand16seed = 1337, empty ModeController
// config cache, empty FastLED controller list, empty NVS) -- the same state as a fresh boot.
//
//   oracle --list                 print scenario names (one per line)
//   oracle --run NAME OUTDIR      write OUTDIR/NAME.json
//   oracle --manifest OUTDIR      write OUTDIR/MANIFEST.json
//
// A port reuses this file unchanged by providing the same headers/types at the same include
// path (see README.md, "How a port links against this").

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

#include "golden_port_adapter.h" // PORT EDIT 1: the led module engine behind the 2.3.0 names
#include "host_shim.h"

#ifndef ORACLE_VARIANT
#define ORACLE_VARIANT "riscv"
#endif
#ifndef FASTLED_GIT_COMMIT
#define FASTLED_GIT_COMMIT "unknown"
#endif

// =====================================================================================
// Deterministic heap: every allocation is pre-filled with 0xA5 so reads of members the
// 2.3.0 code never initialises (e.g. FadeColorTwoZone::base_rgb) are reproducible on the
// host. Values that depend on such reads are NOT recorded as goldens (see README).
// =====================================================================================
#ifndef ORACLE_HEAP_FILL
#define ORACLE_HEAP_FILL 0xA5
#endif
static constexpr unsigned char HEAP_FILL = ORACLE_HEAP_FILL;
static void* fill_alloc(std::size_t n, std::size_t align) {
    if (n == 0)
        n = 1;
    void* p = nullptr;
    if (align <= alignof(std::max_align_t))
        p = std::malloc(n);
    else if (posix_memalign(&p, align, n) != 0)
        p = nullptr;
    if (!p)
        throw std::bad_alloc();
    std::memset(p, HEAP_FILL, n);
    return p;
}
void* operator new(std::size_t n) {
    return fill_alloc(n, 0);
}
void* operator new[](std::size_t n) {
    return fill_alloc(n, 0);
}
void* operator new(std::size_t n, std::align_val_t a) {
    return fill_alloc(n, (std::size_t)a);
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return fill_alloc(n, (std::size_t)a);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

// =====================================================================================
// Read-only access to a few private members without patching 2.3.0 (explicit template
// instantiation is exempt from access checking, [temp.spec.general]/6). Only pure getters
// are invoked through these, never anything that advances AsyncTimer state.
// =====================================================================================
template <typename Tag, typename Tag::type M> struct Rob {
    friend typename Tag::type rob(Tag) {
        return M;
    }
};
#define ROB(TAG, TYPE, MEMBER) \
    struct TAG { \
        using type = TYPE; \
        friend type rob(TAG); \
    }; \
    template struct Rob<TAG, &MEMBER>;

using TimerPtr = std::unique_ptr<AsyncTimer<uint8_t>>;
// PORT EDIT 2: LedStrip::leds is read through LedStrip::port_leds() (adapter)
// PORT EDIT 3: LedStrip::mode_controller is read through LedStrip::port_mode_controller() (adapter)
ROB(RobMCTimer, TimerPtr ModeController::*, ModeController::transition_timer)
ROB(RobMCNum, uint16_t ModeController::*, ModeController::num_leds)
ROB(RobMCStatic, bool ModeController::*, ModeController::buffer_old_static_flag)
ROB(RobMCNorm, uint16_t (ModeController::*)(std::string_view, int32_t) const,
    ModeController::normalize_mode_param_value)

// =====================================================================================
// Constants (all recorded in MANIFEST.json)
// =====================================================================================
static constexpr uint32_t T0_MS = 10000; // fake millis() at scenario start
static constexpr uint32_t FRAME_MS = 20; // LedStripConfig::frame_delay (2.3.0)
static constexpr uint16_t RAND16_SEED = 1337; // FastLED lib8tion.cpp RAND16_SEED (boot value)
static constexpr uint16_t MODE_TRANS_MS = 900; // LedStripConfig::mode_transition_delay
static constexpr uint16_t BRIGHT_TRANS_MS = 500; // LedStripConfig::brightness_transition_delay
static const std::vector<int> SAMPLE_FRAMES = {0, 1, 2, 5, 10, 30, 60, 120, 300, 600, 1500};
static const std::vector<uint16_t> LED_COUNTS = {1, 7, 60, 300, 600};
static constexpr int FIRST_N = 16;
static const CRGB SENTINEL = CRGB(0x5A, 0xC3, 0x3C);

// =====================================================================================
// CRC-32 (IEEE 802.3 / zlib: reflected poly 0xEDB88320, init 0xFFFFFFFF, final xor)
// over the bytes r,g,b of LEDs [0, n) in index order.
// =====================================================================================
static uint32_t crc_update(uint32_t crc, const uint8_t* p, std::size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}
static uint32_t crc_leds(const CRGB* leds, int n) {
    uint32_t crc = 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t b[3] = {leds[i].r, leds[i].g, leds[i].b};
        crc = crc_update(crc, b, 3);
    }
    return crc;
}
static uint32_t crc_chain(uint32_t chain, uint32_t frame_crc) {
    const uint8_t b[4] = {(uint8_t)frame_crc, (uint8_t)(frame_crc >> 8), (uint8_t)(frame_crc >> 16),
        (uint8_t)(frame_crc >> 24)};
    return crc_update(chain, b, 4);
}

// =====================================================================================
// JSON helpers (deterministic text; one frame per line for readable diffs)
// =====================================================================================
static std::string fmt(const char* f, ...) {
    va_list ap;
    va_start(ap, f);
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, f, ap);
    va_end(ap);
    std::string out((std::size_t)n + 1, '\0');
    std::vsnprintf(out.data(), out.size(), f, ap2);
    va_end(ap2);
    out.resize((std::size_t)n);
    return out;
}
static std::string hex32(uint32_t v) {
    return fmt("\"%08x\"", v);
}
static std::string jstr(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((unsigned char)c < 0x20)
            o += fmt("\\u%04x", (unsigned char)c);
        else
            o += c;
    }
    return o + "\"";
}
static std::string jrgb(const std::array<uint8_t, 3>& c) {
    return fmt("[%u,%u,%u]", c[0], c[1], c[2]);
}
static std::string jfirst(const CRGB* leds, int n) {
    std::string o = "[";
    for (int i = 0; i < std::min(n, FIRST_N); ++i) {
        if (i)
            o += ",";
        o += fmt("[%u,%u,%u]", leds[i].r, leds[i].g, leds[i].b);
    }
    return o + "]";
}
static std::string jparams(const std::map<std::string, uint16_t>& m) {
    std::string o = "{";
    bool first = true;
    for (auto& [k, v] : m) {
        if (!first)
            o += ",";
        first = false;
        o += jstr(k) + ":" + std::to_string(v);
    }
    return o + "}";
}
static std::string jdouble(double d) {
    return fmt("%.17g", d);
}
static std::string jnvs() {
    std::string o = "{";
    bool fns = true;
    for (auto& [ns, kv] : host::nvs_dump()) {
        if (!fns)
            o += ",";
        fns = false;
        o += jstr(ns) + ":{";
        bool fk = true;
        for (auto& [k, e] : kv) {
            if (!fk)
                o += ",";
            fk = false;
            std::string bytes;
            for (std::size_t i = 0; i < e.bytes.size(); ++i)
                bytes += (i ? "," : "") + std::to_string(e.bytes[i]);
            o += jstr(k) + ":{\"type\":\"" + std::string(1, e.type) + "\",\"bytes\":[" + bytes +
                "]}";
        }
        o += "}";
    }
    return o + "}";
}
static std::string jmeta(const std::vector<ModeParam>& ps) {
    std::string o = "[";
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];
        if (i)
            o += ",";
        o += fmt("{\"key\":%s,\"display_name\":%s,\"min\":%u,\"max\":%u,\"default\":%u,\"step\":%u,"
                 "\"type\":\"%c\"}",
            jstr(p.key).c_str(), jstr(p.display_name).c_str(), p.min_value, p.max_value,
            p.default_value, p.step_value, p.type);
    }
    return o + "]";
}

struct Out {
    FILE* f;
    explicit Out(const std::string& path) : f(std::fopen(path.c_str(), "w")) {
        if (!f) {
            std::perror(path.c_str());
            std::exit(2);
        }
    }
    ~Out() {
        std::fclose(f);
    }
    void w(const std::string& s) {
        std::fputs(s.c_str(), f);
    }
};

static std::string common_header(const char* scenario, const char* kind) {
    return fmt("  \"scenario\": %s,\n  \"kind\": %s,\n  \"t0_ms\": %u,\n  \"frame_ms\": %u,\n  "
               "\"rand16_seed\": %u,\n",
        jstr(scenario).c_str(), jstr(kind).c_str(), T0_MS, FRAME_MS, RAND16_SEED);
}

// Mode 2 (FadeColorTwoZone) get_rgb() returns an uninitialised member in 2.3.0.
static bool get_rgb_is_defined(uint8_t mode_id) {
    return mode_id != 2;
}

// =====================================================================================
// Scenario: mode_frames_<id>  -- Mode::loop(CRGB*, n) on a directly constructed mode
// =====================================================================================
static uint16_t snap(uint32_t v, const ModeParam& p) {
    const uint32_t step = p.step_value ? p.step_value : 1;
    return (uint16_t)(p.min_value + ((v - p.min_value) / step) * step);
}
struct ParamSet {
    std::string label;
    std::map<std::string, uint16_t> params;
};
static std::vector<ParamSet> param_sets_for(const std::vector<ModeParam>& meta) {
    std::map<std::string, uint16_t> defaults;
    for (auto& p : meta)
        defaults[p.key] = p.default_value;
    std::vector<ParamSet> sets{{"default", defaults}};
    for (auto& p : meta) {
        const uint32_t span = (uint32_t)p.max_value - p.min_value;
        const std::vector<std::pair<std::string, uint16_t>> vals = {
            {"min", p.min_value},
            {"mid1", snap(p.min_value + span / 3, p)},
            {"mid2", snap(p.min_value + (2 * span) / 3, p)},
            {"max", p.max_value},
        };
        for (auto& [tag, v] : vals) {
            auto m = defaults;
            m[p.key] = v;
            sets.push_back({p.key + "=" + tag + "(" + std::to_string(v) + ")", m});
        }
    }
    // hue/sat combinations at inputs where xewe::color::hsv_to_rgb rounds differently with the
    // ESP32-S3's fused multiply-subtract (see README "Float semantics"); defaults for the rest.
    if (defaults.count("hue") && defaults.count("sat")) {
        uint16_t sat_max = 255;
        for (auto& p : meta)
            if (p.key == "sat")
                sat_max = p.max_value;
        for (auto [h, sat] : std::vector<std::pair<uint16_t, uint16_t>>{
                 {10, 85}, {60, 119}, {136, 25}, {190, 119}, {251, 85}}) {
            auto m = defaults;
            m["hue"] = h;
            m["sat"] = std::min<uint16_t>(sat, sat_max);
            sets.push_back({fmt("combo hue=%u sat=%u", h, m["sat"]), m});
        }
    }
    return sets;
}

static void scen_mode_frames(uint8_t id, const std::string& name, const std::string& dir) {
    auto& reg = ModeRegistry::get_registry();
    auto meta = reg.at(id)({})->get_params(); // temp instance only to read the config
    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "mode_frames"));
    o.w(fmt("  \"mode_id\": %u,\n  \"mode_name\": %s,\n", id,
        jstr(reg.at(id)({})->get_name()).c_str()));
    o.w("  \"params_meta\": " + jmeta(meta) + ",\n");
    o.w("  \"sample_frames\": [0,1,2,5,10,30,60,120,300,600,1500],\n");
    o.w("  \"procedure\": \"rand16seed:=1337; millis:=t0; m=factory(params); "
        "buf[0..600):=sentinel; for k in 0..1500 { if k>0 millis+=20; m->loop(buf,n); "
        "crc32(buf[0..n)) }\",\n");
    o.w("  \"cases\": [\n");
    bool first_case = true;
    for (auto& ps : param_sets_for(meta)) {
        for (uint16_t n : LED_COUNTS) {
            random16_set_seed(RAND16_SEED);
            host::clock_set(T0_MS);
            auto mode = reg.at(id)(ps.params);
            std::vector<CRGB> buf(LED_STRIP_NUM_LEDS_MAX, SENTINEL);
            uint32_t chain = 0;
            std::string frames;
            for (int k = 0; k <= SAMPLE_FRAMES.back(); ++k) {
                if (k > 0)
                    host::clock_advance(FRAME_MS);
                mode->loop(buf.data(), n);
                const uint32_t c = crc_leds(buf.data(), n);
                chain = crc_chain(chain, c);
                if (std::find(SAMPLE_FRAMES.begin(), SAMPLE_FRAMES.end(), k) !=
                    SAMPLE_FRAMES.end()) {
                    if (!frames.empty())
                        frames += ",\n";
                    frames += fmt("        {\"frame\":%d,\"t_ms\":%u,\"crc32\":%s,\"first16\":%s}",
                        k, host::clock_now(), hex32(c).c_str(), jfirst(buf.data(), n).c_str());
                }
            }
            bool beyond = false;
            for (int i = n; i < LED_STRIP_NUM_LEDS_MAX; ++i)
                if (!(buf[i] == SENTINEL))
                    beyond = true;
            o.w(std::string(first_case ? "" : ",\n") + "    {\n");
            first_case = false;
            o.w("      \"label\": " + jstr(ps.label) +
                ",\n      \"params\": " + jparams(ps.params) + ",\n");
            o.w(fmt("      \"num_leds\": %u,\n", n));
            o.w("      \"get_rgb\": " +
                (get_rgb_is_defined(id) ? jrgb(mode->get_rgb()) : std::string("null")) + ",\n");
            o.w(std::string("      \"writes_beyond_num_leds\": ") + (beyond ? "true" : "false") +
                ",\n");
            o.w("      \"crc_chain_all_frames\": " + hex32(chain) + ",\n");
            o.w("      \"frames\": [\n" + frames + "\n      ]\n    }");
        }
    }
    o.w("\n  ]\n}\n");
}

// =====================================================================================
// Scenario: normalize  -- ModeController::normalize_mode_param_value table
// =====================================================================================
static void scen_normalize(const std::string& name, const std::string& dir) {
    random16_set_seed(RAND16_SEED);
    host::clock_set(T0_MS);
    auto mcc = std::make_unique<ModuleController>();
    std::vector<CRGB> buf(LED_STRIP_NUM_LEDS_MAX, SENTINEL);
    ModeController mc(buf.data(), 60, MODE_TRANS_MS, mcc->mc_store) /* PORT EDIT 4 */;
    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "normalize"));
    o.w("  \"procedure\": \"mc.set_mode(id); out = mc.normalize_mode_param_value(key, in) for "
        "every key of the mode plus 'hue' (when absent) and an unknown key\",\n");
    o.w("  \"modes\": [\n");
    auto& reg = ModeRegistry::get_registry();
    bool fm = true;
    for (auto& [id, factory] : reg) {
        mc.set_mode(id);
        auto params = mc.get_current_mode_params();
        std::vector<std::string> keys;
        for (auto& p : params)
            keys.push_back(p.key);
        if (std::find(keys.begin(), keys.end(), "hue") == keys.end())
            keys.push_back("hue");
        keys.push_back("no_such_key");
        o.w(std::string(fm ? "" : ",\n") +
            fmt("    {\"mode_id\": %u, \"mode_name\": %s, \"keys\": [\n", id,
                jstr(mc.get_current_mode_name()).c_str()));
        fm = false;
        bool fk = true;
        for (auto& key : keys) {
            int64_t lo = 0, hi = 65535;
            for (auto& p : params)
                if (p.key == key) {
                    lo = p.min_value;
                    hi = p.max_value;
                }
            std::set<int64_t> ins = {INT32_MIN, -65536, -1000, -257, -256, -255, -2, -1, 0, 1, 2,
                254, 255, 256, 257, 511, 512, 1000, 65534, 65535, 65536, 65537, 100000, INT32_MAX,
                lo - 1, lo, lo + 1, (lo + hi) / 2, hi - 1, hi, hi + 1};
            std::string rows;
            for (int64_t in : ins) {
                if (in < INT32_MIN || in > INT32_MAX)
                    continue;
                const uint16_t out = (mc.*rob(RobMCNorm{}))(key, (int32_t)in);
                rows += (rows.empty() ? "" : ",") + fmt("[%" PRId64 ",%u]", in, out);
            }
            o.w(std::string(fk ? "" : ",\n") + "      {\"key\": " + jstr(key) + ", \"in_out\": [" +
                rows + "]}");
            fk = false;
        }
        o.w("\n    ]}");
    }
    o.w("\n  ]\n}\n");
}

// =====================================================================================
// Scenario: brightness  -- Brightness 500 ms AsyncTimer fade curves
// =====================================================================================
struct BriCase {
    std::string name;
    std::function<void(std::string&)> run;
};
static std::string bri_sample(Brightness& b, uint32_t rel) {
    const uint8_t cur = b.get_current_value();
    const std::array<uint8_t, 3> dim = b.get_dimmed_color(std::array<uint8_t, 3>{255, 128, 1});
    return fmt("[%u,%u,%s]", rel, cur, jrgb(dim).c_str());
}
static void scen_brightness(const std::string& name, const std::string& dir) {
    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "brightness"));
    o.w(fmt("  \"transition_ms\": %u,\n", BRIGHT_TRANS_MS));
    o.w("  \"sample_format\": \"[t_rel_ms, get_current_value(), "
        "get_dimmed_color({255,128,1})]\",\n");
    o.w("  \"note\": \"Each sample calls get_current_value() then get_dimmed_color() at the same "
        "millis(); AsyncTimer recomputes progress at most every 5 ms (calc_interval_ms), so the 1 "
        "ms series shows held values.\",\n");
    o.w("  \"cases\": [\n");
    std::vector<std::string> cases;

    // (a) set_brightness(a -> b) while ON, sampled every `step` ms for 0..520 ms
    const std::vector<std::pair<int, int>> pairs = {{0, 255}, {255, 0}, {50, 200}, {200, 50},
        {1, 2}, {128, 128}, {255, 254}, {10, 245}, {0, 1}, {3, 250}};
    for (uint32_t step : {10u, 1u}) {
        for (auto [a, bv] : pairs) {
            host::clock_set(T0_MS - 1000);
            Brightness b(BRIGHT_TRANS_MS, (uint8_t)a, true); // ctor fade a->a, finished by T0
            host::clock_set(T0_MS);
            b.set_brightness((uint8_t)bv);
            std::string s;
            for (uint32_t t = 0; t <= 520; t += step) {
                host::clock_set(T0_MS + t);
                s += (s.empty() ? "" : ",") + bri_sample(b, t);
            }
            cases.push_back(
                fmt("    {\"name\": \"set_%d_to_%d_step%ums\", \"ctor\": [500,%d,true], \"ops\": "
                    "\"t=0: set_brightness(%d)\", \"last_brightness\": %u, \"samples\": [%s]}",
                    a, bv, step, a, bv, b.get_last_brightness(), s.c_str()));
        }
    }
    // (b) retarget mid-fade: 0->255 at t=0, ->0 at t=250 (and ->128 at t=100)
    {
        host::clock_set(T0_MS - 1000);
        Brightness b(BRIGHT_TRANS_MS, 0, true);
        host::clock_set(T0_MS);
        b.set_brightness(255);
        std::string s;
        for (uint32_t t = 0; t <= 800; t += 10) {
            host::clock_set(T0_MS + t);
            if (t == 250)
                b.set_brightness(0);
            s += (s.empty() ? "" : ",") + bri_sample(b, t);
        }
        cases.push_back("    {\"name\": \"retarget_0_255_then_0_at_250\", \"ctor\": [500,0,true], "
                        "\"ops\": \"t=0: set_brightness(255); t=250: set_brightness(0) (before "
                        "sampling)\", \"last_brightness\": " +
            std::to_string(b.get_last_brightness()) + ", \"samples\": [" + s + "]}");
    }
    {
        host::clock_set(T0_MS - 1000);
        Brightness b(BRIGHT_TRANS_MS, 200, true);
        host::clock_set(T0_MS);
        b.set_brightness(20);
        std::string s;
        for (uint32_t t = 0; t <= 700; t += 10) {
            host::clock_set(T0_MS + t);
            if (t == 100)
                b.set_brightness(128);
            s += (s.empty() ? "" : ",") + bri_sample(b, t);
        }
        cases.push_back("    {\"name\": \"retarget_200_20_then_128_at_100\", \"ctor\": "
                        "[500,200,true], \"ops\": \"t=0: set_brightness(20); t=100: "
                        "set_brightness(128)\", \"last_brightness\": " +
            std::to_string(b.get_last_brightness()) + ", \"samples\": [" + s + "]}");
    }
    // (c) LedStrip's construction: Brightness(500, 0, false); set_brightness while OFF; turn_on;
    // turn_off
    {
        host::clock_set(T0_MS - 1000);
        Brightness b(BRIGHT_TRANS_MS, 0, false);
        host::clock_set(T0_MS - 500);
        b.set_brightness(128); // OFF: timer untouched, last_brightness = 128
        std::string s;
        for (uint32_t t = 0; t <= 1600; t += 10) {
            host::clock_set(T0_MS + t);
            if (t == 0)
                b.turn_on();
            if (t == 700)
                b.turn_off();
            if (t == 900)
                b.turn_on(); // during turn_off fade? no: turn_off fade is 700..1200, this retargets
            s += (s.empty() ? "" : ",") +
                fmt("[%u,%u,%s,%d]", t, b.get_current_value(),
                    jrgb(b.get_dimmed_color(std::array<uint8_t, 3>{255, 128, 1})).c_str(),
                    b.get_state() ? 1 : 0);
        }
        cases.push_back("    {\"name\": \"off_ctor_set128_turn_on_off_on\", \"ctor\": "
                        "[500,0,false], \"ops\": \"t=-500: set_brightness(128) while off; t=0: "
                        "turn_on(); t=700: turn_off(); t=900: turn_on()\", \"sample_format\": "
                        "\"[t_rel_ms, value, dimmed, state]\", \"last_brightness\": " +
            std::to_string(b.get_last_brightness()) + ", \"samples\": [" + s + "]}");
    }
    {
        host::clock_set(T0_MS - 1000);
        Brightness b(BRIGHT_TRANS_MS, 77, true);
        std::string s;
        host::clock_set(T0_MS);
        b.turn_off();
        for (uint32_t t = 0; t <= 600; t += 10) {
            host::clock_set(T0_MS + t);
            s += (s.empty() ? "" : ",") +
                fmt("[%u,%u,%s,%d,%u]", t, b.get_current_value(),
                    jrgb(b.get_dimmed_color(std::array<uint8_t, 3>{255, 128, 1})).c_str(),
                    b.get_state() ? 1 : 0, b.get_dimmed_color((uint8_t)200));
        }
        cases.push_back("    {\"name\": \"on77_turn_off\", \"ctor\": [500,77,true], \"ops\": "
                        "\"t=0: turn_off()\", \"sample_format\": \"[t_rel_ms, value, dimmed_rgb, "
                        "state, get_dimmed_color(200)]\", \"last_brightness\": " +
            std::to_string(b.get_last_brightness()) + ", \"samples\": [" + s + "]}");
    }
    // (d) get_dimmed_color(uint8) table at fixed brightness values (timer done)
    {
        std::string rows;
        for (int bv : {0, 1, 2, 50, 127, 128, 200, 254, 255}) {
            host::clock_set(T0_MS - 1000);
            Brightness b(BRIGHT_TRANS_MS, (uint8_t)bv, true);
            host::clock_set(T0_MS);
            std::string r;
            for (int c : {0, 1, 2, 127, 128, 254, 255})
                r += (r.empty() ? "" : ",") + std::to_string(b.get_dimmed_color((uint8_t)c));
            rows += (rows.empty() ? "" : ",") + fmt("[%d,[%s]]", bv, r.c_str());
        }
        cases.push_back("    {\"name\": \"dimmed_color_table\", \"format\": \"[brightness, "
                        "get_dimmed_color(c) for c in [0,1,2,127,128,254,255]]\", \"rows\": [" +
            rows + "]}");
    }
    for (std::size_t i = 0; i < cases.size(); ++i)
        o.w(cases[i] + (i + 1 < cases.size() ? ",\n" : "\n"));
    o.w("  ]\n}\n");
}

// =====================================================================================
// Scenario: mc_*  -- ModeController (900 ms cross-fade), rendered once per 20 ms frame
// =====================================================================================
struct MCOp {
    int frame;
    std::string desc;
    std::function<void(ModeController&)> fn;
};
static void scen_mc(const std::string& name, const std::string& dir, uint16_t n, int total_frames,
    const std::vector<MCOp>& ops, const std::string& description) {
    random16_set_seed(RAND16_SEED);
    host::clock_set(T0_MS);
    auto mcc = std::make_unique<ModuleController>();
    std::vector<CRGB> buf(LED_STRIP_NUM_LEDS_MAX, SENTINEL);
    ModeController mc(
        buf.data(), n, MODE_TRANS_MS, mcc->mc_store) /* PORT EDIT 4 */; // ctor: set_mode(0) at t0

    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "mode_controller"));
    o.w("  \"description\": " + jstr(description) + ",\n");
    o.w(fmt("  \"num_leds\": %u,\n  \"transition_ms\": %u,\n", n, MODE_TRANS_MS));
    o.w("  \"procedure\": \"rand16seed:=1337; millis:=t0; ModeController(buf, n, 900, nvs, 'mc'); "
        "for k in 0..N { if k>0 millis+=20; apply ops scheduled at k; mc.loop(); record }\",\n");
    std::string ops_s;
    for (auto& op : ops)
        ops_s += (ops_s.empty() ? "" : ",") +
            fmt("{\"frame\":%d,\"op\":%s}", op.frame, jstr(op.desc).c_str());
    o.w("  \"ops\": [" + ops_s + "],\n");
    o.w("  \"frame_format\": \"{frame, t_ms, mode_id, transition_active, "
        "transition_progress(double, AsyncTimer::get_progress), old_static, crc32, first16}\",\n");
    o.w("  \"frames\": [\n");
    uint32_t chain = 0;
    for (int k = 0; k <= total_frames; ++k) {
        if (k > 0)
            host::clock_advance(FRAME_MS);
        for (auto& op : ops)
            if (op.frame == k)
                op.fn(mc);
        mc.loop();
        const uint32_t c = crc_leds(buf.data(), n);
        chain = crc_chain(chain, c);
        auto& tt = mc.*rob(RobMCTimer{});
        o.w(fmt("    "
                "{\"frame\":%d,\"t_ms\":%u,\"mode_id\":%u,\"transition_active\":%s,\"transition_"
                "progress\":%s,\"old_static\":%s,\"crc32\":%s,\"first16\":%s}%s\n",
            k, host::clock_now(), mc.get_current_mode_id(), tt->is_active() ? "true" : "false",
            jdouble(tt->get_progress()).c_str(), (mc.*rob(RobMCStatic{})) ? "true" : "false",
            hex32(c).c_str(), jfirst(buf.data(), n).c_str(), k < total_frames ? "," : ""));
    }
    o.w("  ],\n");
    o.w("  \"crc_chain_all_frames\": " + hex32(chain) + ",\n");
    std::map<std::string, uint16_t> params;
    for (auto& p : mc.get_current_mode_params())
        params[p.key] = mc.get_current_mode_param(p.key);
    o.w("  \"final_mode_id\": " + std::to_string(mc.get_current_mode_id()) + ",\n");
    o.w("  \"final_params\": " + jparams(params) + ",\n");
    o.w("  \"final_get_rgb\": " +
        (get_rgb_is_defined(mc.get_current_mode_id()) ? jrgb(mc.get_rgb()) : std::string("null")) +
        ",\n");
    o.w("  \"nvs\": " + jnvs() + "\n}\n");
}

// =====================================================================================
// Scenario: strip_*  -- full LedStrip pipeline (frame timer, Brightness dimming, color
// order, double set_mode through ModuleController::sync_color), boot from configured NVS.
// =====================================================================================
static uint64_t g_shows = 0;
class CountingController : public CLEDController { // counts FastLED.show() calls == renders
public:
    void init() override {}
    void showColor(const CRGB&, int, uint8_t) override {
        ++g_shows;
    }
    void show(const CRGB*, int, uint8_t) override {
        ++g_shows;
    }
};
static CRGB g_dummy_led[1];

struct StripCfg {
    uint16_t num_led;
    uint8_t chip;
    uint8_t colorder;
    std::array<uint8_t, 3> rgb;
    uint8_t brightness;
    uint8_t mode_id;
    bool state;
};
struct StripOp {
    uint32_t t_rel_ms; // relative to t0
    std::string desc;
    std::function<void(LedStrip&)> fn;
};
// Strip scenarios that enter mode 2 through LedStrip (boot or set_mode): LedStrip::set_mode()
// calls controller.sync_color(get_rgb()) and FadeColorTwoZone::get_rgb() returns its
// never-initialised base_rgb member, which then becomes the "hue" param via set_rgb().
static std::set<std::string> g_uninit_scenarios;
static bool strip_reads_uninit(const StripCfg& cfg, const std::vector<StripOp>& ops) {
    if (cfg.mode_id == 2)
        return true;
    for (auto& op : ops)
        if (op.desc.find("set_mode(2)") != std::string::npos)
            return true;
    return false;
}
static std::string strip_state_json(LedStrip& s) {
    auto& mc = s.port_mode_controller(); // PORT EDIT 3
    std::map<std::string, uint16_t> params;
    for (auto& p : mc.get_current_mode_params())
        params[p.key] = mc.get_current_mode_param(p.key);
    const uint8_t mid = s.get_current_mode_id();
    return fmt("{\"mode_id\":%u,\"mode_name\":%s,\"params\":%s,\"get_rgb\":%s,\"brightness\":%u,"
               "\"state\":%s,\"length\":%u,\"mode_controller_num_leds\":%u}",
        mid, jstr(s.get_current_mode_name()).c_str(), jparams(params).c_str(),
        get_rgb_is_defined(mid) ? jrgb(s.get_rgb()).c_str() : "null", s.get_brightness(),
        s.get_state() ? "true" : "false", s.get_length(), mc.*rob(RobMCNum{}));
}
// poll_per_ms: how many times LedStrip::loop() runs per fake millisecond (2 == "fast poll",
// the canonical model: renders every 20 ms; 1 == one call per ms: renders every 21 ms).
static void scen_strip(const std::string& name, const std::string& dir, const StripCfg& cfg,
    const std::vector<StripOp>& ops, uint32_t duration_ms, int poll_per_ms,
    const std::string& description, int record_all_first = 100) {
    random16_set_seed(RAND16_SEED);
    host::clock_set(T0_MS);
    FastLED.addLeds(new CountingController(), g_dummy_led, 1);
    auto mcc = std::make_unique<ModuleController>();
    auto& nvs = mcc->nvs;
    // A configured (non-first) boot, as Module::begin() sees it.
    nvs.write<bool>("led", "not_first_boot", true);
    nvs.write<bool>("led", "is_enabled", true);
    nvs.write<bool>("led", "init_complete", true);
    nvs.write<uint16_t>("led", "num_led", cfg.num_led);
    nvs.write<uint8_t>("led", "chip", cfg.chip);
    nvs.write<uint8_t>("led", "colorder", cfg.colorder);
    nvs.write_blob("led", "rgb", std::vector<uint8_t>(cfg.rgb.begin(), cfg.rgb.end()));
    nvs.write<uint8_t>("led", "brightness", cfg.brightness);
    nvs.write<uint8_t>("led", "mode_id", cfg.mode_id);
    nvs.write<bool>("led", "state", cfg.state);
    nvs.write<bool>("led", "use_clk", false);
    nvs.write<uint8_t>("led", "voltage", 5);
    nvs.write<uint16_t>("led", "lines", 0);

    LedStrip& s = mcc->led_strip;
    // Module::begin() for a configured device == required + regular + common.
    s.begin_routines_required(LedStripConfig{});
    s.begin_routines_regular(LedStripConfig{});
    const std::string boot_state = strip_state_json(s);
    // begin_routines_common() == run_with_dots(loop, 1080 ms) followed by the main loop(); both
    // just poll LedStrip::loop(), which the polling below does from t0 onward.

    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "led_strip"));
    o.w("  \"description\": " + jstr(description) + ",\n");
    if (strip_reads_uninit(cfg, ops))
        o.w("  \"depends_on_uninitialised_memory\": \"YES: mode 2 is entered via LedStrip, so its "
            "hue param (and NVS led/rgb) come from FadeColorTwoZone::base_rgb, which 2.3.0 never "
            "initialises. The host value is the harness heap fill 0xA5 -> rgb(165,165,165) -> hue "
            "0. NOT reproducible on a device; a port should not try to match these frames.\",\n");
    o.w(fmt("  \"boot_nvs_led\": "
            "{\"num_led\":%u,\"chip\":%u,\"colorder\":%u,\"rgb\":%s,\"brightness\":%u,\"mode_id\":%"
            "u,\"state\":%s},\n",
        cfg.num_led, cfg.chip, cfg.colorder, jrgb(cfg.rgb).c_str(), cfg.brightness, cfg.mode_id,
        cfg.state ? "true" : "false"));
    o.w(fmt("  \"poll_calls_per_ms\": %d,\n  \"duration_ms\": %u,\n", poll_per_ms, duration_ms));
    o.w("  \"procedure\": \"rand16seed:=1337; millis:=t0; NVS preloaded; "
        "begin_routines_required(); begin_routines_regular(); then for t in [t0, t0+duration): "
        "apply ops scheduled at t; call LedStrip::loop() poll_calls_per_ms times; a render = a "
        "FastLED.show() inside loop(); record LedStrip::leds[0..num_led) after each render\",\n");
    std::string ops_s;
    for (auto& op : ops)
        ops_s += (ops_s.empty() ? "" : ",") +
            fmt("{\"t_rel_ms\":%u,\"op\":%s}", op.t_rel_ms, jstr(op.desc).c_str());
    o.w("  \"ops\": [" + ops_s + "],\n");
    o.w("  \"state_after_boot\": " + boot_state + ",\n");
    o.w("  \"render_format\": \"{render, t_ms, crc32, first16} -- every render for the first N "
        "renders after boot and after each op, then every 10th\",\n");
    o.w("  \"renders\": [\n");
    std::string rendered;
    std::vector<std::pair<uint32_t, std::string>> states;
    uint64_t render = 0;
    uint32_t chain = 0;
    uint64_t since_event = 0;
    std::vector<uint32_t> render_ms;
    for (uint32_t t = 0; t < duration_ms; ++t) {
        host::clock_set(T0_MS + t);
        for (auto& op : ops)
            if (op.t_rel_ms == t) {
                op.fn(s);
                states.push_back({t, strip_state_json(s)});
                since_event = 0;
            }
        for (int p = 0; p < poll_per_ms; ++p) {
            const uint64_t before = g_shows;
            s.loop();
            if (g_shows != before) {
                const CRGB* leds = s.port_leds(); // PORT EDIT 2
                const uint16_t n = s.get_length();
                const uint32_t c = crc_leds(leds, n);
                chain = crc_chain(chain, c);
                render_ms.push_back(t);
                if (since_event < (uint64_t)record_all_first || render % 10 == 0) {
                    if (!rendered.empty())
                        rendered += ",\n";
                    rendered +=
                        fmt("    {\"render\":%" PRIu64 ",\"t_ms\":%u,\"crc32\":%s,\"first16\":%s}",
                            render, host::clock_now(), hex32(c).c_str(), jfirst(leds, n).c_str());
                }
                ++render;
                ++since_event;
            }
        }
    }
    o.w(rendered + "\n  ],\n");
    o.w("  \"render_count\": " + std::to_string(render) + ",\n");
    std::string rms;
    for (std::size_t i = 0; i < std::min<std::size_t>(render_ms.size(), 12); ++i)
        rms += (i ? "," : "") + std::to_string(render_ms[i]);
    o.w("  \"first_render_t_rel_ms\": [" + rms + "],\n");
    o.w("  \"crc_chain_all_renders\": " + hex32(chain) + ",\n");
    std::string st;
    for (auto& [t, js] : states)
        st += (st.empty() ? "" : ",\n") + fmt("    {\"t_rel_ms\":%u,\"state\":", t) + js + "}";
    o.w("  \"state_after_ops\": [\n" + st + "\n  ],\n");
    o.w("  \"final_state\": " + strip_state_json(s) + ",\n");
    o.w(fmt("  \"delay_calls\": %" PRIu64 ",\n", host::delay_calls()));
    o.w("  \"nvs\": " + jnvs() + "\n}\n");
}

// =====================================================================================
// Scenario: catalogue -- the 2.3.0 mode catalogue (get_all_modes_json() verbatim + table)
// =====================================================================================
static void scen_catalogue(const std::string& name, const std::string& dir) {
    random16_set_seed(RAND16_SEED);
    host::clock_set(T0_MS);
    auto mcc = std::make_unique<ModuleController>();
    std::vector<CRGB> buf(LED_STRIP_NUM_LEDS_MAX, SENTINEL);
    ModeController mc(buf.data(), 60, MODE_TRANS_MS, mcc->mc_store) /* PORT EDIT 4 */;
    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "catalogue"));
    o.w("  \"get_all_modes_json\": " + jstr(mc.get_all_modes_json()) + ",\n");
    o.w("  \"modes\": [\n");
    auto& reg = ModeRegistry::get_registry();
    bool f = true;
    for (auto& [id, factory] : reg) {
        auto m = factory({});
        o.w(std::string(f ? "" : ",\n") +
            fmt("    {\"id\":%u,\"name\":%s,\"params\":", id, jstr(m->get_name()).c_str()) +
            jmeta(m->get_params()) + "}");
        f = false;
    }
    o.w("\n  ],\n");
    o.w("  \"mode_transition_delay_ms\": " + std::to_string(mc.get_mode_transition_delay()) +
        ",\n");
    o.w(fmt(
        "  \"led_strip_num_leds_max\": %d,\n  \"led_pin_data\": %d,\n  \"led_pin_clock\": %d\n}\n",
        LED_STRIP_NUM_LEDS_MAX, LED_PIN_DATA, LED_PIN_CLOCK));
}

// =====================================================================================
// Scenario: xewe_color -- exhaustive tables of the 2.3.0 float colour helpers
// =====================================================================================
static void scen_xewe_color(const std::string& name, const std::string& dir) {
    Out o(dir + "/" + name + ".json");
    o.w("{\n" + common_header(name.c_str(), "xewe_color"));
    o.w("  \"procedure\": \"crc32 over the 3 output bytes for every input; inputs iterated "
        "a=0..255 (outer), b, c (inner); per-row crcs restart at 0 for each a\",\n");
    uint32_t h2r_all = 0, r2h_all = 0;
    std::string rows_h2r, rows_r2h;
    for (int a = 0; a < 256; ++a) {
        uint32_t h2r_row = 0, r2h_row = 0;
        for (int b = 0; b < 256; ++b)
            for (int c = 0; c < 256; ++c) {
                const auto x = xewe::color::hsv_to_rgb({(uint8_t)a, (uint8_t)b, (uint8_t)c});
                const auto y = xewe::color::rgb_to_hsv({(uint8_t)a, (uint8_t)b, (uint8_t)c});
                h2r_all = crc_update(h2r_all, x.data(), 3);
                r2h_all = crc_update(r2h_all, y.data(), 3);
                h2r_row = crc_update(h2r_row, x.data(), 3);
                r2h_row = crc_update(r2h_row, y.data(), 3);
            }
        rows_h2r += (a ? "," : "") + hex32(h2r_row);
        rows_r2h += (a ? "," : "") + hex32(r2h_row);
    }
    o.w("  \"hsv_to_rgb_crc32_all\": " + hex32(h2r_all) + ",\n");
    o.w("  \"rgb_to_hsv_crc32_all\": " + hex32(r2h_all) + ",\n");
    o.w("  \"hsv_to_rgb_crc32_per_h\": [" + rows_h2r + "],\n");
    o.w("  \"rgb_to_hsv_crc32_per_r\": [" + rows_r2h + "],\n");
    // explicit values for v=255 (the value every mode constructor uses), all h, a few s
    o.w("  \"hsv_to_rgb_v255\": {\n");
    const std::vector<int> sats = {0, 25, 34, 85, 119, 128, 200, 245, 255};
    for (std::size_t k = 0; k < sats.size(); ++k) {
        std::string r;
        for (int h = 0; h < 256; ++h)
            r +=
                (h ? "," : "") + jrgb(xewe::color::hsv_to_rgb({(uint8_t)h, (uint8_t)sats[k], 255}));
        o.w(fmt("    \"s=%d\": [", sats[k]) + r + "]" + (k + 1 < sats.size() ? ",\n" : "\n"));
    }
    o.w("  }\n}\n");
}

// =====================================================================================
// Registry of scenarios
// =====================================================================================
using Runner = std::function<void(const std::string& name, const std::string& dir)>;
static std::vector<std::pair<std::string, Runner>> build_scenarios() {
    std::vector<std::pair<std::string, Runner>> v;
    v.push_back({"catalogue", scen_catalogue});
    v.push_back({"normalize", scen_normalize});
    v.push_back({"brightness", scen_brightness});
    v.push_back({"xewe_color", scen_xewe_color});
    static const char* names[] = {"solid", "fade_color", "fade_color_two_zone", "fade_brightness",
        "pulse", "rainbow", "christmas_lights"};
    for (uint8_t id = 0; id < 7; ++id)
        v.push_back({fmt("mode_frames_%u_%s", id, names[id]),
            [id](const std::string& n, const std::string& d) { scen_mode_frames(id, n, d); }});

    // ---- ModeController cross-fades: boot into 0, set_mode(from) at frame 60, set_mode(to) at
    // frame 120
    auto pair_case = [&v](uint8_t from, uint8_t to, uint16_t n) {
        v.push_back({fmt("mc_xfade_%u_to_%u_n%u", from, to, n),
            [from, to, n](const std::string& nm, const std::string& d) {
                scen_mc(nm, d, n, 180,
                    {{60, fmt("set_mode(%u)", from),
                         [from](ModeController& m) { m.set_mode(from); }},
                        {120, fmt("set_mode(%u)", to),
                            [to](ModeController& m) { m.set_mode(to); }}},
                    "boot fade (mode 0 from black), then set_mode(from) at frame 60, set_mode(to) "
                    "at frame 120; every frame recorded");
            }});
    };
    for (uint8_t a = 0; a < 7; ++a)
        for (uint8_t b = 0; b < 7; ++b)
            pair_case(a, b, 60);
    for (auto [a, b] : std::vector<std::pair<uint8_t, uint8_t>>{
             {0, 5}, {5, 6}, {6, 2}, {2, 1}, {1, 3}, {3, 4}, {4, 0}}) {
        pair_case(a, b, 1);
        pair_case(a, b, 600);
    }
    v.push_back({"mc_switch_mid_transition_n60",
        [](const std::string& nm, const std::string& d) {
            scen_mc(nm, d, 60, 180,
                {{60, "set_mode(5)", [](ModeController& m) { m.set_mode(5); }},
                    {80, "set_mode(6) (5 is mid-fade: buffer_old frozen)",
                        [](ModeController& m) { m.set_mode(6); }},
                    {90, "set_mode(2) (mid-fade again)", [](ModeController& m) { m.set_mode(2); }}},
                "set_mode while a transition is active: buffer_old is computed once and frozen "
                "(buffer_old_static_flag)");
        }});
    v.push_back(
        {"mc_boot_switch_at_t0_n60", [](const std::string& nm, const std::string& d) {
             scen_mc(nm, d, 60, 80, {{0, "set_mode(6)", [](ModeController& m) { m.set_mode(6); }}},
                 "set_mode during the constructor's own boot transition, before the first loop()");
         }});
    v.push_back(
        {"mc_set_mode_param_rainbow_speed_n60", [](const std::string& nm, const std::string& d) {
             scen_mc(nm, d, 60, 180,
                 {{60, "set_mode(5)", [](ModeController& m) { m.set_mode(5); }},
                     {120, "set_mode_param(speed, 20)",
                         [](ModeController& m) { m.set_mode_param("speed", 20); }},
                     {150, "set_mode_param(density, 999) (clamped)",
                         [](ModeController& m) { m.set_mode_param("density", 999); }},
                     {160, "set_mode_param(nope, 1) (unknown key: no-op)",
                         [](ModeController& m) { m.set_mode_param("nope", 1); }}},
                 "set_mode_param rebuilds the mode (cross-fade between two instances, new instance "
                 "restarts its counters)");
         }});
    v.push_back({"mc_set_rgb_hsv_n60", [](const std::string& nm, const std::string& d) {
                     scen_mc(nm, d, 60, 200,
                         {{60, "set_mode(1)", [](ModeController& m) { m.set_mode(1); }},
                             {100, "set_rgb({255,64,0})",
                                 [](ModeController& m) { m.set_rgb({255, 64, 0}); }},
                             {140, "set_mode(0)", [](ModeController& m) { m.set_mode(0); }},
                             {150, "set_hsv({160,200,77})",
                                 [](ModeController& m) { m.set_hsv({160, 200, 77}); }},
                             {170, "set_rgb({12,34,56})",
                                 [](ModeController& m) { m.set_rgb({12, 34, 56}); }}},
                         "set_rgb/set_hsv map through rgb_to_hsv/hsv_to_rgb (float, lossy) into "
                         "hue/sat params");
                 }});
    v.push_back({"mc_adj_and_reset_n60", [](const std::string& nm, const std::string& d) {
                     scen_mc(nm, d, 60, 200,
                         {{60, "set_mode(3)", [](ModeController& m) { m.set_mode(3); }},
                             {100, "adj_mode_param(hue, -10) (wraps)",
                                 [](ModeController& m) { m.adj_mode_param("hue", -10); }},
                             {120, "adj_mode_param(speed, +100) (clamps)",
                                 [](ModeController& m) { m.adj_mode_param("speed", 100); }},
                             {160, "reset_current_mode()",
                                 [](ModeController& m) { m.reset_current_mode(); }}},
                         "adj_mode_param (success path only; the not-found path falls off the end "
                         "of a bool function in 2.3.0 = UB, not exercised)");
                 }});
    v.push_back(
        {"mc_nvs_restore_n60", [](const std::string& nm, const std::string& d) {
             scen_mc(nm, d, 60, 160,
                 {{60, "set_mode(4, {speed:200,hue:96})",
                      [](ModeController& m) { m.set_mode(4, {{"speed", 200}, {"hue", 96}}); }},
                     {100, "set_mode(5)", [](ModeController& m) { m.set_mode(5); }},
                     {130, "set_mode(4) (params restored from NVS 'mc')",
                         [](ModeController& m) { m.set_mode(4); }},
                     {140, "set_mode(9) (unknown id -> 0)",
                         [](ModeController& m) { m.set_mode(9); }}},
                 "param resolution order defaults < NVS < caller overrides; NVS keys m:<id>:<key> "
                 "in namespace mc");
         }});
    v.push_back(
        {"mc_set_length_n60", [](const std::string& nm, const std::string& d) {
             scen_mc(nm, d, 60, 140,
                 {{60, "set_mode(2)", [](ModeController& m) { m.set_mode(2); }},
                     {100, "set_length(30)", [](ModeController& m) { m.set_length(30); }},
                     {120, "set_length(60)", [](ModeController& m) { m.set_length(60); }}},
                 "set_length: FadeColorTwoZone resets its previous_frame buffer when n changes");
         }});

    // ---- Full LedStrip pipeline
    const StripCfg factory_default{
        60, 41, 2, {0, 255, 0}, 50, 0, true}; // LedStrip::reset() values, WS2812B, GRB
    for (auto& [nm, cfg, desc] : std::vector<std::tuple<std::string, StripCfg, std::string>>{
             {"strip_boot_solid_green_b50_grb_n60", factory_default, "factory-reset configuration"},
             {"strip_boot_rainbow_b255_rgb_n60", {60, 41, 0, {255, 0, 0}, 255, 5, true}, ""},
             {"strip_boot_christmas_b128_bgr_n600", {600, 41, 5, {10, 20, 30}, 128, 6, true},
                 "max length; ChristmasLights draws via get_mode_config cache + 2 constructions"},
             {"strip_boot_twozone_b200_rbg_n300", {300, 41, 1, {0, 0, 255}, 200, 2, true}, ""},
             {"strip_boot_fadecolor_b255_gbr_n7", {7, 41, 3, {255, 128, 0}, 255, 1, true}, ""},
             {"strip_boot_fadebright_b90_brg_n1", {1, 41, 4, {0, 128, 255}, 90, 3, true}, ""},
             {"strip_boot_pulse_off_n60", {60, 41, 0, {255, 0, 255}, 255, 4, false},
                 "state off at boot"}}) {
        StripCfg c = cfg;
        std::string d = desc;
        if (strip_reads_uninit(c, {}))
            g_uninit_scenarios.insert(nm);
        v.push_back({nm, [c, d](const std::string& n, const std::string& dir) {
                         scen_strip(n, dir, c, {}, 3000, 2,
                             "boot from NVS (" + d + "), 3 s of fast-poll main loop");
                     }});
    }
    v.push_back({"strip_cadence_poll1_n60",
        [factory_default](const std::string& n, const std::string& dir) {
            scen_strip(n, dir, factory_default, {}, 2000, 1,
                "same as strip_boot_solid_green but LedStrip::loop() is called once per ms: "
                "AsyncTimer 5 ms throttle makes the period 21 ms");
        }});
    for (uint8_t to = 0; to < 7; ++to) {
        if (to == 2)
            g_uninit_scenarios.insert(fmt("strip_set_mode_0_to_%u_n60", to));
        v.push_back({fmt("strip_set_mode_0_to_%u_n60", to),
            [factory_default, to](const std::string& n, const std::string& dir) {
                scen_strip(n, dir, factory_default,
                    {{1500, fmt("set_brightness(255)"), [](LedStrip& s) { s.set_brightness(255); }},
                        {2500, fmt("set_mode(%u)", to), [to](LedStrip& s) { s.set_mode(to); }}},
                    4000, 2,
                    "LedStrip::set_mode = ModeController::set_mode + sync_color -> "
                    "LedStrip::set_rgb -> second ModeController::set_mode with rgb->hsv "
                    "round-tripped params");
            }});
    }
    StripCfg rb = factory_default;
    rb.mode_id = 5;
    v.push_back(
        {"strip_brightness_ops_rainbow_n60", [rb](const std::string& n, const std::string& dir) {
             scen_strip(n, dir, rb,
                 {{1500, "set_brightness(255)", [](LedStrip& s) { s.set_brightness(255); }},
                     {1750, "set_brightness(10) (mid-fade retarget)",
                         [](LedStrip& s) { s.set_brightness(10); }},
                     {2500, "adj_brightness(+100)", [](LedStrip& s) { s.adj_brightness(100); }},
                     {3200, "turn_off()", [](LedStrip& s) { s.turn_off(); }},
                     {4000, "turn_on()", [](LedStrip& s) { s.turn_on(); }},
                     {4300, "toggle_state()", [](LedStrip& s) { s.toggle_state(); }},
                     {4400, "toggle_state()", [](LedStrip& s) { s.toggle_state(); }}},
                 5200, 2, "Brightness fades as seen in the dimmed output buffer", 60);
         }});
    StripCfg fc = factory_default;
    fc.mode_id = 1;
    fc.brightness = 255;
    v.push_back(
        {"strip_color_ops_fadecolor_n60", [fc](const std::string& n, const std::string& dir) {
             scen_strip(n, dir, fc,
                 {{1500, "set_rgb(255,0,0)", [](LedStrip& s) { s.set_rgb(255, 0, 0); }},
                     {2000, "set_hsv({160,255,255})",
                         [](LedStrip& s) { s.set_hsv(std::array<uint8_t, 3>{160, 255, 255}); }},
                     {2500, "adj_h(+40)", [](LedStrip& s) { s.adj_h(40); }},
                     {3000, "set_mode_param(speed, 50)",
                         [](LedStrip& s) { s.set_mode_param("speed", 50); }},
                     {3500, "adj_mode_param(h_gap, -20000)",
                         [](LedStrip& s) { s.adj_mode_param("h_gap", -20000); }},
                     {4000, "reset_current_mode()", [](LedStrip& s) { s.reset_current_mode(); }}},
                 5000, 2, "color/param ops on Color Fade", 60);
         }});
    v.push_back({"strip_hsv_ops_solid_n60",
        [factory_default](const std::string& n, const std::string& dir) {
            StripCfg c = factory_default;
            c.brightness = 255;
            scen_strip(n, dir, c,
                {{1500, "set_hsv({10,85,255})",
                     [](LedStrip& s) { s.set_hsv(std::array<uint8_t, 3>{10, 85, 255}); }},
                    {2000, "set_hsv({190,119,200})",
                        [](LedStrip& s) { s.set_hsv(std::array<uint8_t, 3>{190, 119, 200}); }},
                    {2500, "set_rgb(255,149,135)", [](LedStrip& s) { s.set_rgb(255, 149, 135); }},
                    {3000, "adj_s(-30)", [](LedStrip& s) { s.adj_s(-30); }},
                    {3500, "set_mode(4)", [](LedStrip& s) { s.set_mode(4); }},
                    {3600, "set_h(60)", [](LedStrip& s) { s.set_h(60); }}},
                4500, 2,
                "Solid/Pulse colour ops through rgb<->hsv (hits FMA-sensitive hsv_to_rgb inputs)",
                40);
        }});
    v.push_back({"strip_length_colororder_n60",
        [factory_default](const std::string& n, const std::string& dir) {
            StripCfg c = factory_default;
            c.mode_id = 5;
            c.brightness = 255;
            scen_strip(n, dir, c,
                {{1500, "set_length(30)", [](LedStrip& s) { s.set_length(30); }},
                    {2000, "set_color_order(\"bgr\")",
                        [](LedStrip& s) { s.set_color_order("bgr"); }},
                    {2500, "set_length(600)", [](LedStrip& s) { s.set_length(600); }},
                    {3000, "set_length(601) (rejected)", [](LedStrip& s) { s.set_length(601); }},
                    {3200, "set_color_order(\"XYZ\") (rejected)",
                        [](LedStrip& s) { s.set_color_order("XYZ"); }}},
                4000, 2,
                "length and color order changes (note: until set_length is called after boot, "
                "ModeController renders LED_STRIP_NUM_LEDS_MAX LEDs)",
                40);
        }});
    return v;
}

static void write_manifest(
    const std::string& dir, const std::vector<std::pair<std::string, Runner>>& sc) {
    Out o(dir + "/MANIFEST.json");
    o.w("{\n");
    o.w("  \"oracle\": \"xewe-led-os 2.3.0 LED engine golden frames\",\n");
    o.w("  \"variant\": \"" ORACLE_VARIANT "\",\n");
    o.w("  \"variant_meaning\": \"riscv = ESP32-C3/C6 float semantics (soft-float, no FMA; legacy "
        "XeWeColor.h unmodified). esp32s3 = ESP32-S3 float semantics (xtensa gcc fuses 1-f*s and "
        "1-(1-f)*s in hsv_to_rgb into msub.s; reproduced by "
        "shims/patches-esp32s3/legacy-xewecolor-esp32s3-fma.diff)\",\n");
    o.w("  \"legacy_commit\": \"91bd344\",\n  \"legacy_tag\": \"v2.3.0\",\n");
    o.w("  \"fastled_version\": \"3.10.3\",\n  \"fastled_commit\": \"" FASTLED_GIT_COMMIT "\",\n");
    o.w("  \"fastled_platform\": \"stub (FASTLED_STUB_IMPL), time_stub.cpp replaced by "
        "shims/src/fake_arduino.cpp\",\n");
    o.w("  \"arduino_core_semantics\": \"arduino-esp32 3.3.7 (map/constrain/min/max); long treated "
        "as 32-bit in map()\",\n");
    o.w(fmt("  \"fake_clock\": {\"t0_ms\": %u, \"frame_ms\": %u, \"micros\": \"millis*1000\", "
            "\"delay\": \"advances clock (never called by the LED code; counted)\"},\n",
        T0_MS, FRAME_MS));
    o.w("  \"frame_cadence\": \"mode_frames_* and mc_*: one render per 20 ms "
        "(LedStripConfig::frame_delay=20 with a main loop that polls faster than 1 kHz). strip_*: "
        "LedStrip::loop() polled 2x per fake ms (renders every 20 ms) except strip_cadence_poll1 "
        "(1x per ms -> 21 ms).\",\n");
    o.w(fmt("  \"randomness\": {\"sources\": [\"FastLED random16() in ChristmasLights ctor (600 "
            "draws, LED_STRIP_NUM_LEDS_MAX)\"], \"rand16_seed\": %u, \"seeding\": \"rand16seed set "
            "to 1337 (FastLED's own boot value) at scenario start; mode_frames_* re-seed before "
            "each case; esp_random/random()/rand() are not used by the 2.3.0 LED code and are "
            "deliberately not provided by the shims\"},\n",
        RAND16_SEED));
    o.w("  \"uninitialised_memory\": \"heap allocations pre-filled with 0xA5; "
        "FadeColorTwoZone::get_rgb() (uninitialised member) is recorded as null; the strip "
        "scenarios listed in depends_on_uninitialised_memory unavoidably fold it into their frames "
        "and are host-only\",\n");
    {
        std::string l;
        for (auto& n : g_uninit_scenarios)
            l += (l.empty() ? "" : ",") + jstr(n + ".json");
        o.w("  \"depends_on_uninitialised_memory\": [" + l + "],\n");
    }
    o.w("  \"crc\": \"CRC-32/ISO-HDLC (zlib crc32) over r,g,b bytes of LEDs [0,n) in index order; "
        "hex lowercase, 8 digits\",\n");
    o.w("  \"crc_chain\": \"chain=0; for each frame: chain = crc32(chain, frame_crc as 4 "
        "little-endian bytes)\",\n");
    o.w("  \"led_counts\": [1,7,60,300,600],\n  \"sample_frames\": "
        "[0,1,2,5,10,30,60,120,300,600,1500],\n");
    o.w("  \"scenario_families\": {\n");
    o.w("    \"catalogue\": \"mode ids/names/params exactly as 2.3.0 reports them "
        "(get_all_modes_json verbatim)\",\n");
    o.w("    \"normalize\": \"ModeController::normalize_mode_param_value(key, int32) input->output "
        "table for every mode and key (+ 'hue' when absent, + unknown key)\",\n");
    o.w("    \"brightness\": \"Brightness 500 ms AsyncTimer curves: set_brightness a->b sampled "
        "every 10 ms and every 1 ms, mid-fade retargets, turn_on/turn_off, get_dimmed_color "
        "table\",\n");
    o.w("    \"xewe_color\": \"exhaustive CRCs of xewe::color::hsv_to_rgb / rgb_to_hsv over all "
        "2^24 inputs + explicit v=255 rows\",\n");
    o.w("    \"mode_frames_<id>_<name>\": \"Mode::loop(CRGB*, n) of a directly constructed mode; "
        "param sets = defaults, each param at min/mid1/mid2/max, hue/sat combos; n in "
        "{1,7,60,300,600}; CRC32 + first16 at frames {0,1,2,5,10,30,60,120,300,600,1500}, CRC "
        "chain over all 1501 frames\",\n");
    o.w("    \"mc_xfade_<from>_to_<to>_n<n>\": \"ModeController: boot (mode 0 fades in from "
        "black), set_mode(from) at frame 60, set_mode(to) at frame 120; every frame 0..180 "
        "recorded with transition state\",\n");
    o.w("    \"mc_<other>\": \"ModeController edge cases: switch mid-transition (frozen "
        "buffer_old), set_mode at t0, set_mode_param, set_rgb/set_hsv, "
        "adj_mode_param/reset_current_mode, NVS param restore, set_length\",\n");
    o.w("    \"strip_boot_*\": \"full LedStrip pipeline booted from preloaded NVS "
        "(begin_routines_required + begin_routines_regular), Brightness dimming + colour order "
        "applied, every render recorded\",\n");
    o.w("    \"strip_set_mode_0_to_<m>_n60\": \"LedStrip::set_mode (double "
        "ModeController::set_mode via sync_color) after boot\",\n");
    o.w("    \"strip_<other>\": \"LedStrip brightness/state ops, colour/param ops, hsv ops, "
        "length/colour-order ops, and loop() polled once per ms (21 ms cadence)\"\n");
    o.w("  },\n");
    o.w("  \"scenarios\": [\n");
    for (std::size_t i = 0; i < sc.size(); ++i)
        o.w("    " + jstr(sc[i].first + ".json") + (i + 1 < sc.size() ? ",\n" : "\n"));
    o.w("  ]\n}\n");
}

int main(int argc, char** argv) {
    auto sc = build_scenarios();
    if (argc == 2 && std::string(argv[1]) == "--list") {
        for (auto& [n, r] : sc)
            std::printf("%s\n", n.c_str());
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--manifest") {
        write_manifest(argv[2], sc);
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--run") {
        for (auto& [n, r] : sc)
            if (n == argv[2]) {
                r(n, argv[3]);
                return 0;
            }
        std::fprintf(stderr, "unknown scenario %s\n", argv[2]);
        return 2;
    }
    std::fprintf(stderr, "usage: oracle --list | --run NAME OUTDIR | --manifest OUTDIR\n");
    return 2;
}
