// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/pins/src/Pins/Pins.cpp


#include "Pins.h"


Pins::Pins(xewe::Os& host)
      : xewe::Module(host,
               /* id                  */ "pins",
               /* name                */ "Pins",
               /* description         */ "Allows direct hardware control (GPIO, ADC, I2C, PWM)",
               /* requires_init_setup */ false,
               /* can_be_disabled     */ true,
               /* has_cli_cmds        */ true)
{
    register_command({
        "gpio_read",
        "Read digital logic level. Returns: 0 (GND) or 1 (VCC). Configures pin as INPUT.",
        "$pins gpio_read 9",
        1,
        [this](xewe::span<const std::string> args){ gpio_read_cmd(args); }
    });

    register_command({
        "gpio_write",
        "Force pin to logic HIGH (1) or LOW (0). Configures pin as OUTPUT.",
        "$pins gpio_write 9 1",
        2,
        [this](xewe::span<const std::string> args){ gpio_write_cmd(args); }
    });

    register_command({
        "gpio_toggle",
        "Inverts the current state of a pin (HIGH -> LOW or LOW -> HIGH). Forces OUTPUT mode.",
        "$pins gpio_toggle 9",
        1,
        [this](xewe::span<const std::string> args){ gpio_toggle_cmd(args); }
    });

    register_command({
        "gpio_mode",
        "Set IO mode/resistors. Modes: 'in' (floating), 'out' (push-pull), 'in_pullup' (weak VCC), 'in_pulldown' (weak GND).",
        "$pins gpio_mode 9 in_pullup",
        2,
        [this](xewe::span<const std::string> args){ gpio_mode_cmd(args); }
    });

    register_command({
        "adc_read",
        "Read analog voltage. Returns raw integer (usually 0-4095 for 12-bit).",
        "$pins adc_read 4",
        1,
        [this](xewe::span<const std::string> args){ adc_read_cmd(args); }
    });

    register_command({
        "pwm_setup",
        "Attach PWM timer. Freq range: 1Hz-40MHz. Bits: 1-16. (ESP32 Core v3+ uses Pins directly).",
        "$pins pwm_setup 9 5000 8",
        3,
        [this](xewe::span<const std::string> args){ pwm_setup_cmd(args); }
    });

    register_command({
        "pwm_write",
        "Set PWM duty cycle on a specific pin. Max value = (2^res_bits) - 1.",
        "$pins pwm_write 9 128",
        2,
        [this](xewe::span<const std::string> args){ pwm_write_cmd(args); }
    });

    register_command({
        "pwm_stop",
        "Stop PWM on a pin (sets duty 0) and detaches the hardware timer.",
        "$pins pwm_stop 9",
        1,
        [this](xewe::span<const std::string> args){ pwm_stop_cmd(args); }
    });

    register_command({
        "i2c_scan",
        "Initializes I2C on specific SDA/SCL pins and scans for devices (0x01 - 0x77).",
        "$pins i2c_scan 21 22",
        2,
        [this](xewe::span<const std::string> args){ i2c_scan_cmd(args); }
    });

    register_command({
        "claims",
        "List the GPIOs claimed in the core pin registry and their owners (strapping pins marked).",
        "$pins claims",
        0,
        [this](xewe::span<const std::string> args){ claims_cmd(args); }
    });

    register_command({
        "release",
        "Release a GPIO that $pins claimed (pins of other modules are freed by their own remove).",
        "$pins release 9",
        1,
        [this](xewe::span<const std::string> args){ release_cmd(args); }
    });
}

bool Pins::take(int pin) {
    return xewe::pins::claim(pin, id.c_str());      // false: out of range or held by another module (reported)
}

bool Pins::take_arg(const std::string& arg, int& pin) {
    if (!xewe::str::parse_int(arg, pin)) {
        os.serial.print("! Pins: invalid <pin>");
        return false;
    }
    return take(pin);
}

void Pins::claims_cmd(xewe::span<const std::string>) {
    int n = 0;
    for (int gpio = 0; gpio < xewe::pins::kMaxGpio; ++gpio) {
        const char* owner = xewe::pins::owner_of(gpio);
        if (!owner) continue;
        os.serial.printf("GPIO %d: %s%s", gpio, owner, xewe::pins::is_strapping(gpio) ? " (strapping)" : "");
        ++n;
    }
    if (n == 0) os.serial.print("No GPIO claimed");
}

void Pins::release_cmd(xewe::span<const std::string> args) {
    int pin;
    if (!xewe::str::parse_int(args[0], pin)) {
        os.serial.print("! Pins: invalid <pin>");
        return;
    }
    if (xewe::pins::release(pin, id.c_str())) {
        os.serial.print("ok");
    } else {
        const char* owner = xewe::pins::owner_of(pin);
        os.serial.printf("! Pins: GPIO %d is %s", pin, owner ? (std::string("held by ") + owner).c_str() : "not claimed");
    }
}

