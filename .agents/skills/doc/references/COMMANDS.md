# Commands

## Harness

Nothing here compiles on its own. Modules build and test inside a harness: a copy of the xewe-os
template with every module selected and `XEWE_MODULES_SOURCE` pointing at this checkout. Never
work in the template itself, because `setup.sh --modules` rewrites its `xewe.toml`. Never edit
the tools or a reference clone to make a module pass.

With `H=<harness>` and `XEWE_HOME` set as the harness expects:

```sh
$H/build/tools/.venv/bin/python tools/validate.py --harness $H        # exits 0, last line "N modules, 0 errors, W warnings"
cd $H && XEWE_NO_BOARD=1 build/tools/.venv/bin/python -m xewe modules generate
XEWE_NO_BOARD=1 build/tools/.venv/bin/python -m xewe test --unit-only            # module unit tests
build/tools/.venv/bin/python -m xewe test --module <slug> --chip c3              # compile + test_compiles
```

`build/modules/` in the harness holds copies, so re-run `xewe modules generate` there after
editing a module here.

## Validator

Run `tools/validate.py --harness <project>` after every change to a module, a
`module.properties`, `libraries.toml` or `doc/modules.md`, and get exit 0. After any
`module.properties` change, run `tools/validate.py --write-index` and validate again, because
the `index` rule compares `doc/modules.md` with what `--write-index` writes. `tools/validate.py`
needs a Python with xewe-os-tools installed, which is a harness venv. Without it, it exits 3.

The tools' rules (slug, id, folder, declare, dependencies, version, description, `requires_core`
syntax) run first. The repository rules then check per module: the `declare` type equals the
folder and its first argument is `os`; `name` and `id` appear as string literals in the `.cpp`;
the required files exist and `include=src/<Folder>/<Folder>.h`; the board test defines
`test_compiles`, `test_status` and one behaviour test; the `repo` URL; the `depends_libraries`
names (never XeWeCore or XeWeOS, listed in `libraries.toml`); the forbidden source patterns
(MODULE_RULES.md); a `requires_core` lower bound of at least 2.1.0 when core 2.1 features are
used; setting keys of 1 to 15 characters, unique per module; the `Config.h` scheme; no strays;
an up-to-date `doc/modules.md`. `doc/CONTRACT.md` section 5 is the full table.

## Unit tests

They run on the developer machine: the `@pytest.mark.unit` tests in `tests/unit/` and the C++
files they build with g++. A C++ host build that includes core headers needs XeWeCore's `src/`,
found through `XEWE_CORE_SOURCE`, the core checkout next to this repo, or the harness's
`build/libraries/XeWeCore`.

## Compile gate

Without a board, `test_compiles` really builds, every serial test reports "compiled, not run",
and the run exits 0. A change is done when it passes twice (`doc/CONTRACT.md` section 4). First
each changed module alone: `xewe modules select <slug>`, then
`xewe test --module <slug> --all-chips`. Then all modules together: `--modules all`,
`xewe build --all-chips`, `xewe test`.

## Board tests

They need a provisioned board (`xewe provision`) and the board lock
(`flock <project>/.board.lock`). Never flash a board unless asked.

## Release

`scripts/release.sh <version> (<module>... | --all) --notes "line"` is the owner's command, and
`scripts/README.md` describes it. It commits, tags and pushes, so never run it against the real
repository or origin. Try it only in a scratch clone with a scratch origin.
