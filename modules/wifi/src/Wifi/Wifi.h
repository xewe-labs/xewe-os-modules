// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/wifi/src/Wifi/Wifi.h
#pragma once

#include <WiFi.h>
#include <set>

#include <XeWeCore.h>

#ifndef DEBUG_Wifi
#define DEBUG_Wifi 0
#endif


class Wifi : public xewe::Module {
public:
    explicit                 Wifi                    (xewe::Os& host);

    // optional implementation
    void                     begin_routines_required ()        override;
    void                     begin_routines_init     ()        override;
    void                     begin_routines_regular  ()        override;

    void                     loop                    ()                               override;

    void                     reset                   (const bool verbose      = false,
                                                      const bool do_restart   = true,
                                                      const bool keep_enabled = true) override;

    std::string              status                  (const bool verbose = false)     const override;

    // other methods
    bool                     connect                 (bool prompt_for_credentials);
    bool                     disconnect              (bool verbose = false);
    bool                     is_connected            (bool verbose = false)           const;
    bool                     is_disconnected         (bool verbose = false)           const;

    std::string              get_local_ip            ()                               const;
    std::string              get_ssid                ()                               const;
    std::string              get_mac_address         ()                               const;

private:
    std::vector<std::string> scan                    (bool verbose);

    bool                     join                    (std::string_view ssid,
                                                      std::string_view password,
                                                      uint16_t         timeout_ms  = 15000,
                                                      uint8_t          retry_count = 1);
    bool                     read_stored_credentials (std::string& ssid,
                                                      std::string& password);
    uint8_t                  prompt_credentials      (std::string& ssid,
                                                      std::string& password);
};
