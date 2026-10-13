// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/led/src/Led/Led.cpp

#include "Led.h"

#include <algorithm>

namespace {

// the render lock taken by the task itself (LedEngine sections take it for the setters)
class RenderLock {
public:
    explicit RenderLock(LedEngineIo& io) : io(io) {
        io.lock();
    }
    ~RenderLock() {
        io.unlock();
    }
    RenderLock(const RenderLock&) = delete;
    RenderLock& operator=(const RenderLock&) = delete;

private:
    LedEngineIo& io;
};

} // namespace

// =============================================================================
// Constructor
// =============================================================================
void Led::CommandSlot::attach() {
    const LedCommand& c = LED_COMMANDS[index];
    led->register_command({c.name, c.description, c.sample_usage, c.arg_count,
        [this](xewe::span<const std::string> args) {
            if (!led->engine.begun())
                return;
            led_run_command(led->engine, index, args);
        }});
}

Led::Led(xewe::Os& host)
    : xewe::Module(host,
          /* id                  */ "led",
          /* name                */ "Led",
          /* description         */ "Allows to control addressable LED strip",
          /* requires_init_setup */ true,
          /* can_be_disabled     */ false,
          /* has_cli_cmds        */ true),
      io(*this), store(*this), engine(io), listeners(engine.listeners) {
    engine.set_self_origin(this);
    for (std::size_t i = 0; i < LED_COMMAND_COUNT; ++i) {
        command_slots[i] = {this, i};
        command_slots[i].attach();
    }
    // `$led set` (registered here, so the core does not add its own): a voltage other than 5, 12
    // or 24 is refused before the core stores and echoes it; everything else is the core's `set`
    register_command({"set", "Set a setting: validated, applied, saved", "$led set <key> <value>",
        2, [this](xewe::span<const std::string> args) {
            const std::string refusal = led_voltage_refusal(args[0], args[1], active_voltage);
            if (!refusal.empty()) {
                os.serial.print(refusal);
                return;
            }
            apply_setting(args[0], args[1], true);
        }});
}

// =============================================================================
// Settings
// =============================================================================
xewe::Settings Led::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Led::use_clk>("use_clk", false, "LED has a clock line (status only)"),
        xewe::setting<&Led::chip>("chip", 0, LED_CHIPSET_COUNT - 1, 0,
            "LED chipset id 0..45 (README), applied at boot", xewe::SettingDef::RESTART),
        xewe::setting<&Led::voltage>(
            "voltage", 5, 24, 5, "Strip voltage, 5, 12 or 24 V (power estimate)"),
        xewe::setting<&Led::lines>("lines", 0, XEWE_MODULE_LED_NUM_LEDS_MAX, 0,
            "Parallel strips on the data line (0 = one strip)"),
        xewe::setting<&Led::num_led>("num_led", 0, XEWE_MODULE_LED_NUM_LEDS_MAX,
            XEWE_MODULE_LED_NUM_LEDS_MAX, "Number of LEDs (same as $led set_length)"),
        xewe::setting<&Led::colorder>(
            "colorder", 0, 5, 0, "Colour order 0..5 = RGB RBG GRB GBR BRG BGR"),
    };
    return {table, this};
}

void Led::on_setting_changed(const xewe::SettingDef& def) {
    const std::string_view key = def.key;
    if (key == "voltage") {
        // `$led set` refuses a bad voltage before it is stored; another apply_setting caller gets
        // the last good value put back
        const std::string refusal =
            led_voltage_refusal(key, std::to_string(voltage), active_voltage);
        if (!refusal.empty()) {
            os.serial.print(refusal);
            apply_setting("voltage", std::to_string(active_voltage));
            return;
        }
        active_voltage = voltage;
    } else if (key == "colorder") {
        if (engine.begun())
            engine.set_color_order_index(colorder);
    } else if (key == "num_led") {
        // the wizard sets the length itself; otherwise this is `$led set_length`
        if (!wizard_active && engine.begun())
            engine.sync_length(num_led, nullptr);
    }
}

