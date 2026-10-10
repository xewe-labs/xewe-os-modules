// SPDX-FileCopyrightText: 2025-2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/fan/src/Fan/Fan.cpp

#include "Fan.h"

#include <ArduinoJson.h>
#include <driver/gpio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace {
bool by_temp(const FanCurvePoint& a, const FanCurvePoint& b) { return a.temp < b.temp; }

constexpr const char* CURVE_USAGE =
    "Usage: $fan curve list | add <temp_C> <speed_0-100> | remove <temp_C> | set <T:P,T:P,...|none>";
} // namespace


void IRAM_ATTR Fan::tach_isr_handler(void* arg) {
    if (auto* fan = static_cast<FanData*>(arg)) fan->pulse_count = fan->pulse_count + 1;
}

Fan::Fan(xewe::Os& host, FanConfig config)
      : xewe::Module(host,
               /* id                  */ "fan",
               /* name                */ "Fan",
               /* description         */ "Drives 4-wire PWM fans (25 kHz), reads their tachometers, follows a temperature curve",
               /* requires_init_setup */ false,
               /* can_be_disabled     */ true,
               /* has_cli_cmds        */ true)
      , config(std::move(config))
{
    register_command({
        "add",
        "Add a fan without a tachometer: <pwm_pin>",
        "$fan add 9",
        1,
        [this](xewe::span<const std::string> args){ add_cmd(args); }
    });

    register_command({
        "add_w_tach",
        "Add a fan with a tachometer: <pwm_pin> <tach_pin>",
        "$fan add_w_tach 9 10",
        2,
        [this](xewe::span<const std::string> args){ add_w_tach_cmd(args); }
    });

    // `set` stays ours (speed per pin); a non-numeric first argument is a table setting (curve_ms, ...)
    register_command({
        "set",
        "Set the speed of one fan (0-255): <pwm_pin> <speed>, or a setting: <key> <value>",
        "$fan set 9 255",
        2,
        [this](xewe::span<const std::string> args){ set_cmd(args); }
    });

    register_command({
        "set_all",
        "Set the speed of every fan (0-255): <speed>",
        "$fan set_all 255",
        1,
        [this](xewe::span<const std::string> args){ set_all_cmd(args); }
    });

    register_command({
        "remove",
        "Remove a fan by its PWM pin: <pwm_pin>",
        "$fan remove 9",
        1,
        [this](xewe::span<const std::string> args){ remove_cmd(args); }
    });

    register_command({
        "temp",
        "Feed a temperature to the curve (as a sensor would): <temp_C>",
        "$fan temp 30.5",
        1,
        [this](xewe::span<const std::string> args){ temp_cmd(args); }
    });

    // ---- curve: one `curve` command per argument count, dispatched on the first argument ----------
    // (the core picks the registration whose count matches; on a mismatch it shows the first usage)
    register_command({
        "curve",
        "Curve: list",
        "$fan curve list",
        1,
        [this](xewe::span<const std::string> args){ curve_cmd(args); }
    });

    register_command({
        "curve",
        "Curve: remove <temp_C> | set <T:P,T:P,...|none>",
        "$fan curve set 22:0,27:100,32:100",
        2,
        [this](xewe::span<const std::string> args){ curve_cmd(args); }
    });

    register_command({
        "curve",
        "Curve: add <temp_C> <speed_0-100> (adds or replaces the point)",
        "$fan curve add 40.5 50",
        3,
        [this](xewe::span<const std::string> args){ curve_cmd(args); }
    });

    register_command({
        "print_json",
        "Print the fan data as JSON",
        "$fan print_json",
        0,
        [this](xewe::span<const std::string>){ os.serial.print(get_json()); }
    });
}

Fan::~Fan() {
    clear_fans();
}

void Fan::begin_routines_required() {
    load();
    load_curve();
    begin_ms      = millis();
    last_curve_ms = begin_ms;
}

