# web-interface — send CLI commands to the board over HTTP

## Overview

HTTP page and command endpoint for other devices on the network. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). It starts an HTTP server on
the `port` setting (default 80) that serves a small dark-themed page with one input box, and accepts CLI commands from
any device on the local network at `GET /cmd?c=<command>`. Each command is echoed to the serial
console and run through the same command line as serial input, so every installed module can be
driven from a browser, a script or another microcontroller board.

## Highlights

- Routes: `GET /console` (embedded HTML page stored in flash with `PROGMEM`), `GET /` (the console unless a project takes it, see below) and `GET /cmd?c=<command>` → `200 OK` or `400 Empty Command`
- Catch-all for every request no route answers: the three CORS headers, then `204` with an empty body to an `OPTIONS` preflight, else `404 text/plain "404: Not Found"`
- The server listens on every boot of the enabled module, including its first-boot setup boot (the first boot of the device, the boot after `$web_interface reset`, or the first boot after adding the module)
- Settings table: `port` (u16, default 80, takes effect after a restart) and `root` (str ≤ 31: `GET /` redirects there, e.g. `/pad`; empty = the console)
- Root hook: `web_interface.set_root(handler)` (RAM only, call from `setup()`) lets a project page own `/`; it wins over the `root` setting. `redirect_root("/pad")` is the persisted form
- The page sends commands with `fetch('/cmd?c=' + encodeURIComponent(...))` and flashes "Command Sent" / "Error Sending" / "Connection Error"
- `status` adds `Uptime: <d>d HH:MM:SS` and `Memory usage: <pct>% (<used> / <total> bytes)`
- Requires the Wifi module and prints the page URL (`http://<local ip>`, plus `:<port>` when it is not 80) once the server starts, on every boot

## How it works

```
browser / curl → GET /cmd?c=$pins gpio_toggle 8 → WebServer → os.cli.execute(command) → module callback
```

- **`WebInterface` class** (`src/WebInterface/`) — a `xewe::Module` with id `web_interface`; requires Wifi (`add_requirement(wifi)`), cannot be disabled; `loop()` calls `handleClient()`.
- **Boot** — `begin_routines_required` registers `/`, `/console`, `/cmd` and the catch-all (`onNotFound`); `begin_routines_common` starts the server on the `port` setting and prints the URL. The core runs both on every boot of the enabled module (required → init or regular → common), so the server also answers on the module's first-boot setup boot. Modules declared after this one begin later in the same boot, so routes they add with `get_server().on(...)` answer from the first request.
- **`WebInterfaceApi.h`** — the catch-all's answers without the server (`CORS_HEADERS`, `PREFLIGHT`, `NOT_FOUND`, `answer(is_options)`); no includes, so the host test checks the bytes the firmware sends.
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
| any path no route answers | OPTIONS | `204`, empty body, `Content-Type: text/html` (the server's default), plus the CORS headers |
| any path no route answers | any other | `404 text/plain` `404: Not Found`, plus the CORS headers |

The catch-all sends these headers before every answer:

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE, PUT
Access-Control-Allow-Headers: Content-Type
```

The routes answer `GET` only, so a preflight to `/`, `/console` or `/cmd` also reaches the
catch-all and gets the `204`.

**Replacing the catch-all.** The esp32 `WebServer` keeps a single not-found handler:
`onNotFound(fn)` assigns it (`_notFoundHandler = fn`), so the last call wins. A module declared
after `web_interface` that calls `web_interface.get_server().onNotFound(...)` in one of its
begin routines replaces this catch-all for the rest of that boot; an Alexa integration that
installs its own handler when it is enabled does exactly that, and the catch-all is back on the
next boot with it disabled. Calling `get_server().begin()` again from another module is harmless:
it closes and reopens the listener on the same port.

```bash
curl "http://192.168.1.50/cmd?c=%24pins%20gpio_toggle%208"
```

There is no authentication: anyone on the same network can send any command, including
`$system` commands. Use it on trusted networks only.

### Requirements

| | |
|---|---|
| Modules | [wifi](../wifi) |
| Libraries | XeWeCore >=3.1.0,<4.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Settings and NVS keys

| Key (namespace `web_interface`) | Type | Default | Meaning |
|---|---|---|---|
| `port` | u16 1–65535 | 80 | HTTP port; applies after a restart (RESTART row) |
| `root` | str ≤ 31 | empty | `GET /` redirects here (e.g. `/pad`); empty = the console page |

### Build defines (`src/WebInterface/Config.h`)

All names start with `XEWE_WEB_INTERFACE_` (the slug `web-interface` with `-` written as `_`). They
are the defaults of the two settings rows, the runtime-defaults section of the scheme in [XeWeCore's `doc/config.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/config.md): a
line in the project's `Config.h` (`xewe modules generate` lists them there commented out, for example
`// #define XEWE_WEB_INTERFACE_PORT 80`; uncomment one and set the value) or `xewe build --define
NAME=VALUE` changes the default a fresh device starts with, and
`$web_interface set` changes the stored value. The module has no first-boot values and no
compile-time constants.

