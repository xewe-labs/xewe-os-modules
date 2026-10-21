// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/wifi/src/Wifi/Wifi.cpp

#include "Wifi.h"

Wifi::Wifi(xewe::Os& host)
    : xewe::Module(host,
          /* id                  */ "wifi",
          /* name                */ "Wifi",
          /* description         */
          "Allows to connect to a local WiFi network.\\sepNOTE: Some WiFi networks (ex: "
          "cafes/hotspots) have AP client isolation. In that case you can't use local network "
          "features",
          /* requires_init_setup */ true,
          /* can_be_disabled     */ true,
          /* has_cli_cmds        */ true) {
    register_command({"connect", "Connect or reconnect to WiFi", "$wifi connect", 0,
        [this](xewe::span<const std::string>) { connect(true); }});
    register_command({"disconnect", "Disconnect from WiFi", "$wifi disconnect", 0,
        [this](xewe::span<const std::string>) { disconnect(true); }});
    register_command({"scan", "List available WiFi networks", "$wifi scan", 0,
        [this](xewe::span<const std::string>) { scan(true); }});
}

xewe::Settings Wifi::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Wifi::stored_ssid>(
            "ssid", 32, XEWE_WIFI_SSID, "Network name; used by $wifi connect"),
        xewe::setting<&Wifi::stored_psw>(
            "psw", 63, XEWE_WIFI_PSW, "Network password", xewe::SettingDef::SECRET),
    };
    return {table, this};
}

void Wifi::begin_routines_required() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(os.system.get_device_name().c_str());
    disconnect(false);
    delay(100);
}

void Wifi::begin_routines_init() {
#if !defined(XEWE_WIFI_SSID_PROMPT) && !defined(XEWE_WIFI_PSW_PROMPT)
    // both pre-answered by the build: stored unless NVS holds another network, joined without a
    // scan; the scan is the fallback
    if (stored_ssid == XEWE_WIFI_SSID && stored_psw == XEWE_WIFI_PSW) {
        apply_setting("ssid", XEWE_WIFI_SSID);
        apply_setting("psw", XEWE_WIFI_PSW);
    }
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (join(stored_ssid, stored_psw, 10000, 1))
            return;
    }
    os.serial.print("! Wifi: unable to join " + stored_ssid + "; select a network");
    if (!select_network())
        disable(false, false);
#elif !defined(XEWE_WIFI_SSID_PROMPT)
    // only the network name pre-answered: its password is asked, no scan; the scan is the fallback
    const std::string pwd =
        os.serial.get_string("Selected: '" + std::string(XEWE_WIFI_SSID) + "'\nPassword: ");
    if (join(XEWE_WIFI_SSID, pwd, 10000, 1)) {
        apply_setting("ssid", XEWE_WIFI_SSID);
        apply_setting("psw", pwd);
        return;
    }
    os.serial.print("! Wifi: unable to join " + std::string(XEWE_WIFI_SSID) + "; select a network");
    if (!select_network())
        disable(false, false);
#else
    // a password without a network name is ignored: both are asked
    if (!connect(true))
        disable(false, false);
#endif
}

void Wifi::begin_routines_regular() {
    connect(false);
}

void Wifi::loop() {
    // blocks until connected while the module is active (README, Known issues)
    while (WiFi.status() != WL_CONNECTED) {
        // one attempt with a timeout: a timed prompt that defaults to no
        bool user_disabled = os.serial.get_yn(
            "Wifi connection lost\nReconnecting in 5 seconds\nDisable and reset WiFi module?",
            1, // retry_count
            5000, // timeout_ms
            false // default_value
        );

        if (user_disabled) {
            disable(true);
        }
        connect(false);
    }
}