void Led::schema_extra(xewe::SchemaOut& out) const {
    for (std::size_t i = 0; i < lines; ++i) {
        const std::string key = "l_" + std::to_string(i) + "_cnt";
        out.row("\"key\":\"" + key + "\",\"group\":\"lines\",\"type\":\"u16\",\"value\":" +
            std::to_string(os.nvs.read<uint16_t>(id, key, 0)) +
            ",\"set\":\"first-boot setup ($led reset)\"");
    }
    if (!engine.begun())
        return;
    const auto rgb = engine.get_rgb();
    out.row("\"key\":\"rgb\",\"group\":\"live\",\"type\":\"rgb\",\"value\":\"" +
        xewe::str::to_hex_color(rgb[0], rgb[1], rgb[2]) +
        "\",\"set\":\"$led set_rgb <r> <g> <b>\"");
    out.row("\"key\":\"brightness\",\"group\":\"live\",\"type\":\"u8\",\"value\":" +
        std::to_string(engine.get_brightness()) + ",\"set\":\"$led set_brightness <0-255>\"");
    out.row(std::string("\"key\":\"state\",\"group\":\"live\",\"type\":\"bool\",\"value\":") +
        (engine.get_state() ? "true" : "false") + ",\"set\":\"$led set_state <0|1>\"");
    out.row("\"key\":\"mode_id\",\"group\":\"live\",\"type\":\"u8\",\"value\":" +
        std::to_string(engine.get_current_mode_id()) + ",\"set\":\"$led set_mode <0-6>\"");
    const std::string group = "mode:" + std::string(engine.get_current_mode_name());
    for (const auto& p : engine.get_current_mode_params()) {
        out.row("\"key\":\"" + p.key + "\",\"group\":\"" + group + "\",\"type\":\"u16\",\"min\":" +
            std::to_string(p.min_value) + ",\"max\":" + std::to_string(p.max_value) +
            ",\"value\":" + std::to_string(engine.get_current_mode_param(p.key)) +
            ",\"set\":\"$led set_mode_param " + p.key + " <v>\"");
    }
}

// =============================================================================
// Begin
// =============================================================================
void Led::begin_routines_required() {
    render_mutex = xSemaphoreCreateRecursiveMutex();
    render_task_ended = xSemaphoreCreateBinary();
    active_voltage = voltage;
    load_mode_params();
    engine.begin(num_led, store);
}

