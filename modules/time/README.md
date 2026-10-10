# time — network time and automatic timezone detection

XeWe OS module · created 2026-09-15 (split out of xewe-os, where it was developed from 2026-07) · Solo: Max Dokukin · Status: Active (0.3.0)

## Overview

NTP time sync and timezone handling. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). On first boot it
detects the timezone from the public IP address by racing three geo-IP services in parallel, asks
the user to confirm the resulting local time, and stores the offset in NVS; if detection fails it
asks for the offset by hand. On every boot it syncs the clock over SNTP and applies the stored
offset. The Scheduler module uses it as its clock.

## Highlights

- Timezone detection races three HTTP endpoints (ipwho.is, ipwhois.app, ip2location.io) in three FreeRTOS tasks; the first valid answer wins through an atomic compare-and-swap, the others are aborted and joined (`Time::begin_routines_init`, `fetch_tz_task`)
- Waits at most 6 s (30 × 200 ms) for a winner, then falls back to manual entry (`GMT±HH:MM`)
- SNTP with three servers: `pool.ntp.org`, `time.google.com`, `time.cloudflare.com`
- `GMT±HH:MM` offsets are converted to POSIX `TZ` strings (sign inverted) and applied with `setenv`/`tzset`

## How it works

```
first boot:  SNTP init → 3 tasks × GET geo-IP → first valid offset → "Is your time …?" → store tz_gmt_str
             (no answer in 6 s or "no") → prompt GMT±HH:MM → store
later boots: apply stored offset → SNTP sync wait (≤ 50 × 200 ms) → print current time
```

- **`Time` class** (`src/Time/`) — a `xewe::Module` with id `time`; requires Wifi, asks for setup on first boot, can be disabled.
- **API for other modules** — `get_current_time()` (`tm`), `get_current_time_str()`, `print_current_time()`.
- **Race context** — `TzRace` holds an abort flag, a claimed flag, the result buffer, a binary "winner" semaphore and a counting "done" semaphore used to join the three workers before the context goes out of scope.

### Commands

**Prefix:** `$time` · requires Wifi · asks for the timezone on first boot · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`set_zone`** | Set the timezone offset. | `$time set_zone GMT-08:00` |
| **`set`** / **`get`** / **`schema`** | Settings table (core 2.1): `tz_gmt_str` (normalised, applied at once; an invalid value is put back). `set_zone` is its alias. | `$time set tz_gmt_str GMT-8` |
| **`fetch`** | Sync the current time from the network. | `$time fetch` |

### Requirements

| | |
|---|---|
| Modules | [wifi](../wifi) |
| Libraries | XeWeCore >=2.1.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/board/test_time.py` (board tests) and `tests/unit/test_time.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module time              # lock chip
build/tools/.venv/bin/python -m xewe test --module time --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module time --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$time status`) and `test_fetch_syncs_time` (`$time fetch`) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board with WiFi connected and internet access (NTP). A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts (`get_yn` waits forever), so provision it by hand first
  (CONTRACT.md section 7.2).
