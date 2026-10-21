# led-web — LED control page in the browser

## Overview

A web page for the [led](../led) module. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os),
built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). The page (mode menu, the current
mode's parameter sliders, colour, brightness, on/off) is served at `/` by the
[web-interface](../web-interface) module's HTTP server; its changes go to the strip through
`GET /set`, and every LED change, whatever made it (this page, another page, the CLI, a button, a
schedule, another module), is pushed to every open page as a WebSocket text frame on port 81.

## Highlights

- The page, stylesheet and script are served byte for byte from flash (`PROGMEM`, `src/LedWeb/pages/`)
- Routes on the web interface's server: `/` (taken with `web_interface.set_root`), `/set`, `/state`, `/modes`, `/name` and the page assets; each answer carries the CORS headers `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE, PUT`, `Access-Control-Allow-Headers: Content-Type`
- WebSocket server on port 81: a `led` listener turns every change into one frame for all pages, including the page that made it; a heartbeat every second while a page is connected
- A colour from the page is read with the core hex parser (`xewe::color::parse_hex_color` from `XeWeCore/Utils/Color.h`); anything that is not six hex digits is refused with `400 Invalid color`
- The handlers and frames are in a pure header (`LedWebApi.h`) and are tested on the developer machine

## How it works

```
page → GET /set?brightness=128 → web interface server → led.sync_brightness(128, origin)
     → led applies it → LedListener::on_brightness → "B128" to every page on ws://<ip>:81/
$led set_mode 5 (CLI) → led → on_color, on_mode → "C…", "M5" to every page → the page reloads /modes
```

- **`LedWeb` class** (`src/LedWeb/LedWeb.h`) — a `xewe::Module` with id `led_web`; requires `led` and `web_interface`; can be disabled (asks on the first boot). At begin it registers its routes on `web_interface.get_server()`, takes `/` with `set_root`, starts the WebSocket server and adds itself to `led.listeners`. `loop()` serves the WebSocket clients and the heartbeat.
- **`LedWebApi.h`** — route table, CORS headers, `/set`, `/state`, `/modes`, `/name` handlers, the status page address, the frame formats and the page sessions (client count, heartbeat, `F` on connect, closing every page) (no server, no Arduino).
- When the module is disabled nothing is registered: `/` is the web interface's console again and the other paths are unknown to the server.

### Commands

**Prefix:** `$led_web` · requires Led and Web Interface · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`status`** | `Led Web module enabled\|disabled`, then `Page: http://<ip>/` (`http://<ip>:<port>/` when the web interface's `port` setting is not 80), `WebSocket Port: 81`, `WebSocket Clients: <n>` | `$led_web status` |
| **`reset`** | Closes every WebSocket connection, then the core reset (the module stores nothing of its own) and restarts | `$led_web reset` |
| **`enable`** / **`disable`** | Core enable/disable (disable asks for confirmation); both restart | `$led_web disable` |

### HTTP API

All routes are `GET`. Numbers are read like Arduino `toInt()` (leading digits, `abc` is 0) and narrowed like the `$led` commands (`300` → 44 for brightness).

| Route | Query | Response |
|---|---|---|
| `/` | – | `200 text/html`, the page |
| `/set` | the first present of: `color=RRGGBB` (six hex digits, optional `#`), `brightness=<0-255>`, `state=<n>` (on only for `1`), `mode_id=<n>`, `reset_params` (any value: the current mode's parameters back to their defaults), `param=<key>&val=<n>` | `200 text/plain OK` (also with no or unknown arguments); `400 text/plain Invalid color` for a malformed colour (nothing changes) |
| `/state` | – | `200 text/plain F<RRGGBB>,<brightness>,<0\|1>,<mode id>` (brightness is the last non-zero one) |
| `/modes` | – | `200 application/json`, `led.get_all_modes_json()`: `[{"id":0,"name":"Solid","params":[{"key":..,"display_name":..,"min":..,"max":..,"step":..,"value":..,"type":"a\|b"}]},...]` |
| `/name` | – | `200 text/plain`, the device name |
| `/index.css`, `/static/index.css`, `/%7B%7B%20url_for('static',%20filename='index.css')%20%7D%7D` | – | `200 text/css` |
| `/index.js`, `/static/index.js`, `/%7B%7B%20url_for('static',%20filename='index.js')%20%7D%7D` | – | `200 application/javascript` |

Other paths, and `OPTIONS` preflights, are answered by the web interface's server (see Known
issues). The page's "Set Up Schedule" button opens `/schedule`, which this module does not serve; a
project that has a schedule page registers it there.

### WebSocket frames (server → page, text)

| Frame | Format | Sent on |
|---|---|---|
| `H` | literal | every second while a page is connected; the page shows Offline after 2.2 s without one and reloads itself |
| `C` | `C%02X%02X%02X` | `on_color` |
| `B` | `B%u` | `on_brightness` |
| `S` | `S0` / `S1` | `on_state` |
| `M` | `M%u` | `on_mode` |
| `F` | `F%02X%02X%02X,%u,%u,%u` (rgb, brightness, state, mode) | `on_all` (`$led reset`) and to every page when a page connects |

Text sent by a page is ignored. The length is not shown on the page, so `on_length` sends nothing.
The page also understands `P<key>:<value>`; the led module has no parameter event, so it is not sent.

### Listeners

Implements `LedListener` (registered on `led.listeners` at begin). It sends every event as a frame,
including events caused by this module's own `/set` (origin is not filtered), in the order the led
reports them: a mode change gives `C` (the mode's colour) then `M`, `$led reset` gives `C` then `F`.
The frames go out from inside the callback, so a page's `/set` echo is sent before the HTTP answer.
It registers no listener set of its own.

### Requirements

| | |
|---|---|
| Modules | [led](../led), [web-interface](../web-interface) (and through it [wifi](../wifi)) |
| Libraries | WebSockets (the xewe-labs fork of arduinoWebSockets, pinned to a commit in `libraries.toml`), XeWeCore >=3.1.0,<4.0.0 |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Settings and NVS keys

None of its own (namespace `led_web` holds only the core's `is_enabled` and `not_first_boot`).

### Build defines (`src/LedWeb/Config.h`)

The module has no configurable values: its `Config.h` has both sections empty. The WebSocket port (81) and the heartbeat interval (1000 ms) are constants in `LedWeb.h`, matched to the page script, which connects to port 81 and shows Offline after 2.2 s without a heartbeat.

### Known issues

- No authentication: anyone on the local network can change the LEDs (as with the web interface's `/cmd`).
- At most 5 WebSocket connections at once (the library's `WEBSOCKETS_SERVER_CLIENT_MAX`).
- About 46-49 KB of flash (the page is 23.5 KB of it) and 1.5 KB of RAM.
- While enabled, the page owns `/`: `$web_interface set root` and a project's own
  `web_interface.set_root` are overridden. The console stays at `/console`.
- The page's "Set Up Schedule" button opens `/schedule`, which answers 404 until the project
  registers a schedule page on the web interface's server.
- `status` prints the HTTP port from the web interface's `port` setting; after
  `$web_interface set port` it shows the new port before the restart that applies it.

### Tests

`tests/board/test_led-web.py` (board tests) and `tests/unit/test_led-web.py` (unit tests, developer machine), run through an xewe-os harness (see the repo [README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module led-web              # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module led-web --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module led-web --unit-only  # unit tests only, no build
```

- Unit: source checks (properties, the WebSockets pin, GPL headers, the route table and the `/set` argument order, route registration, the WebSocket glue), `test_led_web_api.cpp` (g++: `/set` for valid and malformed input, `/state`, `/modes`, `/name`, every frame, the listener including its own-origin echo and the frame order, the page sessions, the status page address, CORS headers), the served bytes of the three page assets (SHA-256, and equal to the files at xewe-led-os commit 91bd344 when that repository is next to the modules checkout or in `XEWE_LED_OS_REPO`; the route table, argument order and content types are compared with its `WebInterface.cpp` the same way), `node --check` of the page script, and the page's links and fetches against the route table. The C++ test needs XeWeCore 3.1.0's `src/` (the hex parser in `Utils/Color.h`) and the led module's `src/Led`.
- Board: `test_compiles` runs without a board; `test_status` and `test_status_reports_page_and_websocket` need a board; `test_http_routes`, `test_http_cors_and_served_bytes` (CORS headers and the script's SHA-256 on the wire, 404 for an unknown path) and `test_set_is_echoed_over_websocket` (echo, CLI change, heartbeat, `C` then `M` on a mode change, `F` to every page when a second one connects) also need network access to the board (`XEWE_TEST_LED_WEB_HTTP=1`); `test_disable_enable_across_restart` (last) answers the disable confirmation and enables the module again.
- Hardware precondition: a provisioned board (the "enable Led Web module?" prompt answered yes, the led wizard completed), WiFi connected.