void Fan::loop() {
    if (is_disabled()) return;
    const uint32_t now = millis();
    run_curve(now);

    for (auto* f : fans) {
        if (!f->has_tach) continue;

        const uint32_t dt = now - f->last_calc_time;
        if (dt < 1000) continue;

        noInterrupts();
        const uint32_t pulses = f->pulse_count;
        f->pulse_count = 0;
        interrupts();

        // 2 pulses per revolution: rpm = pulses / 2 * 60000 / dt (64-bit: pulses * 30000 wraps
        // 32 bits above 143 165 pulses, e.g. a floating or ringing tach line after a loop stall)
        const uint64_t rpm64       = (static_cast<uint64_t>(pulses) * 30000u) / dt;
        const uint32_t current_rpm = rpm64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(rpm64);
        f->last_calc_time = now;

        if (current_rpm > config.absolute_max_rpm) continue;   // noise spike

        f->raw_history[f->history_idx] = current_rpm;
        f->history_idx = (f->history_idx + 1) % 3;
        if (f->history_count < 3) f->history_count++;

        uint32_t median_rpm = current_rpm;
        if (f->history_count == 3) {
            const uint32_t a = f->raw_history[0], b = f->raw_history[1], c = f->raw_history[2];
            median_rpm = std::max(std::min(a, b), std::min(std::max(a, b), c));
        }

        f->ema_rpm = (f->ema_rpm == 0 && median_rpm > 0)
                   ? median_rpm
                   : static_cast<uint32_t>((config.ema_alpha * median_rpm) + ((1.0f - config.ema_alpha) * f->ema_rpm));

        const uint32_t r = config.ui_rounding;
        f->displayed_rpm = r ? ((f->ema_rpm + r / 2) / r) * r : f->ema_rpm;
    }
}

void Fan::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    clear_fans();
    default_curve();
    curve_changed(nullptr);
    temperature_seen = false;
    temperature      = NAN;
    // wipes the "fan" namespace: the next boot sets up the default fans (FAN* defines) and curve again
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Fan::status(const bool verbose) const {
    std::string s = Module::status(false);
    if (fans.empty()) {
        s += "\nNo fans configured";
    } else {
        for (const auto* f : fans) {
            s += "\n  PWM pin " + std::to_string(f->pin_pwm) + ", speed " + std::to_string(f->speed);
            if (f->has_tach) {
                s += ", tach pin " + std::to_string(f->pin_tach) + ", " + std::to_string(f->displayed_rpm) + " RPM";
            } else {
                s += ", no tach";
            }
        }
    }
    s += curve_lines();
    if (verbose) os.serial.print(s);
    return s;
}

xewe::Settings Fan::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Fan::curve_ms>("curve_ms", 100, 60000, 1000, "Curve -> fans period, ms"),
        xewe::setting<&Fan::stale_ms>("stale_ms", 1000, 600000, 10000,
                                      "No temperature for this long: offline (fans at the last point), ms"),
    };
    return {table, this};
}

void Fan::schema_extra(xewe::SchemaOut& out) const {
    std::string pins;
    for (const auto* f : fans) {
        if (!pins.empty()) pins += ',';
        pins += std::to_string(f->pin_pwm);
        if (f->has_tach) pins += '/' + std::to_string(f->pin_tach);
    }
    out.row(R"("key":"fans","group":"fans","type":"pins","value":")" + pins +
            R"(","doc":"PWM[/tach] pin per fan","set":"$fan add <pwm> | add_w_tach <pwm> <tach> | remove <pwm>")");
    std::string spec;
    char buf[24];
    for (const auto& p : curve.points) {
        snprintf(buf, sizeof(buf), "%s%.2f:%u", spec.empty() ? "" : ",", p.temp, static_cast<unsigned>(p.speed));
        spec += buf;
    }
    out.row(R"("key":"points","group":"curve","type":"curve","value":")" + (spec.empty() ? std::string("none") : spec) +
            R"(","doc":"T:P,... temperature C -> speed %","set":"$fan curve set <T:P,T:P,...|none>")");
}

