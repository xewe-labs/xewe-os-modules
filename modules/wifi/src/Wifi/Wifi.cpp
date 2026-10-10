// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/wifi/src/Wifi/Wifi.cpp

#include "Wifi.h"


Wifi::Wifi(xewe::Os& host)
    : xewe::Module(host,
          /* id                  */ "wifi",
          /* name                */ "Wifi",
          /* description         */ "Allows to connect to a local WiFi network.\\sepNOTE: Some WiFi networks (ex: cafes/hotspots) have AP client isolation. In that case you can't use local network features",
          /* requires_init_setup */ true,
          /* can_be_disabled     */ true,
          /* has_cli_cmds        */ true
    )
{
    register_command({
        "connect",
        "Connect or reconnect to WiFi",
        "$wifi connect",
        0,
        [this](xewe::span<const std::string>) { connect(true); }
    });
    register_command({
        "disconnect",
        "Disconnect from WiFi",
        "$wifi disconnect",
        0,
        [this](xewe::span<const std::string>) { disconnect(true); }
    });
    register_command({
        "scan",
        "List available WiFi networks",
        "$wifi scan",
        0,
        [this](xewe::span<const std::string>) { scan(true); }
    });
}

xewe::Settings Wifi::settings() const {
    static constexpr xewe::SettingDef table[] = {
        xewe::setting<&Wifi::stored_ssid>("ssid", 32, "", "Network name; used by $wifi connect"),
        xewe::setting<&Wifi::stored_psw> ("psw", 63, "", "Network password", xewe::SettingDef::SECRET),
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
    if (!connect(true))
        disable(false, false);
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
            1,    // retry_count
            5000, // timeout_ms
            false // default_value
        );

        if (user_disabled) {
            disable(true);
        }
        connect(false);
    }
}

void Wifi::reset(const bool verbose,
                 const bool do_restart,
                 const bool keep_enabled) {
    disconnect(false);
    Module::reset(verbose, do_restart, keep_enabled);
}

std::string Wifi::status(bool verbose) const {
    DBG_PRINTF(Wifi, "status(verbose=%d)\n", verbose);
    std::string s = Module::status(false);
    if (is_enabled()) {
        if (is_disconnected()) {
            s += "\ndisconnected";
        } else {
            s += "\nConnected to " + get_ssid() + "\nLocal ip: " + get_local_ip() + "\nMac: " + get_mac_address();
        }
    }
    if (verbose) os.serial.print(s);
    return s;
}

bool Wifi::connect(bool prompt_for_credentials) {
    DBG_PRINTF(Wifi, "connect(prompt_for_credentials=%d)\n", prompt_for_credentials);
    if (is_disabled(true)) return false;
    if (is_connected(true)) return true;

    std::string ssid, pwd;
    if (read_stored_credentials(ssid, pwd)) {
        DBG_PRINTLN(Wifi, "connect(): stored credentials found");

        os.serial.print("Stored WiFi credentials found");
        if (join(ssid, pwd, 10000, 3)) {
            DBG_PRINTLN(Wifi, "connect(): join() succeeded with stored credentials");
            return true;
        } else {
            DBG_PRINTLN(Wifi, "connect(): join() failed with stored credentials");
            os.serial.print("! Wifi: stored credentials not valid");
            if (!prompt_for_credentials) {
                os.serial.print("Use '$wifi reset' to reset credentials");
            }
        }
    } else {
        DBG_PRINTLN(Wifi, "connect(): no stored credentials");
        os.serial.print("Stored WiFi credentials not found");
        if (!prompt_for_credentials) {
            os.serial.print("Type '$wifi connect' to select a new network");
        }
    }

    if (prompt_for_credentials) {
        while (is_disconnected()) {
            DBG_PRINTLN(Wifi, "connect(): prompting for credentials");
            uint8_t prompt_status = prompt_credentials(ssid, pwd);
            DBG_PRINTF(Wifi, "connect(): prompt_credentials returned %d\n", prompt_status);

            if (prompt_status == 1) { // User exit
                DBG_PRINTLN(Wifi, "connect(): user terminated setup");
                os.serial.print("Terminated WiFi setup");
                return false;
            } else if (prompt_status == 2) { // Rescan
                DBG_PRINTLN(Wifi, "connect(): invalid choice, retrying");
                continue;
            } else if (prompt_status == 3) { // Invalid
                DBG_PRINTLN(Wifi, "connect(): invalid choice, retrying");
                os.serial.print("! Wifi: invalid choice");
                continue;
            } else {
                DBG_PRINTLN(Wifi, "connect(): attempting join() with user credentials");
                if (join(ssid, pwd, 10000, 1)) {
                    DBG_PRINTLN(Wifi, "connect(): join() succeeded with user credentials");
                    // validated + saved by the table (an over-long value is refused and not stored)
                    apply_setting("ssid", ssid);
                    apply_setting("psw", pwd);
                    return true;
                }
            }
        }
    }
    return false;
}

