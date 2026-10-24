# Module rules

These hold for every module under `modules/<slug>/`. `doc/CONTRACT.md` is the full
specification; this file lists what reviews keep catching.

## Layout

`modules/<slug>/` holds `module.properties`, `src/<Folder>/`, `tests/board/test_<slug>.py`,
an optional `tests/unit/` and `README.md`, and nothing else. That rules out `*.ino`, `scripts/`,
a per-module `LICENSE.txt` or `.gitignore`. `tests/` holds only `board/` and `unit/`, with no
`conftest.py` or `__init__.py`. No `module.properties` exists outside `modules/<slug>/`.
Pure headers for host-tested logic sit in `src/<Folder>/` beside the class. No release folder
is tracked (DECISIONS.md).

## Ids and stored keys

An `id` matches `[a-z][a-z0-9_]*`, is at most 15 characters, and never changes once released,
because it is the CLI group and the NVS namespace. NVS keys and settings-table keys are 1 to 15
characters. Never rename, renumber or retype a stored key, mode id, chip id or blob layout:
devices in the field lose their data. A rename is a MAJOR version bump.

## Forbidden in src/

- `cli(` (an arduino-esp32 macro; use `os.cli.execute`).
- `std::span` (use `xewe::span`), `xewe::os::`, `ModuleController`, the word `controller`,
  `xewe_cli`, `this->os`, `<XeWeOS.h>`.
- Any XeWeCore header other than `<XeWeCore.h>` in a module header. Pure headers may include the
  host-includable `XeWeCore/Utils/Color.h` and `Listeners.h`.
- A constructor parameter named `os` (name it `host`).
- `[&]` or `[=]` in a handler registered from a constructor (capture `[this]`).
- Blocking in `loop()`, and a prompt outside a setup routine. wifi is the one exception.

## Class shape

`class <Folder> : public xewe::Module` in the global namespace, with the constructor
`(xewe::Os& host, <Dep>& <var>_ref..., <Config> config = {})` and `has_cli_commands = true`.
It overrides `status()`, whose first line is `<name> module (enabled|disabled)`, and overrides
`reset()` when the module holds RAM state, hardware or pin claims.

## requires_core

Write `requires_core=>=3.0.0,<4.0.0` in the comma form with full versions. A module using
`SettingDef`, `ListenerSet`, `SchemaOut` or `xewe::pins` never declares a lower bound below
2.1.0.

## Libraries

`depends_libraries` lists only Arduino libraries pinned in `libraries.toml`, which is sorted and
has one table per name with exactly `repo` and `ref`. It never lists XeWeCore or a library
bundled with the esp32 core. A changed `ref` needs a compile on c3, c6 and s3 of every module
that uses it.

## Tests

- Use only the plugin fixtures `compiled`, `board`, `firmware` and `serial`.
- No `time.sleep`.
- A test undoes what it changes.
- Credentials and wiring come from `XEWE_TEST_<ID>_<WHAT>` environment variables. Without them
  the test skips; it never hangs. WiFi credentials come from the environment too.
- Copy every regex from the module's real output.
- Mark unit tests `@pytest.mark.unit` and set `MODULE_DIR = module_dir(__file__)`.
- A C++ unit test asserts a minimum check count.

## Versions

Bump `version` on every module change: MINOR for added commands, MAJOR for removed or changed
ones, and MINOR for breaking changes before 1.0.