void Led::begin_routines_init() {
    wizard_active = true;

    os.serial.print(
        "Detailed guide is available here:\nhttps://github.com/maxdokukin/xewe/led-os/TODO");

    // A "no" to a pin question prints how to change the pins and the setup continues: wrong pins
    // show no colours in the calibration, whose "Other" answer restarts into this wizard.
    if (!os.serial.get_yn("Is LED data line connected to pin GPIO_" +
            std::to_string(XEWE_MODULE_LED_PIN_DATA) + "?")) {
        os.serial.print_header(
            "Pins can not be changed after upload. \nOptions:\\sep1. Upload the version with the "
            "correct pin\\sep2. If there is no compiled version with pin that you need, set pins "
            "in Config.h and compile yourself");
    }

    if (os.serial.get_yn("Are you using LED with CLK line?")) {
        apply_setting("use_clk", "true");
        if (!os.serial.get_yn("Is LED CLK line connected to pin GPIO_" +
                std::to_string(XEWE_MODULE_LED_PIN_CLOCK) + "?")) {
            os.serial.print_header(
                "Pins can not be changed after upload.\nOptions:\\sep1. Upload the version with "
                "the correct pin\\sep2. If there is no compiled version with pin that you need, "
                "set "
                "pins in Config.h and compile yourself");
        }
    } else {
        apply_setting("use_clk", "false");
    }

    std::vector<std::string> chipset_names;
    for (const auto& entry : LED_CHIPSET_TABLE) {
        chipset_names.push_back(entry.name);
    }

    const uint8_t selected_chip_id =
        os.serial.get_menu_choice("What is your LED Chip?", chipset_names) - 1;
    apply_setting("chip", std::to_string(selected_chip_id));
    start_strip();

    switch (os.serial.get_menu_choice("What is LED Voltage?", {"5V", "12V", "24V"})) {
    case 1:
        apply_setting("voltage", "5");
        break;
    case 2:
        apply_setting("voltage", "12");
        break;
    case 3:
        apply_setting("voltage", "24");
        break;
    }

    if (os.serial.get_yn("Do you have parallel LED strips attached to data line?")) {
        const uint16_t parallel_led_strips_count = os.serial.get_int(
            "How many LED strips do you have in parallel?", 1, XEWE_MODULE_LED_NUM_LEDS_MAX);

        apply_setting("lines", std::to_string(parallel_led_strips_count));

        // the longest line becomes the stored length (the strip drives that many pixels on the
        // shared data line)
        uint16_t longest_line = 0;
        for (uint16_t i = 0; i < parallel_led_strips_count; ++i) {
            const uint16_t leds_per_line = os.serial.get_int(
                "How many LEDs are connected on parallel line #" + std::to_string(i + 1) + "?", 1,
                XEWE_MODULE_LED_NUM_LEDS_MAX);

            longest_line = std::max(longest_line, leds_per_line);

            os.nvs.write<uint16_t>(id, "l_" + std::to_string(i) + "_cnt", leds_per_line);
        }
        apply_setting("num_led", std::to_string(longest_line));
    } else {
        apply_setting("lines", "0");
        const uint16_t count = os.serial.get_int(
            "How many LEDs do you have connected?", 0, XEWE_MODULE_LED_NUM_LEDS_MAX);
        apply_setting("num_led", std::to_string(count));
    }

    engine.set_length(num_led); // the ModeController renders this length, the rest stays black
    start_render_task(); // the colour order calibration needs live frames

    calibrate_color_order();

    os.serial.print("LED setup success!");
    wizard_active = false;
}

void Led::begin_routines_regular() {
    start_strip();

    // a missing key reads as 0 / false; the rgb blob may be missing
    const std::vector<uint8_t> rgb_blob = os.nvs.read_blob(id, "rgb");
    engine.boot(colorder, rgb_blob, os.nvs.read<uint8_t>(id, "brightness"),
        os.nvs.read<uint8_t>(id, "mode_id"), os.nvs.read<bool>(id, "state"));

    start_render_task();
}

void Led::begin_routines_common() {
    os.serial.print("Shining the light", "");
    wait_with_dots(static_cast<uint32_t>(engine.get_mode_transition_delay() * 1.2f));
}

void Led::calibrate_color_order() {
    std::string value;
    uint8_t color_order_index = 0;

    // the test colours go out in plain RGB order, so the answers name the strip's own order also
    // when an order is already applied (calibrating a second time)
    engine.set_color_order_index(0);

    while (true) {
        value = "BBB";

        engine.set_rgb({0, 255, 0});
        engine.set_brightness(50);
        engine.set_mode(0);
        engine.turn_on();
        os.serial.print_header("Color Order Calibration");
        wait_with_dots(static_cast<uint32_t>(engine.get_mode_transition_delay() * 1.2f));

        auto color = os.serial.get_menu_choice(
            "What color are LEDs now?", {"Red", "Green", "Blue", "Other"});

        if (color == 4) {
            os.serial.print_header("Double check pins and LED chip type.\nRGBW is not supported.");
            os.system.restart();
            return;
        }

        value[color - 1] = 'G';
        os.serial.print("Changing color", "");
        engine.set_rgb({255, 0, 0});
        wait_with_dots(static_cast<uint32_t>(engine.get_mode_transition_delay() * 1.2f));

        color = os.serial.get_menu_choice("What color are LEDs now?", {"Red", "Green", "Blue"});

        value[color - 1] = 'R';

        const auto it = std::find(LED_COLOR_ORDERS.begin(), LED_COLOR_ORDERS.end(), value);
        if (it != LED_COLOR_ORDERS.end()) {
            color_order_index = static_cast<uint8_t>(it - LED_COLOR_ORDERS.begin());
            // applied now, so the fade-out and the confirmation below already use the new order
            engine.set_color_order_index(color_order_index);
            break;
        }

        os.serial.print("Incorrect entries. Let's try again");
    }

    engine.turn_off();
    os.serial.print("Setting color order", "");
    wait_with_dots(static_cast<uint32_t>(engine.get_mode_transition_delay() * 1.2f));
    engine.turn_on();
    engine.set_rgb({0, 255, 0});

    apply_setting("colorder", std::to_string(color_order_index)); // stores the row
    os.serial.print("Color order set to " + value);
}