bool Wifi::disconnect(bool verbose) {
    DBG_PRINTLN(Wifi, "disconnect()");
    if (is_disabled(verbose)) return true;
    if (is_disconnected(verbose)) return true;

    DBG_PRINTLN(Wifi, "disconnect(): start");
    WiFi.disconnect();
    unsigned long           start   = millis();
    constexpr unsigned long timeout = 5000;
    while (WiFi.status() == WL_CONNECTED && millis() - start < timeout) {
        delay(100);
    }
    bool done = (WiFi.status() != WL_CONNECTED);
    DBG_PRINTF(Wifi, "disconnect(): %s\n", done ? "success" : "timeout/failure");

    if (verbose) os.serial.print("Wifi: disconnected");
    return done;
}

bool Wifi::is_connected(bool verbose) const {
    DBG_PRINTF(Wifi, "is_connected(verbose=%d)\n", verbose);
    if (is_disabled()) return false;
    bool conn = (WiFi.status() == WL_CONNECTED);
    if (verbose && conn) {
        DBG_PRINTLN(Wifi, "is_connected(): true");
        os.serial.printf("Connected to %s", get_ssid().c_str());
    }
    DBG_PRINTF(Wifi, "is_connected(): %s\n", conn ? "true" : "false");
    return conn;
}

bool Wifi::is_disconnected(bool verbose) const {
    DBG_PRINTF(Wifi, "is_disconnected(verbose=%d)\n", verbose);
    if (is_disabled()) return true;
    bool conn = (WiFi.status() == WL_CONNECTED);
    if (verbose && !conn) {
        DBG_PRINTLN(Wifi, "is_disconnected(): true");
        os.serial.print("! Wifi: not connected; use $wifi connect");
    }
    DBG_PRINTF(Wifi, "is_disconnected(): %s\n", !conn ? "true" : "false");
    return !conn;
}
std::string Wifi::get_local_ip() const {
    DBG_PRINTLN(Wifi, "get_local_ip()");
    if (is_disabled(true)) return {};
    if (is_disconnected(true)) return {};

    auto ip = WiFi.localIP();
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
        ip[0], ip[1], ip[2], ip[3]
    );
    DBG_PRINTF(Wifi, "get_local_ip(): %s\n", buf);
    return std::string(buf);
}

std::string Wifi::get_ssid() const {
    DBG_PRINTLN(Wifi, "get_ssid()");
    if (is_disabled(true)) return {};
    if (is_disconnected(true)) return {};
    return stored_ssid;
}

std::string Wifi::get_mac_address() const {
    DBG_PRINTLN(Wifi, "get_mac_address()");
    if (is_disabled(true)) return {};
    if (is_disconnected(true)) return {};

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[18];
    snprintf(buf, sizeof(buf),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2],
        mac[3], mac[4], mac[5]
    );
    DBG_PRINTF(Wifi, "get_mac_address(): %s\n", buf);
    return std::string(buf);
}

std::vector<std::string> Wifi::scan(bool verbose) {
    DBG_PRINTF(Wifi, "scan(verbose=%d)\n", verbose);
    if (is_disabled(true)) return {};

    DBG_PRINTLN(Wifi, "scan(): starting scan");
    os.serial.print("Scanning WiFi networks...");
    int num_networks = WiFi.scanNetworks(true, true);
    while (num_networks == WIFI_SCAN_RUNNING) {
        delay(10);
        num_networks = WiFi.scanComplete();
    }
    DBG_PRINTF(Wifi, "scan(): scan complete, %d networks found\n", num_networks);

    std::vector<std::string> unique_ssid_list;
    for (int i = 0; i < num_networks; ++i) {
        std::string ssid(WiFi.SSID(i).c_str());
        if (ssid.empty() || std::find(unique_ssid_list.begin(), unique_ssid_list.end(), ssid) != unique_ssid_list.end()) continue;
        DBG_PRINTF(Wifi, "scan(): adding [%s]\n", ssid.c_str());
        unique_ssid_list.push_back(std::move(ssid));
    }

    if (verbose) {
        for (size_t j = 0; j < unique_ssid_list.size(); ++j) {
            os.serial.printf("%zu. %s", j, unique_ssid_list[j].c_str());
        }
    }

    WiFi.scanDelete();
    DBG_PRINTLN(Wifi, "scan(): done");
    return unique_ssid_list;
}

