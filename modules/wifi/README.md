# wifi — joins a WiFi network and keeps it connected

XeWe OS module · created 2026-09-15 (split out of xewe-os, where it was developed from 2026-01) · Solo: Max Dokukin · Status: Active (0.2.0)

## Overview

Connects to a local WiFi network and keeps the connection alive. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). On first boot it scans for
networks and asks for one on the serial console; the chosen SSID and password are stored in NVS
and reused on every later boot. If the connection drops, the module keeps reconnecting and offers
to disable itself. Other modules (WebInterface, Time) take it as a requirement.

## Highlights

- Interactive network selection on first boot: numbered scan results, rescan, custom SSID or exit (`Wifi::prompt_credentials`)
- Stored credentials are retried 3 times with a 10 s timeout each before the module asks to reset them (`Wifi::connect`, `Wifi::join`)
- Scan results are de-duplicated by SSID and hidden networks are skipped (`Wifi::scan`)
- Device hostname is set from the system device name; station mode only (`begin_routines_required`)

## How it works

```
first boot:  begin_routines_init → connect(prompt) → scan → choose → join → store ssid/psw in NVS
later boots: begin_routines_regular → connect(no prompt) → join(stored, 10 s × 3)
loop:        while not connected → 5 s "disable and reset?" prompt → reconnect
```

- **`Wifi` class** (`src/Wifi/`) — a `xewe::Module` with id `wifi`; requires initial setup, can be disabled, registers three commands.
- **Status** — `status()` returns the SSID, local IP and MAC address, which appear in `$system status`.

### Commands

**Prefix:** `$wifi` · asks for a network on first boot · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`connect`** | Connect or reconnect; prompts for a network if needed. | `$wifi connect` |
| **`disconnect`** | Disconnect from WiFi. | `$wifi disconnect` |
| **`scan`** | List available networks. | `$wifi scan` |

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.0.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Known issues

- **`loop()` blocks while disconnected.** It runs `while (WiFi.status() != WL_CONNECTED)` with a
  5 s "Disable and reset WiFi module?" prompt (`get_yn`) and a reconnect attempt per pass. This
  breaks the "loop must not block" rule: the CLI and every other module stall until WiFi is back.
  Ported unchanged on purpose (CONTRACT.md section 7.1); a non-blocking reconnect state machine is
  planned for step 5, when a board can verify it.
- Set `-DDEBUG_Wifi=1` for debug output only on a bench: it prints credentials to serial.
  Credentials are stored in NVS unencrypted.

### Tests

`tests/test_wifi.py`, run through an xewe-os harness (see the repo [README](../../README.md)):

```sh
build/.venv/bin/python -m xewe test --module wifi              # lock chip
build/.venv/bin/python -m xewe test --module wifi --all-chips  # c3, c6, s3
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$wifi status`) and `test_scan_lists_networks` (`$wifi scan`) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board (first boot done, WiFi credentials stored, connected). A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts (`get_yn` waits forever), so provision it by hand first
  (CONTRACT.md section 7.2).
