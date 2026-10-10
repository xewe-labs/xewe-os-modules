# web-interface — send CLI commands to the board over HTTP

## Overview

HTTP page and command endpoint for other devices on the network. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). It starts an HTTP server on
port 80 that serves a small dark-themed page with one input box, and accepts CLI commands from
any device on the local network at `GET /cmd?c=<command>`. Each command is echoed to the serial
console and run through the same command line as serial input, so every installed module can be
driven from a browser, a script or another microcontroller board.

## Highlights

- Routes: `GET /console` (embedded HTML page stored in flash with `PROGMEM`), `GET /` (the console unless a project takes it, see below) and `GET /cmd?c=<command>` → `200 OK` or `400 Empty Command`
- Settings table: `port` (u16, default 80, takes effect after a restart) and `root` (str ≤ 31: `GET /` redirects there, e.g. `/pad`; empty = the console)
- Root hook: `web_interface.set_root(handler)` (RAM only, call from `setup()`) lets a project page own `/`; it wins over the `root` setting. `redirect_root("/pad")` is the persisted form
- The page sends commands with `fetch('/cmd?c=' + encodeURIComponent(...))` and flashes "Command Sent" / "Error Sending" / "Connection Error"
- `status` reports server uptime and heap usage (used / total bytes)
- Requires the Wifi module and prints the page URL (`http://<local ip>`) once the server starts

## How it works

```
browser / curl → GET /cmd?c=$pins gpio_toggle 8 → WebServer (port 80) → os.cli.execute(command) → module callback
```

- **`WebInterface` class** (`src/WebInterface/`) — a `xewe::Module` with id `web_interface`; requires Wifi (`add_requirement(wifi)`), cannot be disabled; `loop()` calls `handleClient()`.
- **`get_server()`** — exposes the `WebServer` so other code can register more routes; **`set_root(handler)`** / **`redirect_root(path)`** hand `/` to a project page.

### Commands

**Prefix:** `$web_interface` · requires Wifi

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | Server uptime and memory usage (plus the `port`/`root` lines). | `$web_interface status` |
| **`set`** / **`get`** / **`schema`** | Settings table: `port`, `root`. | `$web_interface set root /pad` |

### HTTP API

| Route | Method | Response |
|---|---|---|
| `/` | GET | the project's handler (`set_root`), else a 302 to the `root` setting, else the command page |
| `/console` | GET | the command page (`text/html`), always |
| `/cmd?c=<command>` | GET | runs `<command>`; `200 OK`, or `400 Empty Command` without `c` |

```bash
curl "http://192.168.1.50/cmd?c=%24pins%20gpio_toggle%208"
```

There is no authentication: anyone on the same network can send any command, including
`$system` commands. Use it on trusted networks only.

### Requirements

| | |
|---|---|
| Modules | [wifi](../wifi) |
| Libraries | XeWeCore >=2.1.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Settings and NVS keys

| Key (namespace `web_interface`) | Type | Default | Meaning |
|---|---|---|---|
| `port` | u16 1–65535 | 80 | HTTP port; applies after a restart (RESTART row) |
| `root` | str ≤ 31 | empty | `GET /` redirects here (e.g. `/pad`); empty = the console page |

### Tests

`tests/board/test_web-interface.py` (board tests) and `tests/unit/test_web-interface.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module web-interface              # lock chip
build/tools/.venv/bin/python -m xewe test --module web-interface --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module web-interface --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$web_interface status`), `test_status_reports_server` (the web server block of `$web_interface status`) and `test_settings_table` (`$web_interface schema`) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board with WiFi connected. Tests read the serial output only; they make no HTTP requests. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).