| Define | Default | Setting | Meaning |
|---|---|---|---|
| `XEWE_WEB_INTERFACE_PORT` | 80 | `port` | HTTP port, 1 to 65535; takes effect after a restart |
| `XEWE_WEB_INTERFACE_ROOT` | `""` | `root` | path `GET /` redirects to, up to 31 characters; empty = the console page |

### Tests

`tests/board/test_web-interface.py` (board tests) and `tests/unit/test_web-interface.py` (unit
tests on the developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module web-interface              # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module web-interface --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module web-interface --unit-only  # unit tests only, no build
```

Unit tests:

- `test_properties_match_source`: `module.properties` matches the C++ source.
- `test_routes_and_catch_all_in_the_required_routine`, `test_server_starts_in_the_common_routine`, `test_catch_all_sends_cors_then_the_answer`: the hook placement and the catch-all in the source text (the whole body of the common routine and of the handler, so the CORS headers go out before every answer, not only the preflight).
- `test_api_values`, `test_api_header_stays_pure`, `test_api_unit_gpp`: the CORS headers and the `204`/`404` answers of `WebInterfaceApi.h`, compiled with `g++ -std=c++17 -Wall -Wextra -Werror` (`tests/unit/test_web_interface_api.cpp`; skips without g++).
- `test_console_page_bytes`: size and SHA-256 of the console page.
- `test_catch_all_equals_v230`: the headers, the answers and the hook placement equal the catch-all of `WebInterface.cpp` at xewe-led-os commit 91bd344, read with `git show` from `XEWE_LED_OS_REPO`, else `xewe-led-os` next to the modules checkout; without it the test skips with a warning.

Board tests:

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$web_interface status`), `test_status_reports_server` (the `Uptime` and `Memory usage` lines of `$web_interface status`) and `test_settings_table` (`$web_interface schema`) need a board; without one they report
  "compiled, not run".
- With `XEWE_TEST_WEB_INTERFACE_HTTP=1` (this machine reaches the board; the address comes from `$wifi status` and `$web_interface get port`):
  - `test_http_routes`: `/` answers (`200`, or `302` with the `root` setting), `/console` serves the page, `/cmd` without `c` → `400 Empty Command`.
  - `test_http_catch_all`: `GET /nope` → `404 text/plain "404: Not Found"`, `OPTIONS` to `/nope`, `/`, `/console`, `/cmd` → `204`, empty, `text/html`; the CORS headers on each. Skips when `$system status` lists an enabled Alexa module (its handler replaces the catch-all).
  - `test_restart_keeps_serving`: `$system restart`, then the routes answer on the next boot.
  - `test_reset_serves_on_the_next_boot`, also with `XEWE_TEST_WEB_INTERFACE_RESET=1`, last: `$web_interface reset` (no question asked: the module cannot be disabled), then on the very next boot, the module's setup boot, the server is on port 80 and `/`, `/console` and `/cmd` answer, and, when present, led-web's `/state`; the catch-all too when Alexa is not enabled (the `test_http_catch_all` checks). `port` and `root` are restored afterwards (with a restart when the port was not 80).
- Hardware precondition: a provisioned board with WiFi connected. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).

```sh
XEWE_TEST_WEB_INTERFACE_HTTP=1 build/tools/.venv/bin/python -m xewe test --module web-interface
XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1 build/tools/.venv/bin/python -m xewe test --module web-interface
```
