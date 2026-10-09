# Agent rules for xewe-os-modules

[CONTRACT.md](CONTRACT.md) is the full contract; this is the checklist. Every box must hold for
every module under `modules/<slug>/`.

## Layout

- [ ] `modules/<slug>/` holds exactly `module.properties`, `src/<Folder>/<Folder>.h`,
      `src/<Folder>/<Folder>.cpp`, `tests/test_<slug>.py`, `README.md`.
- [ ] No `*.ino`, `scripts/`, per-module `LICENSE.txt` or `.gitignore`, no `conftest.py` or
      `__init__.py` in `tests/`, no `xewe-os-module-*` directories, no `module.properties`
      outside `modules/<slug>/`.

## module.properties

- [ ] Every key, in this order, even when empty: `name`, `slug`, `id`, `version`, `description`,
      `repo`, `folder`, `include`, `declare`, `depends_modules`, `depends_libraries`,
      `requires_core`. `key=value`, no spaces around `=`.
- [ ] `slug` == directory name. `name` == the C++ name argument (string literal in the `.cpp`).
- [ ] `id` matches `[a-z][a-z0-9_]*`, **at most 15 characters** (it is the NVS namespace), is a
      string literal in the `.cpp`, and never changes once released. NVS keys are at most 15
      characters too.
- [ ] `repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>`.
- [ ] `include=src/<Folder>/<Folder>.h`.
- [ ] `declare=<Folder> <var>(os[, <dep var>...]);` exactly: type == folder, first argument is
      `os`, the other arguments are `declare` variables of modules in `depends_modules`, variable
      unique and never `os`.
- [ ] `requires_core=>=2.0.0,<3.0.0` (comma form; `>=2.0.0 <3` does not parse).
- [ ] `depends_libraries` lists only Arduino libraries pinned in the harness `xewe.lock`
      `[libraries]`; never XeWeCore/XeWeOS or esp32-core libraries (WiFi, WebServer, Wire, ...).
- [ ] Bump `version`: MINOR for added commands, MAJOR for removed or changed ones (pre-1.0: MINOR
      for breaking).

## C++

- [ ] `class <Folder> : public xewe::Module`, in the global namespace.
- [ ] The header includes `#include <XeWeCore.h>` and no other XeWeCore header. esp32-core headers
      (`<WiFi.h>`, `<WebServer.h>`, `<Wire.h>`) are fine. A required module's header is included
      relatively: `#include "../Wifi/Wifi.h"`.
- [ ] Constructor: `(xewe::Os& host, <Dep>& <var>_ref..., <Config> config = {})` and
      `: xewe::Module(host, "<id>", "<name>", ...)`. The Os parameter is named **`host`** (a
      parameter named `os` hides the member). Dependency parameters are `<var>_ref`; members are
      named like the dependency's `declare` variable (`wifi`, `time_module`), and the body calls
      `add_requirement(<member>)`.
- [ ] Bodies and lambdas use the member `os` (`os.serial`, `os.nvs`, `os.cli`), never `this->os`.
- [ ] Handlers registered from a constructor capture **`[this]` only**, never `[&]` or `[=]`.
      Store value parameters in members first.
- [ ] Never write `cli(`: arduino-esp32 defines `cli()` as a macro. Use `os.cli.execute("...")`;
      brace-initialise anything named `cli`.
- [ ] Commands: `register_command({name, description, "$<id> <cmd> ...", arg_count, fn})` with
      `fn` taking `xewe::span<const std::string>` (never `std::span`); the sample usage is a
      literal string.
- [ ] Pass `has_cli_commands = true` (so `$<id> status` exists).
- [ ] Override `status(bool verbose) const`: start from `Module::status(false)`, append the
      module's state, print when `verbose`. The first printed line must match
      `<name> module (enabled|disabled)`.
- [ ] Override `reset(...)` when the module holds RAM state or hardware: clear it, then call
      `Module::reset(...)`.
- [ ] `loop()` must not block (no waiting loops, no prompts). Known exception: wifi (see its
      README).
- [ ] None of the old API anywhere: `xewe::os::`, `ModuleController`, `controller`,
      `controller.xewe_cli` member access (now `os.cli`), `<XeWeOS.h>`, `std::span`.

## Tests

- [ ] `tests/test_<slug>.py` defines `test_compiles(compiled)`, `test_status(serial)` and at least
      one behaviour test that runs one of the module's own commands (or reads its output) with a
      regex copied from what the module really prints.
- [ ] Use only the plugin's fixtures (`compiled`, `board`, `firmware`, `serial`); never redefine
      them. Mark pure-Python tests `@pytest.mark.host`.
- [ ] No `time.sleep`; use `serial.expect(..., timeout=)`. Mutating tests undo what they do
      (add, then remove).
- [ ] Credentials or wiring come from `XEWE_TEST_<ID>_<WHAT>` env vars; skip with
      `pytest.skip("needs ...")` when unset, never hang.
- [ ] The docstring states the hardware preconditions (hardware tests assume a provisioned board).

## Before you call it done

- [ ] `<harness>/build/tools/.venv/bin/python tools/validate.py --harness <harness>` exits 0
      (after `tools/validate.py --write-index` if module metadata changed).
- [ ] In a **copy** of the xewe-os template (never the template itself), for each changed module:
      `xewe modules select <slug>` then `xewe test --module <slug> --all-chips` (every
      `test_compiles` passed, the rest "compiled, not run", exit 0).
- [ ] `./setup.sh --modules all`, then `xewe build --all-chips` (c3, c6, s3 `ok`, 0 warnings) and
      `xewe test`.
- [ ] No git writes; no edits to the reference clones, the template or the tools.