void Led::wait_with_dots(uint32_t duration_ms) {
    run_with_dots([] { vTaskDelay(pdMS_TO_TICKS(10)); }, duration_ms);
}

// =============================================================================
// Reset and status
// =============================================================================
void Led::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    if (engine.begun()) {
        engine.sync_all({0, 255, 0}, 50, true, 0, engine.get_length(), this);
        if (verbose)
            status(true);
    }
    // the mode parameters live in the led namespace, which Module::reset wipes; the RAM copy goes
    // here
    mode_params.params.clear();
    // no output after a reset: the task stops before the pins are released
    stop_render_task();
    release_pins();
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Led::status(const bool verbose) const {
    std::string result = Module::status(false);

    if (engine.begun()) {
        LedStatusInput in;
        in.use_clk = use_clk;
        in.chip = chip;
        in.voltage = voltage;
        in.lines = lines;
        in.line_lengths = read_line_lengths();
        in.fps = fps_calculated.load();
        result += "\n" + engine.status_text(in);
    }

    if (verbose)
        os.serial.print(result);
    return result;
}

std::vector<uint16_t> Led::read_line_lengths() const {
    std::vector<uint16_t> out;
    for (uint16_t i = 0; i < lines; ++i)
        out.push_back(os.nvs.read<uint16_t>(id, "l_" + std::to_string(i) + "_cnt", 0));
    return out;
}

// =============================================================================
// API for other modules
// =============================================================================
void Led::sync_color(const std::array<uint8_t, 3> rgb, const void* origin) {
    if (engine.begun())
        engine.sync_color(rgb, origin);
}

void Led::sync_brightness(const uint8_t value, const void* origin) {
    if (engine.begun())
        engine.sync_brightness(value, origin);
}

void Led::sync_state(const bool state, const void* origin) {
    if (engine.begun())
        engine.sync_state(state, origin);
}

void Led::sync_mode(const uint8_t mode_id, const void* origin) {
    if (engine.begun())
        engine.sync_mode(mode_id, origin);
}

void Led::sync_length(const uint16_t length, const void* origin) {
    if (engine.begun())
        engine.sync_length(length, origin);
}

void Led::sync_all(const std::array<uint8_t, 3> rgb, const uint8_t value, const bool state,
    const uint8_t mode_id, const uint16_t length, const void* origin) {
    if (engine.begun())
        engine.sync_all(rgb, value, state, mode_id, length, origin);
}

void Led::set_mode_param(std::string_view key, const uint16_t value) {
    if (engine.begun())
        engine.set_mode_param(key, value);
}

void Led::reset_current_mode() {
    if (engine.begun())
        engine.reset_current_mode();
}

std::array<uint8_t, 3> Led::get_rgb() const {
    return engine.begun() ? engine.get_rgb() : std::array<uint8_t, 3>{0, 0, 0};
}

std::array<uint8_t, 3> Led::get_hsv() const {
    return engine.begun() ? engine.get_hsv() : std::array<uint8_t, 3>{0, 0, 0};
}