std::string Fan::get_json() const {
    JsonDocument doc;
    JsonArray json_fans = doc["fans"].to<JsonArray>();

    for (const auto* f : fans) {
        JsonObject o = json_fans.add<JsonObject>();
        o["pin_pwm"]  = f->pin_pwm;
        o["has_tach"] = f->has_tach;
        // the web UI plots 0-100 %
        o["speed"]    = static_cast<uint8_t>((static_cast<uint16_t>(f->speed) * 100) / 255);
        if (f->has_tach) {
            o["pin_tach"]      = f->pin_tach;
            o["displayed_rpm"] = f->displayed_rpm;
            o["ema_rpm"]       = f->ema_rpm;
        }
    }
    JsonArray json_curve = doc["curve"].to<JsonArray>();
    for (const auto& p : curve.points) {
        JsonObject o = json_curve.add<JsonObject>();
        o["temp"]  = p.temp;
        o["speed"] = p.speed;
    }
    doc["curve_target"] = curve_target_pct;

    std::string out;
    serializeJson(doc, out);
    return out;
}

// ---- API ----------------------------------------------------------------------------------------

uint32_t Fan::get_rpm(uint8_t pwm_pin) const {
    const FanData* f = get_fan(pwm_pin);
    return (f && f->has_tach) ? f->displayed_rpm : 0;
}

bool Fan::add(uint8_t pwm_pin, uint8_t tach_pin) {
    if (is_disabled() || get_fan(pwm_pin)) return false;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pwm_pin)) return false;
    if (tach_pin != NO_PIN && (tach_pin == pwm_pin || !GPIO_IS_VALID_GPIO(tach_pin))) return false;
    // a pin already used by another fan (as PWM or tach) would re-route its LEDC output or replace
    // its tach interrupt
    for (const auto* f : fans) {
        if (f->has_tach && f->pin_tach == pwm_pin) return false;
        if (tach_pin != NO_PIN && (f->pin_pwm == tach_pin || (f->has_tach && f->pin_tach == tach_pin))) return false;
    }
    if (!create_fan(pwm_pin, tach_pin, 0)) return false;
    save();
    return true;
}

bool Fan::remove(uint8_t pwm_pin) {
    if (is_disabled()) return false;
    auto it = std::find_if(fans.begin(), fans.end(), [pwm_pin](FanData* f) { return f->pin_pwm == pwm_pin; });
    if (it == fans.end()) return false;

    free_fan(*it);
    fans.erase(it);
    save();
    return true;
}

bool Fan::set(uint8_t pwm_pin, uint8_t speed, bool persist) {
    if (is_disabled()) return false;
    FanData* f = get_fan(pwm_pin);
    if (!f) return false;
    f->speed = speed;
    ledcWrite(f->pin_pwm, speed);
    if (persist) save();
    return true;
}

bool Fan::set_all(uint8_t speed, bool persist) {
    if (is_disabled() || fans.empty()) return false;
    bool changed = false;
    for (auto* f : fans) {
        if (f->speed != speed) changed = true;
        f->speed = speed;
        ledcWrite(f->pin_pwm, speed);
    }
    if (persist && changed) save();
    return true;
}

// ---- internals ----------------------------------------------------------------------------------

Fan::FanData* Fan::get_fan(uint8_t pwm_pin) const {
    for (auto* f : fans) if (f->pin_pwm == pwm_pin) return f;
    return nullptr;
}

void Fan::free_fan(FanData* f) {
    if (f->has_tach) {
        detachInterrupt(digitalPinToInterrupt(f->pin_tach));
        xewe::pins::release(f->pin_tach, id.c_str());
    }
    ledcWrite(f->pin_pwm, 0);
    ledcDetach(f->pin_pwm);
    xewe::pins::release(f->pin_pwm, id.c_str());
    delete f;
}

void Fan::clear_fans() {
    for (auto* f : fans) free_fan(f);
    fans.clear();
}

Fan::FanData* Fan::create_fan(uint8_t pwm, uint8_t tach, uint8_t speed) {
    // core pin registry: a pin another module holds is refused (and reported); ours are released in free_fan
    if (!xewe::pins::claim(pwm, id.c_str())) return nullptr;
    if (tach != NO_PIN && !xewe::pins::claim(tach, id.c_str())) {
        xewe::pins::release(pwm, id.c_str());
        return nullptr;
    }
    if (!ledcAttach(pwm, PWM_FREQ, PWM_RES)) {
        xewe::pins::release(pwm, id.c_str());
        if (tach != NO_PIN) xewe::pins::release(tach, id.c_str());
        return nullptr;
    }

    auto* f           = new FanData();
    f->pin_pwm        = pwm;
    f->pin_tach       = tach;
    f->has_tach       = (tach != NO_PIN);
    f->speed          = speed;
    f->last_calc_time = millis();

    if (f->has_tach) {
        pinMode(tach, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(tach), tach_isr_handler, f, FALLING);
    }
    ledcWrite(pwm, speed);
    fans.push_back(f);
    return f;
}

