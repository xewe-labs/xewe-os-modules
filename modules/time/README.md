# time

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

- `src/Time/` holds the `Time` class, a `xewe::Module` with id `time`; requires Wifi, asks for setup on first boot, can be disabled.
- Other modules call `get_current_time()` (`tm`), `get_current_time_str()`, `print_current_time()`.
- The race context `TzRace` holds an abort flag, a claimed flag, the result buffer, a binary "winner" semaphore and a counting "done" semaphore used to join the three workers before the context goes out of scope.

## Commands

Commands start with `$time`. The module requires Wifi, asks for the timezone on first boot and can be disabled.

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`set_zone`** | Set the timezone offset. | `$time set_zone GMT-08:00` |
| **`set`** / **`get`** / **`schema`** | Settings table: `tz_gmt_str` (normalised, applied at once; an invalid value is put back). `set_zone` is its alias. | `$time set tz_gmt_str GMT-8` |
| **`fetch`** | Sync the current time from the network. | `$time fetch` |

## Requirements

| | |
|---|---|
| Modules | [wifi](../wifi) |
| Libraries | XeWeCore >=3.1.0,<4.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

## Settings and NVS keys

| Key (namespace `time`) | Type | Default | Meaning |
|---|---|---|---|
| `tz_gmt_str` | str ≤ 9 | `GMT+00:00` | timezone offset `GMT±HH:MM`; `GMT-8` is normalised to `GMT-08:00` and applied at once; an unparsable value is put back |

## Build defines (`src/Time/Config.h`)

All names start with `XEWE_TIME_`. `./setup.sh` (and `xewe modules select`) appends the module's
`src/Time/Config.h` as one marked block to the project's `Config.h`, every entry commented out, following the scheme in
[XeWeCore's `doc/CONFIG.md`](https://github.com/xewe-labs/xewe-os-core/blob/main/doc/CONFIG.md). The module has one default runtime value, asked at first boot, written there as a comment and a commented-out line:

The file frames its default runtime values with `/* ---- start default runtime values ---- */` ... `/* ---- end default runtime values ---- */`; its compile time defined constants section (`/* ---- start compile time defined constants ---- */` ... `/* ---- end compile time defined constants ---- */`) is empty.

```c
/* Timezone offset (time/tz_gmt_str), string literal GMT+HH:MM; detected, asked at first boot */
// #define XEWE_TIME_TZ_GMT_STR "GMT+00:00"
```

Uncomment it and set the offset to pre-answer the first boot, or write
`#define XEWE_TIME_TZ_GMT_STR INJECTED_FROM_DOTENV` to take it from the dotenv file. A pre-answer
that parses as `GMT±HH:MM` skips the detection and the confirmation; the clock still syncs over
SNTP. One that does not parse falls back to the typed input. A `xewe build --define XEWE_TIME_TZ_GMT_STR=...` wins over the project's `Config.h`.
The module has no compile time defined constants.

## Tests

`tests/board/test_time.py` (board tests) and `tests/unit/test_time.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module time              # the manifest's chip
build/tools/.venv/bin/python -m xewe test --module time --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module time --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$time status`), `test_fetch_syncs_time` (`$time fetch`) and `test_settings_table` (`$time schema`) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board with WiFi connected and internet access (NTP). A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts, so provision it first (`xewe provision`).