uint8_t Led::get_brightness() const {
    return engine.begun() ? engine.get_brightness() : 0;
}

bool Led::get_state() const {
    return engine.begun() && engine.get_state();
}

uint8_t Led::get_current_mode_id() const {
    return engine.begun() ? engine.get_current_mode_id() : 0;
}

std::string_view Led::get_current_mode_name() const {
    return engine.begun() ? engine.get_current_mode_name() : std::string_view{};
}

uint16_t Led::get_current_mode_param(std::string_view key) const {
    return engine.begun() ? engine.get_current_mode_param(key) : 0;
}

std::string Led::get_all_modes_json() const {
    return engine.begun() ? engine.get_all_modes_json() : std::string("[]");
}

uint16_t Led::get_length() const {
    return engine.begun() ? engine.get_length() : num_led;
}

// =============================================================================
// Persistence (engine and mode parameters)
// =============================================================================
void Led::Io::save_u8(const char* key, uint8_t value) {
    led.os.nvs.write<uint8_t>(led.id, key, value);
    if (std::string_view(key) == "colorder")
        led.colorder = value; // keep the settings row in step with $led set_color_order
}

void Led::Io::save_u16(const char* key, uint16_t value) {
    led.os.nvs.write<uint16_t>(led.id, key, value);
    if (std::string_view(key) == "num_led")
        led.num_led = value; // keep the settings row in step with $led set_length
}

void Led::Io::save_bool(const char* key, bool value) {
    led.os.nvs.write<bool>(led.id, key, value);
}

void Led::Io::save_rgb(const std::array<uint8_t, 3>& rgb) {
    led.os.nvs.write_blob(led.id, "rgb", std::vector<uint8_t>(rgb.begin(), rgb.end()));
}

void Led::Io::print(std::string_view text) {
    led.os.serial.print(text);
}

void Led::Io::calibrate_color_order() {
    led.calibrate_color_order();
}

void Led::Io::lock() {
    if (led.render_mutex != nullptr)
        xSemaphoreTakeRecursive(led.render_mutex, portMAX_DELAY);
}

void Led::Io::unlock() {
    if (led.render_mutex != nullptr)
        xSemaphoreGiveRecursive(led.render_mutex);
}

uint16_t Led::ParamStore::read(uint8_t mode_id, std::string_view key, uint16_t default_value) {
    for (const auto& p : led.mode_params.params)
        if (p.mode == mode_id && p.key == key)
            return p.value;
    return default_value;
}

void Led::ParamStore::write(uint8_t mode_id, std::string_view key, uint16_t value) {
    for (auto& p : led.mode_params.params) {
        if (p.mode == mode_id && p.key == key) {
            dirty = dirty || p.value != value;
            p.value = value;
            return;
        }
    }
    LedModeParam p;
    p.mode = mode_id;
    p.key = std::string(key);
    p.value = value;
    led.mode_params.params.push_back(std::move(p));
    dirty = true;
}

void Led::ParamStore::commit() {
    if (!dirty)
        return;
    led.os.nvs.write_flex(led.id, "mode_params", led.mode_params);
    dirty = false;
}

void Led::load_mode_params() {
    LedModeParams stored;
    if (os.nvs.read_flex(id, "mode_params", stored) && stored.has("schema") &&
        stored.schema == LedModeParams::SCHEMA) {
        mode_params = std::move(stored);
    } else {
        mode_params = LedModeParams{};
    }
}