void Fan::load() {
    clear_fans();

    FanStore store;
    const bool stored  = os.nvs.read_flex(id, "data", store);
    // has(): a missing `schema` field is foreign, not the struct default
    const bool foreign = stored && (!store.has("schema") || store.schema != FanStore::SCHEMA);
    if (stored && !foreign) {
        for (const auto& e : store.fans) create_fan(e.pwm, e.tach, e.speed);
        return;
    }

    // first boot: the default fans (FAN* defines), then store them. Another firmware's layout (schema
    // mismatch): the defaults from RAM only; the stored blob is never reinterpreted and never
    // overwritten at boot (the first fan change replaces it).
    for (const auto& d : config.defaults) {
        if (d.pwm == NO_PIN || get_fan(d.pwm) || !GPIO_IS_VALID_OUTPUT_GPIO(d.pwm)) continue;
        if (d.tach != NO_PIN && (d.tach == d.pwm || !GPIO_IS_VALID_GPIO(d.tach))) continue;
        create_fan(d.pwm, d.tach, 0);
    }
    if (foreign) {
        os.serial.print("Fan: stored fans have schema " + std::to_string(store.schema) +
                        ", this firmware reads schema " + std::to_string(FanStore::SCHEMA) +
                        "; using the default fans (the stored blob is left untouched until the next change)");
        return;
    }
    save();
}

void Fan::save() {
    FanStore store;
    for (const auto* f : fans) {
        FanEntry e;
        e.pwm   = f->pin_pwm;
        e.tach  = f->has_tach ? f->pin_tach : NO_PIN;
        e.speed = f->speed;
        store.fans.push_back(e);
    }
    os.nvs.write_flex(id, "data", store);
}

// ---- curve --------------------------------------------------------------------------------------

void Fan::set_temperature(float celsius, const void* origin) {
    temperature        = celsius;
    temperature_seen   = true;
    temperature_ms     = millis();
    temperature_origin = origin;
}

bool Fan::curve_active() const {
    return is_enabled() && temperature_seen && !curve.points.empty();
}

bool Fan::curve_failsafe() const {
    return is_enabled() && curve_math::never_received(temperature_seen, !curve.points.empty(), millis() - begin_ms, stale_ms);
}

void Fan::run_curve(uint32_t now, bool force) {
    const bool never = curve_failsafe();
    if ((!curve_active() && !never) || (!force && now - last_curve_ms < curve_ms)) return;
    last_curve_ms = now;
    // a source that stopped (or never started) reporting counts as offline: NaN -> last point (fail-safe hot)
    const float   t      = (never || now - temperature_ms > stale_ms) ? NAN : temperature;
    const uint8_t before = curve_target_pct;
    curve_target_pct = curve_math::target_speed(curve.points, t);
    set_all(curve_math::speed_to_pwm(curve_target_pct), false);    // RAM only: no NVS write per second
    if (curve_target_pct != before) {
        const uint8_t pct = curve_target_pct;
        listeners.notify([&](FanListener& l) { l.on_curve_target(pct, t, temperature_origin); });
    }
}

void Fan::curve_changed(const void* origin) {
    listeners.notify([&](FanListener& l) { l.on_curve_changed(origin); });
}

