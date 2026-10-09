# buttons — bind CLI commands to physical buttons

XeWe OS module · created 2026-09-15 (split out of xewe-os, where it was developed from 2026-01) · Solo: Max Dokukin · Status: Active (0.2.0)

## Overview

Binds commands to physical buttons with software debouncing. A module for
[XeWe OS](https://github.com/xewe-labs/xewe-os), built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core). Each mapping ties a GPIO pin,
an input mode (pull-up or pull-down), a trigger event (press, release or change) and a debounce
interval to one CLI command, so a button can do anything the command line can — reboot the board,
toggle a pin, run a schedule. Mappings are stored in NVS and restored on boot without any setup
prompt.

## Highlights

- Per-button software debouncing: a state change is accepted only after it has been stable for the mapping's interval (default 50 ms) (`Buttons::loop`)
- Three trigger events (`on_press`, `on_release`, `on_change`) and two input modes (`pullup`, `pulldown`); "pressed" is derived from the input mode
- Mappings are `FlexData` records persisted as one NVS entry; runtime-only debounce state is not stored
- `$buttons status` prints a table of active mappings (ID, pin, command, debounce, type, event)

## How it works

```
$buttons add 9 "$system reboot" pullup on_press 50 → pinMode(INPUT_PULLUP) → ButtonData{id = max+1} → NVS
loop: digitalRead → stable for debounce_ms? → state changed? → event matches? → os.cli.execute(command)
```

- **`Buttons` class** (`src/Buttons/`) — a `xewe::Module` with id `buttons`; no first-boot setup; can be disabled.
- **API** — `add(pin, command, type, event, debounce)`, `remove(id)`, `load_from_nvs()`, `save_to_nvs()`.

### Commands

**Prefix:** `$buttons` · can be disabled

| Command | Description | Sample Usage |
| :--- | :--- | :--- |
| **`add`** | Add a mapping: `<pin> "<cmd>" <pullup\|pulldown> <on_press\|on_release\|on_change> <debounce_ms>`. | `$buttons add 9 "$system reboot" pullup on_press 50` |
| **`remove`** | Remove a mapping by its id (see `$buttons status`). | `$buttons remove 0` |

### Requirements

| | |
|---|---|
| Modules | none |
| Libraries | XeWeCore >=2.0.0,<3.0.0 (Arduino libraries bundled with the esp32 core are not listed) |
| Boards | ESP32-C3, ESP32-C6, ESP32-S3 (arduino-esp32 3.x) |

Metadata and dependencies are declared in [`module.properties`](module.properties).

### Tests

`tests/board/test_buttons.py` (board tests) and `tests/unit/test_buttons.py` (unit test: `module.properties`
matches the C++ source; developer machine, no build), run through an xewe-os harness (see the repo
[README](../../README.md)):

```sh
build/tools/.venv/bin/python -m xewe test --module buttons              # lock chip
build/tools/.venv/bin/python -m xewe test --module buttons --all-chips  # c3, c6, s3
build/tools/.venv/bin/python -m xewe test --module buttons --unit-only  # unit tests only, no build
```

- `test_compiles` builds the harness firmware with this module selected; it runs without a board.
- `test_status` (`$buttons status`) and `test_add_then_remove` (`$buttons add`, then `$buttons remove` with the new id) need a board; without one they report
  "compiled, not run".
- Hardware precondition: a provisioned board. The add/remove test needs a free GPIO in `XEWE_TEST_BUTTONS_PIN` and is skipped without it; it removes the mapping it adds. A freshly erased board stops at the first-boot
  "Would you like to enable ...?" prompts (`get_yn` waits forever), so provision it by hand first
  (CONTRACT.md section 7.2).