bool Wifi::join(std::string_view ssid,
                std::string_view password,
                uint16_t timeout_ms,
                uint8_t retry_count) {
    DBG_PRINTF(Wifi,
        "join(ssid='%.*s', password='%.*s')\n",
        int(ssid.size()), ssid.data(),
        int(password.size()), password.data()
    );
    if (is_disabled(true)) return false;

    for (uint8_t retry_counter = 0; retry_counter < retry_count; retry_counter++) {
        // no line end, so the progress dots follow on the same line
        os.serial.print(std::string("Joining ") + std::string(ssid), "");

        DBG_PRINTF(Wifi, "join(): ssid='%.*s'\n", int(ssid.size()), ssid.data());
        WiFi.begin(ssid.data(), password.data());
        unsigned long start = millis();

        while (millis() - start < timeout_ms) {
            os.serial.print(".", "");

            if (WiFi.status() == WL_CONNECTED) {
                DBG_PRINTLN(Wifi, "join(): connected");
                os.serial.print(""); // Finish the dot line
                os.serial.printf("\nJoined %s\nLocal ip: %s\nMac: %s",
                    ssid.data(),
                    get_local_ip().c_str(),
                    get_mac_address().c_str()
                );
                return true;
            }
            delay(200);
        }
        WiFi.disconnect(true);
        os.serial.printf("\n! Wifi: unable to join %s\n", ssid.data());
        os.serial.print("Check the password\ntry moving closer to router\nand restarting the router\nRetrying");
        DBG_PRINTLN(Wifi, "join(): timeout, disconnected");
    }
    if (retry_count > 1) {
        // one attempt, 10 s timeout
        bool reset_credentials = os.serial.get_yn("Would you like to reset credentials?", 1, 10000);
        if (reset_credentials) reset();
    }

    return false;
}

bool Wifi::read_stored_credentials(std::string& ssid,
                                   std::string& password) {
    DBG_PRINTLN(Wifi, "read_stored_credentials()");
    if (is_disabled(true)) return false;
    ssid     = stored_ssid;                 // loaded by the core from NVS at begin
    password = stored_psw;
    DBG_PRINTF(Wifi, "read_stored_credentials(): %s\n", ssid.length() > 0 ? "found" : "none");
    return ssid.length() > 0;
}

uint8_t Wifi::prompt_credentials(std::string& ssid,
                                 std::string& password) {
    DBG_PRINTLN(Wifi, "prompt_credentials()");
    if (is_disabled(true)) return 2;

    std::vector<std::string> networks = scan(true);

    // Min value -3 covers the menu options (-1, -2, -3) and max is high enough for network indices
    int                      choice   = os.serial.get_int(
        "\nSelect network by number; or enter\n-1 to exit\n-2 to rescan\n-3 to enter custom SSID\nSelection: ",
        -3,
        std::numeric_limits<int>::max()
    );

    DBG_PRINTF(Wifi, "prompt_credentials(): user choice = %d\n", choice);

    if (choice == -1) {
        DBG_PRINTLN(Wifi, "prompt_credentials(): user exit");
        return 1;
    } else if (choice == -2) {
        DBG_PRINTLN(Wifi, "prompt_credentials(): user rescan");
        return 2;
    } else if (choice == -3) {
        DBG_PRINTLN(Wifi, "prompt_credentials(): user custom ssid");
        ssid = os.serial.get_string("Enter custom SSID: ");
    } else if (choice >= 0 && choice < static_cast<int>(networks.size())) {
        ssid = networks[choice];
        DBG_PRINTF(Wifi, "prompt_credentials(): selected ssid = %s\n", ssid.c_str());
    } else {
        DBG_PRINTLN(Wifi, "prompt_credentials(): invalid choice");
        return 3;
    }

    password = os.serial.get_string("Selected: '" + ssid + "'\nPassword: ");

    DBG_PRINTLN(Wifi, "prompt_credentials(): password entered");
    DBG_PRINTF(Wifi,
        "prompt_credentials(): final ssid='%s', password='%s'\n",
        ssid.c_str(), password.c_str()
    );

    return 0;
}