// =============================================================================
// Strip output
// =============================================================================
bool Led::start_strip() {
    const LEDChipsetEntry& entry = LED_CHIPSET_TABLE[chip < LED_CHIPSET_COUNT ? chip : 0];

    // the clock pin is claimed only for the clocked chipsets
    data_pin_claimed = xewe::pins::claim(XEWE_MODULE_LED_PIN_DATA, id.c_str());
    clock_pin_claimed = data_pin_claimed && entry.clocked &&
        xewe::pins::claim(XEWE_MODULE_LED_PIN_CLOCK, id.c_str());
    if (!data_pin_claimed || (entry.clocked && !clock_pin_claimed)) {
        release_pins();
        os.serial.print("! Led: strip pin claimed by another module (see $pins claims); no output");
        return false;
    }

    if (!set_leds_chipset(entry.value)) {
        os.serial.print("Failed to initialize selected led chip");
        os.system.restart();
        return false;
    }
    FastLED.setBrightness(255);
    return true;
}

void Led::release_pins() {
    if (data_pin_claimed)
        xewe::pins::release(XEWE_MODULE_LED_PIN_DATA, id.c_str());
    if (clock_pin_claimed)
        xewe::pins::release(XEWE_MODULE_LED_PIN_CLOCK, id.c_str());
    data_pin_claimed = false;
    clock_pin_claimed = false;
}

// FastLED takes the chipset and the pins as template arguments: every chipset is instantiated
// with the compile-time pins and the stored one is registered once per boot, always for the full
// buffer, RGB order on the wire (the colour order is applied in software) and TypicalLEDStrip
// correction. Changing the chip, the pins or the buffer size needs a restart.
bool Led::set_leds_chipset(const LEDChipset chipset) {
    constexpr int D = XEWE_MODULE_LED_PIN_DATA;
    constexpr int C = XEWE_MODULE_LED_PIN_CLOCK;
    constexpr int N = XEWE_MODULE_LED_NUM_LEDS_MAX;
    CRGB* leds = engine.pixels();
    switch (chipset) {
    case LEDChipset::APA102:
        FastLED.addLeds<APA102, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::APA102HD:
        FastLED.addLeds<APA102HD, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::APA104:
        FastLED.addLeds<APA104, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::APA106:
        FastLED.addLeds<APA106, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::DOTSTAR:
        FastLED.addLeds<DOTSTAR, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::DOTSTARHD:
        FastLED.addLeds<DOTSTARHD, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::GE8822:
        FastLED.addLeds<GE8822, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::GS1903:
        FastLED.addLeds<GS1903, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::GW6205:
        FastLED.addLeds<GW6205, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::GW6205_400KHZ:
        FastLED.addLeds<GW6205_400, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::HD107:
        FastLED.addLeds<HD107, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::HD107HD:
        FastLED.addLeds<HD107HD, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::LPD1886:
        FastLED.addLeds<LPD1886, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::LPD1886_8BIT:
        FastLED.addLeds<LPD1886_8BIT, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::LPD6803:
        FastLED.addLeds<LPD6803, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::LPD8806:
        FastLED.addLeds<LPD8806, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    // NEOPIXEL has no order argument: FastLED fixes GRB on the wire, on top of the software order
    case LEDChipset::NEOPIXEL:
        FastLED.addLeds<NEOPIXEL, D>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::P9813:
        FastLED.addLeds<P9813, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::PL9823:
        FastLED.addLeds<PL9823, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SK6812:
        FastLED.addLeds<SK6812, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SK6822:
        FastLED.addLeds<SK6822, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SK9822:
        FastLED.addLeds<SK9822, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SK9822HD:
        FastLED.addLeds<SK9822HD, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SM16703:
        FastLED.addLeds<SM16703, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SM16716:
        FastLED.addLeds<SM16716, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::SM16824E:
        FastLED.addLeds<SM16824E, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::TM1803:
        FastLED.addLeds<TM1803, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::TM1804:
        FastLED.addLeds<TM1804, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::TM1809:
        FastLED.addLeds<TM1809, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::TM1812:
        FastLED.addLeds<TM1812, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::TM1829:
        FastLED.addLeds<TM1829, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::UCS1903:
        FastLED.addLeds<UCS1903, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::UCS1903B:
        FastLED.addLeds<UCS1903B, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::UCS1904:
        FastLED.addLeds<UCS1904, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::UCS1912:
        FastLED.addLeds<UCS1912, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::UCS2903:
        FastLED.addLeds<UCS2903, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2801:
        FastLED.addLeds<WS2801, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2803:
        FastLED.addLeds<WS2803, D, C, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2811:
        FastLED.addLeds<WS2811, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2811_400KHZ:
        FastLED.addLeds<WS2811_400, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2812:
        FastLED.addLeds<WS2812, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2812B:
        FastLED.addLeds<WS2812B, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2813:
        FastLED.addLeds<WS2813, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2815:
        FastLED.addLeds<WS2815, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    // 16-bit chip, fed the 8-bit buffer
    case LEDChipset::WS2816:
        FastLED.addLeds<WS2816, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;
    case LEDChipset::WS2852:
        FastLED.addLeds<WS2852, D, RGB>(leds, N).setCorrection(TypicalLEDStrip);
        return true;

    default:
        return false;
    }
}