void Wifi::reset(const bool verbose, const bool do_restart, const bool keep_enabled) {
    disconnect(false);
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Wifi::status(bool verbose) const {
    std::string s = Module::status(false);
    if (is_enabled()) {
        if (is_disconnected()) {
            s += "\ndisconnected";
        } else {
            s += "\nConnected to " + get_ssid() + "\nLocal ip: " + get_local_ip() +
                "\nMac: " + get_mac_address();
        }
    }
    if (verbose)
        os.serial.print(s);
    return s;
}

bool Wifi::connect(bool prompt_for_credentials) {
    if (is_disabled(true))
        return false;
    if (is_connected(true))
        return true;

    std::string ssid, pwd;
    if (read_stored_credentials(ssid, pwd)) {

        os.serial.print("Stored WiFi credentials found");
        if (join(ssid, pwd, 10000, 3)) {
            return true;
        } else {
            os.serial.print("! Wifi: stored credentials not valid");
            if (!prompt_for_credentials) {
                os.serial.print("Use '$wifi reset' to reset credentials");
            }
        }
    } else {
        os.serial.print("Stored WiFi credentials not found");
        if (!prompt_for_credentials) {
            os.serial.print("Type '$wifi connect' to select a new network");
        }
    }

    return prompt_for_credentials && select_network();
}

bool Wifi::select_network() {
    std::string ssid, pwd;
    while (is_disconnected()) {
        uint8_t prompt_status = prompt_credentials(ssid, pwd);

        if (prompt_status == 1) { // User exit
            os.serial.print("Terminated WiFi setup");
            return false;
        } else if (prompt_status == 2) { // Rescan
            continue;
        } else if (prompt_status == 3) { // Invalid
            os.serial.print("! Wifi: invalid choice");
            continue;
        } else {
            if (join(ssid, pwd, 10000, 1)) {
                // validated + saved by the table (an over-long value is refused and not stored)
                apply_setting("ssid", ssid);
                apply_setting("psw", pwd);
                return true;
            }
        }
    }
    return false;
}

bool Wifi::disconnect(bool verbose) {
    if (is_disabled(verbose))
        return true;
    if (is_disconnected(verbose))
        return true;

    WiFi.disconnect();
    unsigned long start = millis();
    constexpr unsigned long timeout = 5000;
    while (WiFi.status() == WL_CONNECTED && millis() - start < timeout) {
        delay(100);
    }
    bool done = (WiFi.status() != WL_CONNECTED);

    if (verbose)
        os.serial.print("Wifi: disconnected");
    return done;
}

bool Wifi::is_connected(bool verbose) const {
    if (is_disabled())
        return false;
    bool conn = (WiFi.status() == WL_CONNECTED);
    if (verbose && conn) {
        os.serial.printf("Connected to %s", get_ssid().c_str());
    }
    return conn;
}

bool Wifi::is_disconnected(bool verbose) const {
    if (is_disabled())
        return true;
    bool conn = (WiFi.status() == WL_CONNECTED);
    if (verbose && !conn) {
        os.serial.print("! Wifi: not connected; use $wifi connect");
    }
    return !conn;
}
std::string Wifi::get_local_ip() const {
    if (is_disabled(true))
        return {};
    if (is_disconnected(true))
        return {};

    auto ip = WiFi.localIP();
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return std::string(buf);
}

std::string Wifi::get_ssid() const {
    if (is_disabled(true))
        return {};
    if (is_disconnected(true))
        return {};
    return stored_ssid;
}

std::string Wifi::get_mac_address() const {
    if (is_disabled(true))
        return {};
    if (is_disconnected(true))
        return {};

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
        mac[4], mac[5]);
    return std::string(buf);
}

std::vector<std::string> Wifi::scan(bool verbose) {
    if (is_disabled(true))
        return {};

    os.serial.print("Scanning WiFi networks...");
    int num_networks = WiFi.scanNetworks(true, true);
    while (num_networks == WIFI_SCAN_RUNNING) {
        delay(10);
        num_networks = WiFi.scanComplete();
    }

    std::vector<std::string> unique_ssid_list;
    for (int i = 0; i < num_networks; ++i) {
        std::string ssid(WiFi.SSID(i).c_str());
        if (ssid.empty() ||
            std::find(unique_ssid_list.begin(), unique_ssid_list.end(), ssid) !=
                unique_ssid_list.end())
            continue;
        unique_ssid_list.push_back(std::move(ssid));
    }

    if (verbose) {
        for (size_t j = 0; j < unique_ssid_list.size(); ++j) {
            os.serial.printf("%zu. %s", j, unique_ssid_list[j].c_str());
        }
    }

    WiFi.scanDelete();
    return unique_ssid_list;
}

bool Wifi::join(
    std::string_view ssid, std::string_view password, uint16_t timeout_ms, uint8_t retry_count) {
    if (is_disabled(true))
        return false;

    for (uint8_t retry_counter = 0; retry_counter < retry_count; retry_counter++) {
        // no line end, so the progress dots follow on the same line
        os.serial.print(std::string("Joining ") + std::string(ssid), "");

        WiFi.begin(ssid.data(), password.data());
        unsigned long start = millis();

        while (millis() - start < timeout_ms) {
            os.serial.print(".", "");

            if (WiFi.status() == WL_CONNECTED) {
                os.serial.print(""); // Finish the dot line
                os.serial.printf("\nJoined %s\nLocal ip: %s\nMac: %s", ssid.data(),
                    get_local_ip().c_str(), get_mac_address().c_str());
                return true;
            }
            delay(200);
        }
        WiFi.disconnect(true);
        os.serial.printf("\n! Wifi: unable to join %s\n", ssid.data());
        os.serial.print(
            "Check the password\ntry moving closer to router\nand restarting the router\nRetrying");
    }
    if (retry_count > 1) {
        // one attempt, 10 s timeout
        bool reset_credentials = os.serial.get_yn("Would you like to reset credentials?", 1, 10000);
        if (reset_credentials)
            reset();
    }

    return false;
}

bool Wifi::read_stored_credentials(std::string& ssid, std::string& password) {
    if (is_disabled(true))
        return false;
    ssid = stored_ssid; // loaded by the core from NVS at begin
    password = stored_psw;
    return ssid.length() > 0;
}

uint8_t Wifi::prompt_credentials(std::string& ssid, std::string& password) {
    if (is_disabled(true))
        return 2;

    std::vector<std::string> networks = scan(true);

    // Min value -3 covers the menu options (-1, -2, -3) and max is high enough for network indices
    int choice = os.serial.get_int("\nSelect network by number; or enter\n-1 to exit\n-2 to "
                                   "rescan\n-3 to enter custom SSID\nSelection: ",
        -3, std::numeric_limits<int>::max());

    if (choice == -1) {
        return 1;
    } else if (choice == -2) {
        return 2;
    } else if (choice == -3) {
        ssid = os.serial.get_string("Enter custom SSID: ");
    } else if (choice >= 0 && choice < static_cast<int>(networks.size())) {
        ssid = networks[choice];
    } else {
        return 3;
    }

    password = os.serial.get_string("Selected: '" + ssid + "'\nPassword: ");

    return 0;
}
