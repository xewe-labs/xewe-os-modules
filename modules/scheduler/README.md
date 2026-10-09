# scheduler — run stored commands on a weekly schedule

XeWe OS module · created 2026-09-15 (split out of xewe-os, where it was developed from 2026-07) · Solo: Max Dokukin · Status: Active (0.2.0)

## Overview

Runs stored commands on a weekly schedule. A module for [XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). A schedule block
has a start and end time (minutes from midnight), a weekday, a display colour and one or more CLI
commands; at the start minute of a matching day the commands run through the command line, so a
schedule can drive any installed module. Blocks are stored in NVS as one structured record and
reloaded on boot.

## Highlights

- Schedule blocks are `FlexData` structs (`id`, `start_time`, `end_time`, `day`, `displayed_color`, `commands`) persisted with `write_flex`/`read_flex` under the NVS key `schedules`
- Evaluated once per minute from `loop()`; nothing runs until the clock is synced (year ≥ 1970)
- Weekday mapping converts `tm_wday` (0 = Sunday) to the module's 0 = Monday … 6 = Sunday
- Input validation with range checks: start/end 0–1439, day 0–6, colour exactly 6 characters; several commands separated by `|`

## How it works

```
$schedule add 480 1020 1 FF0000 "$pins gpio_write 8 1" → validate → ScheduleBlock{id = max+1} → NVS
loop (once per minute): Time.get_current_time() → day + minute match start_time → os.cli.execute(each command)
```

- **`Scheduler` class** (`src/Scheduler/`) — a `xewe::Module` with id `schedule`; requires Time (and through it Wifi); cannot be disabled; the schedule becomes active after the reboot that follows first setup.
- **API** — `add(...)`, `remove(id)`, `get_all_json()`, `load_from_nvs()`, `save_to_nvs()`; `status` prints all blocks as JSON.
- `end_time` and `displayed_color` are stored for interfaces that draw the schedule; only the start minute triggers commands.

### Commands

**Prefix:** `$schedule` · requires Time

Times are minutes from midnight (0-1439), days are 0 (Monday) to 6 (Sunday); several commands are separated by `|`.

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`add`** | Add a schedule: `<start> <end> <day> <RRGGBB> "<cmd1\|cmd2>"`. | `$schedule add 480 1020 1 FF0000 "$pins gpio_write 8 1"` |
| **`remove`** | Remove a schedule by id. | `$schedule remove 1` |

### Requirements

| | |
|---|---|
| Modules | [time](../time) (and, through it, [wifi](../wifi)) |
| Libraries | XeWeCore >=2.0.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/test_scheduler.py`, run through an xewe-os harness (see the repo [README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module scheduler              # lock chip
build/tools/.venv/bin/python -m xewe test --module scheduler --all-chips  # c3, c6, s3
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$schedule status`) and `test_add_then_remove` (`$schedule add`, then `$schedule remove` with the new id) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board with Time and Wifi enabled, past the reboot that follows first setup. The test adds one block and removes it again. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts (`get_yn` waits forever), so provision it by hand first
  (CONTRACT.md section 7.2).
