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
             (XEWE_WIFI_SSID and XEWE_WIFI_PSW both set: store them, no scan, join 3 times)
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
| Libraries | XeWeCore >=3.1.0,<4.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Settings and NVS keys

| Key (namespace `wifi`) | Type | Default | Meaning |
|---|---|---|---|
| `ssid` | str ≤ 32 | empty | network name, used by `$wifi connect` and at boot |
| `psw` | str ≤ 63 | empty | password; a SECRET row, never printed (`********`) |

### In Config.h

All names start with `XEWE_WIFI_`. `./setup.sh` (and `xewe modules select`) appends the module's
`src/Wifi/Config.h` as one marked block to the project's `Config.h`, every entry commented out; the file follows the scheme in
[XeWeCore's `doc/config.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/config.md), and an uncommented line in the project's `Config.h`, a dotenv value behind `INJECTED_FROM_DOTENV` or
`xewe build --define NAME=VALUE` wins over the module's default.

The file frames its default runtime values with `/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */` and its compile time defined constants with `/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */`.

**Default runtime values, asked at first boot.** Like every entry, the block holds them as a comment and a commented-out line, so a fresh project still asks:

```c
/* Network name (wifi/ssid), string literal, up to 32 characters; asked at first boot */
// #define XEWE_WIFI_SSID ""
/* Network password (wifi/psw), string literal, up to 63 characters; asked at first boot */
// #define XEWE_WIFI_PSW ""
```

| Define | Setting | Meaning |
|---|---|---|
| `XEWE_WIFI_SSID` | `ssid` | network name; set = no network question |
| `XEWE_WIFI_PSW` | `psw` | network password; set = no password question |

Uncomment a line and set the value to pre-answer it, or write `#define XEWE_WIFI_PSW INJECTED_FROM_DOTENV`
to take the password from the dotenv file at build time (the key is `XEWE_WIFI_PSW`; the value never
reaches the repository). What the first boot does:

- `XEWE_WIFI_SSID` and `XEWE_WIFI_PSW` both set: both are written to NVS, the scan is skipped and
  the join is tried three times. If all three fail, the module prints one `!` line and shows the
  scan menu.
- Only `XEWE_WIFI_SSID` set: the password is asked.
- Only `XEWE_WIFI_PSW` set: ignored, the network is chosen from the scan and the password is asked.

### Known issues

- **`loop()` waits for the network, by design.** While the module is enabled and disconnected,
  `loop()` does not return until WiFi is back. Each pass is a 5 s "Disable and reset WiFi module?"
  prompt (`get_yn`, default no), then a reconnect with the stored credentials: 3 attempts of 10 s
  and a 10 s "reset credentials?" prompt, so up to about 45 s, repeated until connected. A boot
  with stored credentials waits the same way once (`begin_routines_regular`, up to 30 s plus the
  prompt). The CLI and every other module wait meanwhile. This is intended: time, web-interface
  and scheduler (through time) need the network, so the device holds until it is back instead of
  running them offline. Answering yes to the prompt (and confirming) disables the module and its
  dependents, which ends the wait ([contract](../../doc/contract.md#6-known-limits), section 6).
- Credentials are stored in NVS unencrypted.

### Tests

`tests/board/test_wifi.py` (board tests) and `tests/unit/test_wifi.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module wifi              # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module wifi --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module wifi --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$wifi status`), `test_scan_lists_networks` (`$wifi scan`) and `test_settings_table_masks_password` (`$wifi schema`, `psw` masked) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board (first boot done, WiFi credentials stored, connected). A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).
