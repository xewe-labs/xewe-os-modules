# AGENTS.md — xewe-os-modules

This is **WAX 1.3**, the WAX Agentic Workspace. The rules, preferences, and skills below belong
to this version. Human documentation lives at https://github.com/maxdokukin/wax_agents.

This folder is the agentic part of the project. It gives you boundaries, context, and tools,
in that order. Read it as described below before doing anything else in the repository.

## Read in this order

1. **`RULES.md` — root boundaries.** Absolute rules, the same in every project. Cite them by
   ID when you explain a decision or a refusal.
2. **`PREFERENCES.md` — user boundaries.** This project's chosen defaults (R-15). They bind
   like rules until a human changes them; cite them by ID too.
3. **`handoffs/HANDOFF.md` — context.** The head of the work record: where the last session
   stopped and what is open. Reach it only through the `pickup` procedure of the
   `wax_handoff` skill (R-04), because the directory has invariants that the skill checks
   before you rely on anything in it.
4. **`skills/` — tools.** One folder per skill, each with a `SKILL.md`. Discover them by
   reading the frontmatter of every `skills/*/SKILL.md` (R-10); a folder without a valid one
   is not a skill (R-11).

## Session shape

- **Pickup → work → handoff.** Start with `wax_handoff` pickup, do the work, end with
  `wax_handoff` handoff. A session that changed anything and did not end with a handoff is
  incomplete (R-06); say so rather than letting it pass.

## Never do these without being asked

- **Edit `RULES.md`, `PREFERENCES.md`, or this file** (R-15, P-05). Propose changes in the
  handoff instead (P-10).
- **Touch anything under `handoffs/` by hand** (R-04). The skill is the only door.
- **Add, rename, or remove a top-level item in `.agents/`** (R-13).
- **Commit, push, tag, release, publish, or delete what you did not create** (P-09).

## Precedence

- **Human instruction in this session > `RULES.md` > `PREFERENCES.md` > this file > a
  skill** (R-03). A project's own `.agents/` wins over any organization-level agent file. Record every
  human-instructed deviation in the handoff, quoting the instruction.

## Reporting back

- **Say what you actually ran** and label anything unverified as unverified (P-07).
- **Never report a skipped or failed step as done** (P-08). Failures come with their output.

## Project: xewe-os-modules

The ready-made modules of XeWe OS: one directory per module under `modules/<slug>/`, each a C++ class
on XeWeCore plus the metadata and tests that `xewe-os-tools` needs to install, order and test it.
Human documentation: `README.md` (what the modules are, how a project uses them), `doc/contract.md` (the
normative specification), `modules/<slug>/README.md`. Project rules are X-01 … X-14 at the end of
`RULES.md`. Organization rules: `https://github.com/xewe-labs/.github/blob/main/AGENTS.md`; they apply
where this file is silent. XeWeCore's own reference is its `doc/` (settings: `doc/os/settings.md`,
modules and listeners: `doc/os/module.md`, pin registry: `doc/utils/pins.md`).

### Layout

- `modules/<slug>/`: `module.properties`, `src/<Folder>/<Folder>.h` + `.cpp` (plus pure headers for
  host-tested logic), `tests/board/test_<slug>.py`, optional `tests/unit/`, `README.md`, and
  `releases/<version>/` once released. Nothing else (X-02).
- `tools/validate.py`: the tools' validator plus the repository rules (doc/contract.md section 5).
- `libraries.toml`: the catalogue of Arduino libraries the modules may list in `depends_libraries`.
- `doc/modules.md`: generated from the `module.properties` files; never edit it by hand (X-12).
- No module is an Arduino library and nothing here is compiled on its own: modules are compiled
  inside an xewe-os project ("harness").
- `scripts/release.sh` releases one module (tag `<slug>/X.Y.Z`, files in
  `modules/<slug>/releases/<version>/`) or the set (tag `X.Y.Z`, `releases/<version>/`) that
  projects pin (README "Releasing"; a module tag pins too). `--prepare <version> <module>...`
  prepares the named modules, `--prepare <version> --all` every module and the set (no git
  change): it sets their `version=` and `requires_core=`, regenerates `doc/modules.md`, stops with
  exit 2 until their `notes.txt` files are filled, then runs one shared check run through `xewe
  release-card record` (harness setup with every module, validator, unit tests, format,
  brand-lint, module-alone builds of the prepared modules, all-modules builds and the board tests
  per chip; logs in `releases/<version>/logs/`, gitignored) and renders the generated
  `release_card.md` of each prepared module (only its tests) and, with `--all`, of the set (no
  hand-written test card). The tag runs verify the card, wait for the `tests` workflow with its
  card check green, and publish the card as the release's only asset. Agents may run `--dry-run`
  only, and `--prepare` only on a scratch copy unless asked; the owner commits, tags and pushes.

### How a project installs a module

`./setup.sh --modules <list>` or `xewe modules select <list>` writes `[modules] selected` in the
project's `xewe.toml`, then `xewe modules generate`:

- copies `modules/<slug>/src/<Folder>/` to `<project>/build/modules/src/<Folder>/` (the generated
  library `XeWeModules`, whose `XeWeModules.h` has one `#include "<Folder>/<Folder>.h"` per module);
- copies `tests/board/` and `tests/unit/` to `<project>/build/modules/tests/<slug>/`, where
  `xewe test` collects them;
- writes `<project>/src/Modules.h`: `#include <XeWeModules.h>` and each module's `declare=` line,
  dependencies first;
- installs the selected modules' `depends_libraries` from `libraries.toml` into
  `build/libraries/<name>`, unless the project's `xewe.toml` `[libraries]` pins the same name (the
  manifest wins).