bool Fan::curve_add(float temp, uint8_t speed_pct, const void* origin) {
    if (is_disabled() || !curve_math::temp_ok(temp) || speed_pct > 100) return false;
    auto& pts = curve.points;
    auto same = [temp](const FanCurvePoint& p) { return std::fabs(p.temp - temp) < curve_math::SAME_TEMP; };
    const bool replaces = std::any_of(pts.begin(), pts.end(), same);
    if (!replaces && pts.size() >= MAX_CURVE_POINTS) return false;
    pts.erase(std::remove_if(pts.begin(), pts.end(), same), pts.end());
    FanCurvePoint p;
    p.temp  = temp;
    p.speed = speed_pct;
    pts.push_back(p);
    std::sort(pts.begin(), pts.end(), by_temp);
    save_curve();
    curve_changed(origin);
    return true;
}

bool Fan::curve_remove(float temp, const void* origin) {
    if (is_disabled()) return false;
    auto& pts = curve.points;
    auto it = std::remove_if(pts.begin(), pts.end(), [temp](const FanCurvePoint& p) {
        return std::fabs(p.temp - temp) < curve_math::SAME_TEMP;
    });
    if (it == pts.end()) return false;
    pts.erase(it, pts.end());
    save_curve();
    curve_changed(origin);
    return true;
}

bool Fan::set_curve(std::vector<FanCurvePoint> points, bool persist, const void* origin) {
    if (is_disabled()) return false;
    for (auto& p : points) p.speed = curve_math::clamp_speed(p.speed);     // > 100 % counts as 100 %
    std::sort(points.begin(), points.end(), by_temp);
    // rejected: too many points, a temperature out of range, two points at the same temperature
    if (curve_math::validate_points(points) != curve_math::CurveError::ok) return false;
    curve.points = std::move(points);
    if (persist) save_curve();
    curve_changed(origin);
    return true;
}

void Fan::save_curve() {
    curve.schema = FanCurveStore::SCHEMA;
    os.nvs.write_flex(id, "curve", curve);      // one NVS write per change
}

void Fan::default_curve() {
    curve = FanCurveStore{};
    const float   temps[]  = {22.0f, 27.0f, 32.0f};
    const uint8_t speeds[] = {0, 100, 100};
    for (size_t i = 0; i < 3; ++i) {
        FanCurvePoint p;
        p.temp  = temps[i];
        p.speed = speeds[i];
        curve.points.push_back(p);
    }
}

void Fan::load_curve() {
    FanCurveStore stored;
    if (!os.nvs.read_flex(id, "curve", stored)) {
        default_curve();                                    // first boot: store the default curve
        save_curve();
    } else if (!stored.has("schema") || !curve_math::schema_ok(stored.schema)) {
        // has(): a blob without a readable `schema` field is foreign too, not "schema 1 by default"
        // another firmware's layout: never reinterpret it, never overwrite it at boot
        os.serial.print("Fan: curve: " + curve_math::schema_message(stored.schema));
        default_curve();
    } else {
        std::sort(stored.points.begin(), stored.points.end(), by_temp);
        const auto err = curve_math::validate_points(stored.points);
        if (err == curve_math::CurveError::ok) {
            curve = std::move(stored);
        } else {
            os.serial.print(std::string("Fan: stored curve rejected (") + curve_math::error_text(err) +
                            "); using and storing the default curve");
            default_curve();
            save_curve();
        }
    }
}

std::string Fan::curve_lines() const {
    std::string s = "\n  Curve:";
    if (curve.points.empty()) {
        s += " no points";
    } else {
        char buf[64];
        for (const auto& p : curve.points) {
            snprintf(buf, sizeof(buf), "\n    %.2f C -> %u %% (PWM %u)", p.temp, p.speed,
                     static_cast<unsigned>(curve_math::speed_to_pwm(p.speed)));
            s += buf;
        }
    }
    if (curve_failsafe()) {
        s += "\n  Temperature: never received (fail-safe " +
             std::to_string(curve_math::target_speed(curve.points, NAN)) + " %)";
    } else if (!temperature_seen) {
        s += "\n  Temperature: none yet (curve idle; fed by set_temperature or $fan temp)";
    } else {
        char buf[96];
        if (std::isnan(temperature)) snprintf(buf, sizeof(buf), "\n  Temperature: offline (%s)", temperature_origin ? "source" : "CLI");
        else                         snprintf(buf, sizeof(buf), "\n  Temperature: %.2f C (%s)", temperature, temperature_origin ? "source" : "CLI");
        s += buf;
        s += "\n  Curve target: " + std::to_string(curve_target_pct) + " %";
    }
    return s;
}

