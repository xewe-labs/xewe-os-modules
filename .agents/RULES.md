# RULES.md — root rules for agents in this project

Every rule below is absolute. It applies without exception, in every project that carries this
folder, and is never changed at install time or by an agent. Rules are cited by ID (`R-01` …
`R-15`) and are never renumbered. User-level rules live in `PREFERENCES.md` (R-15). If two
instructions conflict, R-03 decides.

## 1. Entry and Read Order

- **R-01 Read order.** On opening the project, read completely and in this order: `AGENTS.md`,
  `RULES.md`, `PREFERENCES.md`, `handoffs/HANDOFF.md` (through R-04), then the frontmatter of
  every `skills/*/SKILL.md`. Do no project work before all of these have been read.
- **R-02 Pickup before work.** The first action after reading is the `pickup` procedure of the
  `wax_handoff` skill. Restate the previous exit point to the human before touching anything
  else.
- **R-03 Precedence.** An explicit human instruction given in the current session outranks
  `RULES.md`, which outranks `PREFERENCES.md`, which outranks `AGENTS.md`, which outranks any
  individual skill. A preference never overrides a rule.
  Every human-instructed deviation is recorded in that session's handoff under "Design
  decisions", quoting the instruction.

## 2. Handoffs

- **R-04 Access only through `wax_handoff`.** Nothing under `handoffs/` is read, created,
  edited, moved, or deleted except by executing the `pickup` or `handoff` procedure of the
  `wax_handoff` skill. The exceptions belong to `wax_init`: filling the project name into the
  shipped genesis entry of a brand-new copy, and, during an upgrade, adding head keys that the
  current format requires to an existing `HANDOFF.md`. It never creates, edits, or removes an
  entry.
- **R-05 HEAD moves with the directory.** Writing an entry under `handoffs/handoffs/` and
  updating `handoffs/HANDOFF.md` are one operation. Never do one without the other.
- **R-06 Every session ends with a handoff.** A session that produced changes and no handoff
  is incomplete; say so to the human.
- **R-07 The template is copied, never filled in place.** `handoffs/handoffs/yyyy-mm-dd-hh-mm-ss.md`
  keeps its literal name and its placeholder content permanently.
- **R-08 Naming.** Entry filenames are UTC timestamps in the form `yyyy-mm-dd-hh-mm-ss.md`.
  HEAD is always the lexically greatest entry filename. Never backdate an entry.
- **R-09 Stop on inconsistency.** If `handoffs/HANDOFF.md` disagrees with the directory
  (HEAD, count, index, or a missing file), do not repair, do not write, do not guess. Report
  the exact mismatch to the human and wait.

## 3. Skills

- **R-10 Discover skills from their frontmatter.** A skill is a folder `skills/<name>/` that
  holds a `SKILL.md`. Skills are found by reading the `name` and `description` frontmatter of
  every `skills/*/SKILL.md`, the same way the host tool finds them. Nothing else indexes them.
- **R-11 Valid or nonexistent.** A folder under `skills/` whose `SKILL.md` is missing or fails
  R-12 is not a skill and must not be used. Adding or removing a skill is one change: the
  whole folder, with a valid `SKILL.md`.
- **R-12 Skill shape.** A skill is a folder containing `SKILL.md` whose YAML frontmatter has
  exactly two keys, `name` and `description`, and whose `name` equals the folder name.
  Supporting files sit flat beside `SKILL.md` or under `references/`, `scripts/`, `assets/`,
  or `evals/`.

## 4. Structure of `.agents/`

- **R-13 Fixed top level.** `.agents/` contains exactly `AGENTS.md`, `RULES.md`,
  `PREFERENCES.md`, `handoffs/`, and `skills/`, plus only the items listed in P-11. Never add,
  rename, or remove a top-level item.
- **R-14 Uppercase names are fixed.** `AGENTS.md`, `RULES.md`, `PREFERENCES.md`, `HANDOFF.md`,
  and every `SKILL.md` keep their names and locations.
- **R-15 Rules are root, preferences are user-level.** `RULES.md` is never edited by an agent
  or at install time. `PREFERENCES.md` ships with defaults; they are changed only during the
  `setup` procedure of `wax_init`, or later on an explicit human instruction quoted in that
  session's handoff. Preferences are cited by ID (`P-01` …) and bind exactly like rules until
  changed.

## 5. Project rules (xewe-os-modules)

Added for this project on the owner's instruction; they bind like the rules above and are cited
as `X-NN`. They are not part of the WAX reference.

- **X-01 The validator passes.** `tools/validate.py --harness <project>` exits 0 after every change
  to a module, `module.properties`, `libraries.toml` or `doc/modules.md`. It enforces, per module:
  `declare` type == folder and first argument `os`; `name` and `id` as string literals in the
  `.cpp`; the required files and `include=src/<Folder>/<Folder>.h`; `test_compiles`, `test_status`
  and one behaviour test; the `repo` URL; `depends_libraries` names (never XeWeCore/XeWeOS, listed in
  `libraries.toml`); the forbidden source patterns (X-04); `requires_core` ≥ 2.1.0 when core 2.1
  features are used; setting keys of 1–15 characters, unique per module; no strays; an up-to-date
  `doc/modules.md`. The tools' rules (slug, id, folder, declare, dependencies, version, description,
  `requires_core` syntax) run first.
