# web-interface — send CLI commands to the board over HTTP

XeWe OS module · created 2026-09-15 (split out of xewe-os, where it was developed from 2026-01) · Solo: Max Dokukin · Status: Active (0.2.0)

## Overview

HTTP page and command endpoint for other devices on the network. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). It starts an HTTP server on
port 80 that serves a small dark-themed page with one input box, and accepts CLI commands from
any device on the local network at `GET /cmd?c=<command>`. Each command is echoed to the serial
console and run through the same command line as serial input, so every installed module can be
driven from a browser, a script or another microcontroller board.

## Highlights

- Two routes: `GET /` (embedded HTML page stored in flash with `PROGMEM`) and `GET /cmd?c=<command>` → `200 OK` or `400 Empty Command`
- The page sends commands with `fetch('/cmd?c=' + encodeURIComponent(...))` and flashes "Command Sent" / "Error Sending" / "Connection Error"
- `status` reports server uptime and heap usage (used / total bytes)
- Requires the Wifi module and prints the page URL (`http://<local ip>`) once the server starts

## How it works

```
browser / curl → GET /cmd?c=$pins gpio_toggle 8 → WebServer (port 80) → os.cli.execute(command) → module callback
```

- **`WebInterface` class** (`src/WebInterface/`) — a `xewe::Module` with id `web_interface`; requires Wifi (`add_requirement(wifi)`), cannot be disabled; `loop()` calls `handleClient()`.
- **`get_server()`** — exposes the `WebServer` so other code can register more routes.

### Commands

**Prefix:** `$web_interface` · requires Wifi

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | Server uptime and memory usage. | `$web_interface status` |

### HTTP API

| Route | Method | Response |
|---|---|---|
| `/` | GET | the command page (`text/html`) |
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
| Libraries | XeWeCore >=2.0.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/test_web-interface.py`, run through an xewe-os harness (see the repo [README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module web-interface              # lock chip
build/tools/.venv/bin/python -m xewe test --module web-interface --all-chips  # c3, c6, s3
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$web_interface status`) and `test_status_reports_server` (the web server block of `$web_interface status`; the module has no commands of its own) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board with WiFi connected. Tests read the serial output only; no HTTP requests in phase 1. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts (`get_yn` waits forever), so provision it by hand first
  (CONTRACT.md section 7.2).