The modules checkout is `XEWE_MODULES_SOURCE` (or `setup.sh --modules-source DIR`), read in place;
otherwise the shared checkout `~/.xewe-os/build-tools/sources/xewe-os-modules/<ref>/`. `build/modules/`
holds copies, so after editing a module here re-run `xewe modules generate` in the project.

### Check your work

The harness for this workspace is a copy of the xewe-os template with every module selected and
`XEWE_MODULES_SOURCE` pointing here. With `H=<harness>` and `XEWE_HOME` set as the harness expects:

```sh
$H/build/tools/.venv/bin/python tools/validate.py --harness $H        # expect "6 modules, 0 errors"
cd $H && XEWE_NO_BOARD=1 build/tools/.venv/bin/python -m xewe modules generate
XEWE_NO_BOARD=1 build/tools/.venv/bin/python -m xewe test --unit-only            # module unit tests
build/tools/.venv/bin/python -m xewe test --module <slug> --chip c3              # compile + test_compiles
```

- **Validator** after every change to a module (X-01). `--write-index` after any `module.properties`
  change, then validate again (the `index` rule compares `doc/modules.md`).
- **Unit tests** run on the developer machine: the `@pytest.mark.unit` tests in `tests/unit/` and
  the C++ files they build with g++. C++ host builds that include core headers need XeWeCore's
  `src/`: `XEWE_CORE_SOURCE`, the core checkout next to this repo, or the project's
  `build/libraries/XeWeCore`.
- **Compile.** Without a board, `test_compiles` really builds and every serial test reports
  "compiled, not run"; the run exits 0. The full gate (doc/contract.md section 4): each changed module
  alone (`xewe modules select <slug>`, `xewe test --module <slug> --all-chips`), then all modules
  together (`--modules all`, `xewe build --all-chips`, `xewe test`). Never run `setup --modules` in
  the template itself; it rewrites `xewe.toml`.
- **Board tests** need a provisioned board (`xewe provision`) and the board lock
  (`flock <project>/.board.lock`). Never flash a board unasked.

### Adding a module

1. Copy the closest module: `modules/pins` (no dependencies, no stored state), `modules/time`
   (depends on `wifi`, one settings row), `modules/buttons` (a stored blob, `schema_extra` rows, pin
   claims). Or start from XeWeCore's `examples/02_MyModule`.
   Rename the directory, folder, files and class.
2. Fill `module.properties` with every key in the doc/contract.md section 2 order:
   `repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>`,
   `include=src/<Folder>/<Folder>.h`, `declare=<Folder> <var>(os[, <dep var>...]);`,
   `requires_core=>=3.0.0,<4.0.0`. A library in `depends_libraries` gets a table in `libraries.toml`
   (sorted, exactly `repo` and `ref`).
3. Write the class to doc/contract.md section 3 (the C++ rules are X-04 … X-06): `host` parameter,
   `[this]` captures, `xewe::span`, a `status()` override, `reset()` when it holds state or pins.
4. Settings go in a table (`settings()`); values that are not rows in `schema_extra`; changes that
   other code needs go through a `ListenerSet` with an `origin`; driven GPIOs are claimed in
   `xewe::pins` (doc/contract.md section 3.1).
5. Tests: `tests/board/test_<slug>.py` from the template (doc/contract.md section 4) with
   `test_compiles`, `test_status` and one behaviour test whose regex comes from the module's real
   output; `tests/unit/test_<slug>.py` with at least `test_properties_match_source`; pure logic in a
   header with a C++ unit test.
6. `README.md` in the shape of the other module READMEs: overview, commands, settings, NVS keys,
   build defines, listeners, requirements, known issues, tests (X-13).
7. `tools/validate.py --write-index`, add the module to the list in `README.md`, run the checks.

Changing a module: bump `version` (MINOR for added commands, MAJOR for removed or changed ones;
before 1.0 MINOR for breaking), update its README in the same change, keep stored keys (X-07).

### Conventions from XeWeCore 2.1

- **Settings table.** `static constexpr xewe::SettingDef` rows built with
  `xewe::setting<&Class::member>("key", ...)`, returned by `settings()` as `{table, this}`. The core
  loads them before `begin_routines_required` and owns `$<id> set|get|schema`. Apply changes in
  `on_setting_changed(def)` (runs while disabled too; not at the boot load). `SECRET` for
  credentials, `RESTART` for rows that apply after a reboot. A module that owns `set` already
  forwards non-numeric first arguments to `apply_setting`. Older alias commands call
  `apply_setting`.
- **`schema_extra`.** Rows with `"group"` and a `"set"` hint for values that are not table rows
  (scheduler blocks, button mappings). Without a table the core registers no `schema` command; the
  module registers its own (scheduler, buttons).
- **Listeners.** `xewe::ListenerSet<Iface, N>` (fixed size, no heap). Every event carries
  `const void* origin` (`nullptr` from the CLI) so a listener drops its own echo; callbacks run in the
  caller's task, outside any render lock; keep them short.
- **Pin claims.** `xewe::pins::claim(gpio, id.c_str())` before configuring a GPIO, `release` when
  done (`remove`, a pin change, `reset`). A refused claim aborts the operation; the core prints it.
  `$pins claims` shows the registry.
- **Stored blobs.** FlexData blobs carry `schema`; accept a blob only when `has("schema")` and the
  value matches; never reinterpret or overwrite another layout at boot.

### Module notes

- **wifi.** `loop()` waits for the network while disconnected, by design (doc/contract.md section 6); no other module may block.
- **web-interface.** No authentication: every command is reachable over HTTP on the local network.
- **time, scheduler.** The scheduler's id is `schedule`; its member for the time dependency is
  `time_module`.
