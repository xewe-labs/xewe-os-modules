# led-web — LED control page at /led

XeWe OS module · created 2026-10-09 (ported from the xewe-led-os 2.3.x `WebInterface` LED page) · Solo: Max Dokukin · Status: Draft (0.1.0)

## Overview

Serves the 2.3.x LED page (mode select, mode parameters, colour, brightness, on/off) at `/led` on the
[web-interface](../web-interface) module's HTTP server, plus a small JSON API under `/led/api`. Every
write route calls the public setter of [led-strip](../led-strip) or [led-modes](../led-modes) that the
matching `$led ...` / `$led_modes ...` command calls, so the web page and the CLI share one code path
(range checks, clamping, NVS persistence, cross-fade). Optional: select it only on boards with WiFi. A
module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on
[XeWeCore](https://github.com/xewe-labs/xewe-os-core).

## Highlights

- The 2.3.x page, CSS and JS as header strings (`R"rawliteral(...)"`, unminified as they were), with minimal edits marked `led-web:` in `index_js.h`
- No first-boot questions (LM5): `requires_init_setup=false`, `can_be_disabled=false`, the same choice as web-interface
- No extra libraries: no WebSockets, no ArduinoJson (JSON is built by hand)
- Page sync by **server-sent events**: `GET /led/api/events` pushes one `state` event per change (led-web is a led-strip `LedListener`); the page polls `GET /led/api/state` every 2 s only while the stream is closed (see "Sync")

## Routes

`/` belongs to web-interface, so everything lives under `/led` (the cooling pad's page uses `/pad`
the same way). POST routes take form fields (`application/x-www-form-urlencoded`) or query arguments,
answer `200` with the state JSON, or `400` with `{"ok":false,"error":"..."}`. They also accept
`client` (the page sends its random id): the change is then not pushed on that page's own event
stream, since the POST answer already carries the new state.

| Method | Path | Fields | Same code path as | Answer |
|---|---|---|---|---|
| `GET` | `/led` | | | the page (`index_html.h`) |
| `GET` | `/led/index.css` | | | `index_css.h` |
| `GET` | `/led/index.js` | | | `index_js.h` |
| `GET` | `/led/api/state` | | `$led status`, `$led_modes status` | state JSON |
| `GET` | `/led/api/modes` | | `$led_modes list` | modes JSON (2.3.x `/modes` shape) |
| `GET` | `/led/api/events` | optional `client` (the page's id) | listener hook | `text/event-stream`: `state` events, `: keep-alive` every 15 s; `503` when 2 streams are open |
| `POST` | `/led/api/brightness` | `value` 0..255 | `$led brightness` → `LedStrip::set_brightness` | state JSON |
| `POST` | `/led/api/power` | `value` 0 or 1 | `$led on` / `$led off` → `LedStrip::set_state` | state JSON |
| `POST` | `/led/api/mode` | `value` mode id | `$led_modes set` → `LedModes::set_mode` | state JSON |
| `POST` | `/led/api/param` | `key`, `value`, optional `mode` (default: current) | `$led_modes param` → `LedModes::set_param` (clamps) | state JSON |
| `POST` | `/led/api/color` | `value` `rrggbb` | `$led_modes color` → `LedModes::set_color` | state JSON |
| `POST` | `/led/api/reset_params` | | `$led_modes reset_params` → `LedModes::reset_params` (current mode, one cross-fade) | state JSON |

State JSON:

```json
{"name":"XeWe LED","on":true,"brightness":128,"mode":0,"mode_name":"Solid","color":"FF0000",
 "params":{"hue":0,"sat":255},"length":60,"fps":50}
```

`color` is `LedModes::get_color()`, the `Color:` line of `$led_modes status` (2.3.x
`Mode::get_rgb`: the base colour, white for Rainbow). Modes JSON: `[{"id":0,"name":"Solid","params":[{"key":"hue","display_name":"Hue","min":0,"max":255,"step":1,"default_value":0,"value":0,"type":"b"},...]},...]`.

```sh
curl http://<ip>/led/api/state
curl -X POST -d value=200 http://<ip>/led/api/brightness
curl -X POST -d 'key=speed&value=7' http://<ip>/led/api/param
curl -X POST -d value=ff8000 http://<ip>/led/api/color
curl -N http://<ip>/led/api/events      # event: state / data: {...} per change
```

## Commands

| Command | Does |
|---|---|
| `$led_web status` | `Led Web module enabled`, then page URL, routes attached, sync (open streams of 2, events sent, streams refused), API request/rejection counters |
| `$led_web url` | `Led Web: http://<ip>/led` (adds `(WiFi not connected)` without WiFi) |
| `$led_web reset` | generic; the module stores nothing in NVS |

## Sync

2.3.x pushed every change over a WebSocket on port 81 (`links2004/WebSockets`, a fork, MIGRATION.md
LM8) through the controller's `sync_*` fan-out. v2 pushes with **server-sent events** on the
web-interface server, no library:

- led-web registers with `led_strip.add_listener(this)`. led-strip's `on_brightness`/`on_state` and
  led-modes' `on_mode`/`on_color`/`on_param` only mark every open stream "pending" (callbacks run in the
  main loop, never in the render task). Changes whose `origin` is led-web itself are skipped there;
  the POST handler instead marks the other streams (not the one with the poster's `client` id).
- `LedWeb::loop()` sends one `event: state` with the state JSON per pending stream (several changes
  between two loops collapse into one event: `reset_params` gives one event, not one per parameter),
  and `: keep-alive` every 15 s, which also detects closed sockets.
- At most `LED_WEB_SSE_CLIENTS` (2) streams; a third gets `503` + `Retry-After: 15` and that page polls.

Why it does not block `handleClient()`: arduino-esp32 3.3.12 `WebServer` handles one client at a time.
`NetworkClient::setSSE(true)` would keep the server in `HC_WAIT_CLOSE` on that socket and stop it
accepting anything else, so led-web does not use it. The handler copies `server->client()` (the copy
shares the socket through a `shared_ptr`), writes the `200 text/event-stream` head itself, and
returns without `send()`; `handleClient()` then drops its own reference and goes on serving, while
the copy keeps the connection open for `loop()` to write. Limit: writes are synchronous; a peer that
stops reading without closing could stall the main loop for up to ~10 s (lwIP send buffer full, then
`NetworkClient::write` retries select() 10 × 1 s) before the short write drops the stream.

The page (`index_js.h`) opens `EventSource('/led/api/events?client=<id>')` and feeds each `state`
event to the unchanged 2.3.x message handler as `F`/`P` wire messages. When the stream errors or
closes (restart, WiFi drop, `503`), it polls `GET /led/api/state` every 2 s (`POLL_MS`, equal to
`POLL_INTERVAL_S` in `LedWeb.cpp`, host-tested) and tries the stream again after 30 s. An open stream
counts as the heartbeat for the online dot (keep-alive comments are not visible to JavaScript). A
pushed or polled state is ignored for 1.5 s after a local change so it does not jump a slider the
user is moving.

## Add it to a project

```toml
# xewe.lock
[modules]
selected = [..., "web-interface", "led-strip", "led-modes", "led-web"]
```

Then `./setup.sh` (or `xewe modules select led-web`), build and flash. `depends_modules` pulls in
web-interface (and wifi), led-strip and led-modes; the lock must list FastLED for led-strip. The
generated `Modules.h` line is `LedWeb led_web(os, web_interface, led_strip, led_modes);`. Open
`$led_web url` in a browser on the same network.

## Not ported

- The `/schedule` page and its six JS/CSS files (2.3.x `schedule_*`): they drive the scheduler module and belong in a scheduler web module; the page's "Set Up Schedule" button is removed.
- The `/name` route: the device name is in the state JSON.
- The Jinja fallback routes (`/%7B%7B url_for(...)`) and the global CORS/404 handler: `/` and 404 belong to web-interface. JSON answers carry `Access-Control-Allow-Origin: *`.

## Testing

`tests/test_led-web.py`. The tools have no HTTP client and the CLI cannot issue a GET, so the routes
are tested on the host: the route table is parsed from `LedWeb.cpp` and compared with the expected
set, with every URL the page's HTML and JS request, with this README, and each POST handler is
checked to call its module setter with `origin = this` and to push to the other streams;
`test_sse_contract` checks the stream (content type, client copy instead of `setSSE`, 2 streams,
15 s keep-alive, one `state` event, listener registration), `test_page_falls_back_to_polling` the
page side and `test_js_syntax_node` runs `node --check` on the page JS. On a board: `test_status` and
`test_url_command` (serial). The page's behaviour (sliders, mode switch, events, fallback polling,
colour) needs a browser and is not automated.

## Files

| File | Holds |
|---|---|
| `src/LedWeb/LedWeb.h`, `LedWeb.cpp` | module, routes, JSON, event streams (listener) |
| `src/LedWeb/index_html.h`, `index_css.h`, `index_js.h` | 2.3.x page assets |
| `tests/test_led-web.py` | contract tests + host route checks |
