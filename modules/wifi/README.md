# wifi — joins a WiFi network and keeps it connected

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
| **`set`** / **`get`** / **`schema`** | Settings table: `ssid` (str ≤ 32), `psw` (str ≤ 63, secret: shown as `********`); `$wifi connect` uses them. | `$wifi set ssid "My Net"` |

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.1.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Settings and NVS keys

| Key (namespace `wifi`) | Type | Default | Meaning |
|---|---|---|---|
| `ssid` | str ≤ 32 | empty | network name, used by `$wifi connect` and at boot |
| `psw` | str ≤ 63 | empty | password; a SECRET row, never printed (`********`) |

### In Config.h

Compile-time values, `XEWE_MODULE_WIFI_<VAR>`. `./setup.sh` (and `xewe modules select`) appends the
module's `src/Wifi/Config.h` as one marked block to the project's `Config.h`; edit the value there.

| Define | Default | Meaning |
|---|---|---|
| `XEWE_MODULE_WIFI_DEBUG` | 0 | 1 prints debug output, including the credentials (bench only) |

### Known issues

- **`loop()` blocks while disconnected.** It runs `while (WiFi.status() != WL_CONNECTED)` with a
  5 s "Disable and reset WiFi module?" prompt (`get_yn`) and a reconnect attempt per pass. This
  breaks the "loop must not block" rule: the CLI and every other module stall until WiFi is back.
  The fix is a non-blocking reconnect state machine (CONTRACT.md section 6).
- Set `XEWE_MODULE_WIFI_DEBUG` to 1 only on a bench: it prints credentials to serial.
  Credentials are stored in NVS unencrypted.

### Tests

`tests/board/test_wifi.py` (board tests) and `tests/unit/test_wifi.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module wifi              # lock chip
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module wifi --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$wifi status`), `test_scan_lists_networks` (`$wifi scan`) and `test_settings_table_masks_password` (`$wifi schema`, `psw` masked) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board (first boot done, WiFi credentials stored, connected). A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).