- **X-02 Module layout.** `modules/<slug>/` holds `module.properties`, `src/<Folder>/`,
  `tests/board/test_<slug>.py`, optional `tests/unit/`, `README.md`, and nothing else: no `*.ino`,
  `scripts/`, per-module `LICENSE.txt` or `.gitignore`; `tests/` holds only `board/` and `unit/`,
  with no `conftest.py` or `__init__.py`. No `module.properties` outside `modules/<slug>/`.
- **X-03 Ids and keys.** An `id` matches `[a-z][a-z0-9_]*`, is at most 15 characters, and never
  changes once released (it is the CLI group and the NVS namespace). NVS keys and settings-table keys
  are 1–15 characters. A stored key, mode id, chip id or blob layout is never renamed, renumbered or
  retyped: devices in the field lose their data. A rename is a MAJOR version bump.
- **X-04 Forbidden patterns.** In `src/`: `cli(` (arduino-esp32 macro; use `os.cli.execute`),
  `std::span` (use `xewe::span`), `xewe::os::`, `ModuleController`, the word `controller`,
  `xewe_cli`, `this->os`, `<XeWeOS.h>`, any XeWeCore header other than `<XeWeCore.h>` in a module
  header (pure headers may use the host-includable `XeWeCore/Utils/Color.h` and `Listeners.h`).
  Also never: a constructor parameter named `os` (name it `host`), `[&]` or `[=]` in a handler
  registered from a constructor (capture `[this]`), blocking in `loop()`, a prompt outside a setup
  routine.
- **X-05 Class shape.** `class <Folder> : public xewe::Module` in the global namespace, constructor
  `(xewe::Os& host, <Dep>& <var>_ref..., <Config> config = {})`, `has_cli_commands = true`, a
  `status()` override whose first line is `<name> module (enabled|disabled)`, a `reset()` override
  when the module holds RAM state, hardware or pin claims.
- **X-06 `requires_core`.** `requires_core=>=2.1.0,<3.0.0` (comma form, full versions). A module
  using `SettingDef`, `ListenerSet`, `SchemaOut` or `xewe::pins` must not declare a lower bound
  below 2.1.0.
- **X-07 Core 2.1 conventions.** Plain settings live in the settings table, never in hand-written
  NVS reads or writes; secrets carry `SECRET`. Non-row values go through `schema_extra`. Change
  events go through a `ListenerSet` with an `origin`. Every driven GPIO is claimed in `xewe::pins`
  and released when let go. Hex colours use the core's parser. (`doc/contract.md` section 3.1.)
- **X-08 Libraries.** `depends_libraries` lists only Arduino libraries that are pinned in
  `libraries.toml` (sorted, one table per name, exactly `repo` and `ref`); never XeWeCore or a library
  bundled with the esp32 core. Changing a pinned `ref` needs a compile of every module that uses it
  on c3, c6 and s3.
- **X-09 Tests.** Use only the plugin fixtures (`compiled`, `board`, `firmware`, `serial`). No
  `time.sleep`. A test undoes what it changes. Credentials and wiring come from
  `XEWE_TEST_<ID>_<WHAT>` environment variables; without them the test skips, never hangs. Regexes
  are copied from real output. Unit tests are marked `@pytest.mark.unit` and use
  `MODULE_DIR = module_dir(__file__)`; C++ unit tests assert a minimum check count.
- **X-10 Harness, not template.** Modules are compiled and tested in a copy of the xewe-os template,
  never in the template itself, and never by editing the tools or a reference clone.
- **X-11 Comments describe the code.** A comment or docstring says what the code does or why (an
  invariant, a limit, a hardware or library fact, a stored-data compatibility reason). No process in
  code: no agent or session names, dates, decision or finding ids, "was …" history, review, report
  or verification status. History lives in the xewe-labs `docs/`. Source files keep their SPDX and
  path header lines.
- **X-12 Generated files.** Never edit `doc/modules.md` by hand (`tools/validate.py --write-index`), and
  never edit a project's `build/modules/`, `src/Modules.h` or `XeWeModules.h`: they are rewritten
  from this repository.
- **X-13 Docs are part of the change.** A command, setting, NVS key, build define, listener or test
  that is added or changed updates the module's `README.md` in the same change; a contract change
  updates `doc/contract.md` and, when the validator enforces it, `tools/validate.py`. READMEs and the
  contract describe current behaviour only: no history, no dates, no version tags on features.
- **X-14 Credentials.** Never open, print or copy a dotenv or key file; the tools read them. WiFi
  credentials in tests come from the environment.