std::string Fan::get_curve_json() const {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& p : curve.points) {
        JsonObject o = arr.add<JsonObject>();
        o["temp"]  = p.temp;
        o["speed"] = p.speed;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

// ---- commands -----------------------------------------------------------------------------------

void Fan::add_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    uint8_t pwm;
    if (xewe::str::parse_int(args[0], pwm) && add(pwm)) os.serial.print("Fan added.");
    else                                                 os.serial.print("Failed to add fan.");
}

void Fan::add_w_tach_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    uint8_t pwm, tach;
    if (xewe::str::parse_int(args[0], pwm) && xewe::str::parse_int(args[1], tach) && tach != NO_PIN && add(pwm, tach))
        os.serial.print("Fan with tach added.");
    else
        os.serial.print("Failed to add fan.");
}

void Fan::set_cmd(xewe::span<const std::string> args) {
    uint8_t pwm, speed;
    if (!args[0].empty() && !std::isdigit(static_cast<unsigned char>(args[0][0]))) {
        apply_setting(args[0], args[1], true);      // `$fan set curve_ms 500`: the core's table set
        return;
    }
    if (is_disabled(true)) return;
    if (xewe::str::parse_int(args[0], pwm) && xewe::str::parse_int(args[1], speed) && set(pwm, speed))
        os.serial.print("Speed updated.");
    else
        os.serial.print("Failed to set fan speed.");
}

void Fan::set_all_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    uint8_t speed;
    if (xewe::str::parse_int(args[0], speed) && set_all(speed)) os.serial.print("All fans updated.");
    else                                                         os.serial.print("Failed to set fans.");
}

void Fan::remove_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    uint8_t pwm;
    if (xewe::str::parse_int(args[0], pwm) && remove(pwm)) os.serial.print("Fan removed.");
    else                                                    os.serial.print("Failed to remove fan.");
}

void Fan::temp_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    float t;
    if (!xewe::str::parse_float(args[0], t) || !std::isfinite(t)) {
        os.serial.print("Invalid temperature.");
        return;
    }
    set_temperature(t, nullptr);
    run_curve(millis(), true);                                  // apply now, not on the next tick
    if (curve.points.empty()) {
        os.serial.printf("Temperature set to %.2f C (the curve has no points).", t);
        return;
    }
    os.serial.printf("Temperature set to %.2f C, curve target %u %%.", t, static_cast<unsigned>(curve_target_pct));
}

void Fan::curve_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;
    const std::string& sub = args[0];
    if (args.size() == 1 && sub == "list") {
        os.serial.print(curve_lines().substr(1));
        return;
    }
    if (args.size() == 2 && sub == "remove") {
        float temp;
        if (xewe::str::parse_float(args[1], temp) && curve_remove(temp, nullptr)) os.serial.print("Curve point removed.");
        else                                                             os.serial.print("Failed to remove curve point.");
        return;
    }
    if (args.size() == 2 && sub == "set") {
        std::vector<FanCurvePoint> pts;
        if (!curve_math::parse_curve_spec(args[1], pts)) {
            os.serial.print("Failed to set curve: expected T:P,T:P,... (speed 0-100) or none.");
            return;
        }
        std::sort(pts.begin(), pts.end(), by_temp);
        const auto err = curve_math::validate_points(pts);
        if (err != curve_math::CurveError::ok || !set_curve(std::move(pts))) {
            os.serial.print(std::string("Failed to set curve: ") + curve_math::error_text(err) + ".");
            return;
        }
        os.serial.print("Curve set (" + std::to_string(curve.points.size()) + " points).");
        return;
    }
    if (args.size() == 3 && sub == "add") {
        float temp;
        uint8_t speed;
        if (xewe::str::parse_float(args[1], temp) && xewe::str::parse_int(args[2], speed) && speed <= 100 &&
            curve_add(temp, speed))
            os.serial.print("Curve point added.");
        else
            os.serial.print("Failed to add curve point.");
        return;
    }
    os.serial.print(CURVE_USAGE);
}