std::string Pins::status(const bool verbose) const {
    // stateless: nothing to add to the base line
    std::string s = Module::status(false);
    if (verbose) os.serial.print(s);
    return s;
}

void Pins::gpio_read_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int pin;
    if (!take_arg(args[0], pin)) return;
    pinMode(pin, INPUT);
    os.serial.print(std::to_string(static_cast<int>(digitalRead(pin))));
}

void Pins::gpio_write_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int pin, lvl;
    if (!xewe::str::parse_int(args[0], pin) || !xewe::str::parse_int(args[1], lvl)) {
        os.serial.print("! Pins: invalid <pin> or <level>");
        return;
    }
    if (!take(pin)) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, lvl ? HIGH : LOW);
    os.serial.print("ok");
}

void Pins::gpio_toggle_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int pin;
    if (!take_arg(args[0], pin)) return;
    pinMode(pin, OUTPUT);
    int new_state = !digitalRead(pin);
    digitalWrite(pin, new_state);
    os.serial.print(std::to_string(new_state));
}

void Pins::gpio_mode_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int pin;
    if (!take_arg(args[0], pin)) return;
    const std::string& m = args[1];

    if      (m == "out") pinMode(pin, OUTPUT);
    else if (m == "in")  pinMode(pin, INPUT);
#ifdef INPUT_PULLDOWN
    else if (m == "in_pulldown") pinMode(pin, INPUT_PULLDOWN);
#endif
    else if (m == "in_pullup")   pinMode(pin, INPUT_PULLUP);
    else {
        os.serial.print("! Pins: valid modes: in | in_pullup | in_pulldown | out");
        return;
    }
    os.serial.print("ok");
}

void Pins::adc_read_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int pin;
    if (!take_arg(args[0], pin)) return;
    os.serial.print(std::to_string(analogRead(pin)));
}

void Pins::pwm_setup_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    uint8_t pin, bits;
    uint32_t freq;
    if (!xewe::str::parse_int(args[0], pin) ||
        !xewe::str::parse_int(args[1], freq) ||
        !xewe::str::parse_int(args[2], bits)) {
        os.serial.print("! Pins: required <pin> <freq_hz> <res_bits>");
        return;
    }
    if (!take(pin)) return;

    // arduino-esp32 3.x LEDC API: ledcAttach(pin, freq, resolution)
    if (!ledcAttach(pin, freq, bits)) {
        os.serial.print("! Pins: PWM attachment failed");
        return;
    }
    os.serial.print("ok");
}

void Pins::pwm_write_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    uint8_t pin;
    uint32_t duty;
    if (!xewe::str::parse_int(args[0], pin) || !xewe::str::parse_int(args[1], duty)) {
        os.serial.print("! Pins: required <pin> <duty_value>");
        return;
    }
    if (!take(pin)) return;
    // arduino-esp32 3.x LEDC API: ledcWrite(pin, duty)
    ledcWrite(pin, duty);
    os.serial.print("ok");
}

void Pins::pwm_stop_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    uint8_t pin;
    if (!xewe::str::parse_int(args[0], pin)) {
        os.serial.print("! Pins: required <pin>");
        return;
    }
    if (xewe::pins::owner_of(pin) && id != xewe::pins::owner_of(pin)) {
        os.serial.printf("! Pins: GPIO %u is held by %s", static_cast<unsigned>(pin), xewe::pins::owner_of(pin));
        return;
    }
    ledcWrite(pin, 0);
    ledcDetach(pin);
    xewe::pins::release(pin, id.c_str());
    os.serial.print("ok");
}

void Pins::i2c_scan_cmd(xewe::span<const std::string> args) {
    if (is_disabled(true)) return;

    int sda, scl;
    if (!xewe::str::parse_int(args[0], sda) || !xewe::str::parse_int(args[1], scl)) {
        os.serial.print("! Pins: required <sda_pin> <scl_pin>");
        return;
    }

    if (!take(sda)) return;
    if (!take(scl)) { xewe::pins::release(sda, id.c_str()); return; }
    Wire.begin(sda, scl);
    int found = 0;
    for (uint8_t addr = 1; addr < 0x78; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            char ln[8];
            snprintf(ln, sizeof(ln), "0x%02X", addr);
            os.serial.print(ln);
            found++;
        }
    }
    if (found == 0) os.serial.print("No I2C devices found");
    xewe::pins::release(sda, id.c_str());
    xewe::pins::release(scl, id.c_str());
}