// =============================================================================
// Render task
// =============================================================================
// The task is the only writer of the pixel buffer and the only caller of FastLED.show(), which
// runs outside the lock so setters never wait for the wire. Every state change happens in a
// LedEngine section under the same lock (see LedEngine.h).
void Led::start_render_task() {
    if (render_task_handle != nullptr)
        return;

    render_task_stop = false;
    const BaseType_t core =
        XEWE_MODULE_LED_TASK_CORE < portNUM_PROCESSORS ? XEWE_MODULE_LED_TASK_CORE : 0;
    const BaseType_t created = xTaskCreatePinnedToCore(&Led::render_task_entry, "led_render",
        XEWE_MODULE_LED_TASK_STACK, this, XEWE_MODULE_LED_TASK_PRIORITY, &render_task_handle, core);

    if (created != pdPASS) {
        render_task_handle = nullptr;
        os.serial.print("! Led: render task not started (out of memory); no output");
    }
}

void Led::stop_render_task() {
    if (render_task_handle == nullptr)
        return;
    render_task_stop = true;
    // one frame period plus the wire time of a full buffer is enough; never wait forever
    if (render_task_ended != nullptr)
        xSemaphoreTake(render_task_ended, pdMS_TO_TICKS(1000));
    render_task_handle = nullptr;
}

void Led::render_task_entry(void* self) {
    static_cast<Led*>(self)->render_task();
}

void Led::render_task() {
    const TickType_t frame_period =
        std::max<TickType_t>(1, pdMS_TO_TICKS(XEWE_MODULE_LED_FRAME_MS));
    TickType_t last_wake = xTaskGetTickCount();
    fps_window_start_ms = millis();

    for (;;) {
        // the first frame comes one period after the start, as the frame clock the effects'
        // per-frame counters were tuned for
        if (xTaskDelayUntil(&last_wake, frame_period) == pdFALSE) {
            // overran the slot: resync and still sleep a tick, so loop() (priority 1) gets CPU
            last_wake = xTaskGetTickCount();
            vTaskDelay(1);
        }

        if (render_task_stop)
            break;

        bool blank;
        {
            RenderLock lock(io);
            blank = engine.take_blank_frame();
        }
        if (blank)
            FastLED.show(); // the black frame of a length change, before the next mode frame

        {
            RenderLock lock(io);
            engine.render_frame();
        }
        FastLED.show();

        count_frame();
    }

    xSemaphoreGive(render_task_ended);
    vTaskDelete(nullptr);
}

void Led::count_frame() {
    fps_counter++;
    const uint32_t now_ms = millis();
    if (now_ms - fps_window_start_ms < static_cast<uint32_t>(XEWE_MODULE_LED_FPS_WINDOW_S) * 1000)
        return;
    fps_calculated = fps_counter / XEWE_MODULE_LED_FPS_WINDOW_S;
    fps_counter = 0;
    fps_window_start_ms = now_ms;
}
