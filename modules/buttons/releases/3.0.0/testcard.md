# buttons 3.0.0 test card

What was tested, where, on which commits, and what was not. Host tests are the module's
`tests/unit/` (pytest, run on 2026-10-11). Board tests are `tests/board/test_buttons.py` from the v3 board
campaign (docs/phase4/reports/testing-v3.md): TV4 module pass 1 (`-k "not survive_restart"`,
2026-10-10) and TV5 pass 2 (`-k "survive_restart"`), which had no test of this module.

| Component or command | Host unit tests | Board tests | Not covered |
|---|---|---|---|
| `add`, `remove` (pin claim, NVS save) | none | `test_add_then_remove` pass (needs `XEWE_TEST_BUTTONS_PIN`; set to 14 on the rig) | a real button press: the test adds and removes a mapping but never drives the pin |
| `status` | none | `test_status` pass | none known |
| `schema`, `schema_extra` | none | no test; TV5 capture `$buttons schema` answered `{"end":"buttons","count":0}` | asserted rows with mappings present |
| Debounce and events in `loop()` | none | none | not tested on a board (no button on the rig) |
| Persistence across a restart | none | core `test_button_and_schedule_survive_restart` pass (TV5) | none known |
| Build with the module selected | none | `test_compiles` pass | none |
| `module.properties` matches the source | `test_properties_match_source` pass |  |  |
| Totals | **1 passed, 0 failed** | **3/3 passed**, 0 skipped (TV4 pass 1; pass 2: none) | |

Compile: a project from the template with all six modules and core 3.0.0 builds for ESP32-C3 and
S3 with 0 warnings (after the format and the version bump, 2026-10-11).

## Environment

- Board: one ESP32-S3 dev board on USB, passed through into the Linux VM; a provisioned board, NVS
  kept. The module sessions ran a project with every module of the time (these six plus the
  parked fan, led and mlx90614) through the xewe-os-tools pytest plugin; image without
  `XEWE_TESTING`, 1288768 B (61 %).
- Toolchain: arduino-cli 1.5.1, esp32 core 3.3.12.
- Host: Linux aarch64, Python 3.14.7, xewe-os-tools 3.0.0.

## Commits

- Board (TV4, TV5): modules c6ace5d, core d14d490, tools ff7a7c8, template c98d2a5.
- Host tests and compile: modules 06394d1 plus the 3.0.0 work, format and prepare changes (the tree
  tagged buttons/3.0.0); tools 3.0.0 (3742334); core 3.0.0 (src as tagged); template 3756ed6.
- History of `modules/buttons/` since v0.2.0:
  - df801b4: docs: paths for the new build layout
  - 3032fc2: tests/unit and tests/board in every module
  - ac2bdaa: core 2.1 adoption: settings tables, pin claims
  - 78f0088: docs pass: module readmes with settings and nvs tables
  - 4c4fe65: smaller modules, consistent replies, all modules 0.3.0
  - 423987b: module readmes checked against the sources

## Not re-run since the board campaign

The board tests have **not** been re-run after c6ace5d: not on 06394d1, the 3.0.0 work commit, the
format commit, nor the 3.0.0 version bump. 06394d1 touched only the pins README in these six modules;
the work commit changes docs, CI and the release script, no module source. The format commit
changes layout only: a token comparison of every C++ file (comments included, whitespace removed,
adjacent string literals joined) and an AST comparison of every Python file find no difference.
The version bump changes `version` and `requires_core` in `module.properties` (and the module
README's requirement line). Core 3.0.0's `src/` is d14d490's apart from its own format commit
(core test card). The host tests and the compile were re-run after all of these.
