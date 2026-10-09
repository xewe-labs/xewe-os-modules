# xewe-os-modules

The modules of [XeWe OS](https://github.com/xewe-labs/xewe-os), in one repo. Each module is a
C++ class built on [XeWeCore](https://github.com/xewe-labs/xewe-os-core) plus the metadata and
tests that `xewe` (from [xewe-os-tools](https://github.com/xewe-labs/xewe-os-tools)) needs to
install, order and test it. The full rules are in [CONTRACT.md](CONTRACT.md); agents also read
[AGENTS.md](AGENTS.md). The module list is [MODULES.md](MODULES.md) (generated).

License: GPL-3.0-only, see [LICENSE.txt](LICENSE.txt).

## What a module is

```
modules/<slug>/
├── module.properties          # metadata; read by xewe setup / modules select / validate
├── src/<Folder>/<Folder>.h    # class <Folder> : public xewe::Module, includes <XeWeCore.h>
├── src/<Folder>/<Folder>.cpp
├── tests/test_<slug>.py       # pytest, run by `xewe test` inside an xewe-os harness
└── README.md
```

- **Install.** `./setup.sh --modules <slug>` (or `xewe modules select <slug>`) in an xewe-os
  project copies `src/<Folder>/` to `src/modules/<Folder>/` and writes `src/modules/Modules.h`:
  one `#include "<Folder>/<Folder>.h"` and the module's `declare=` line (for example
  `Wifi wifi(os);`), dependencies first. Nothing else from the module goes into the firmware.
- **Identity.** `slug` is the directory name and the `--modules` value. `id` is the CLI group
  (`$<id> ...`) and the NVS namespace: at most 15 characters, and it never changes once released.
  `name` equals the name the C++ class passes to `xewe::Module` (`$<id> status` prints
  `<name> module enabled`).
- **Dependencies.** `depends_modules=wifi` selects and orders the dependency, the `declare` line
  passes its variable (`Time time_module(os, wifi);`), and the header includes it relatively
  (`#include "../Wifi/Wifi.h"`).
- **Versions.** `version` is the module's own semver (informational, plus the promise for its
  commands); `requires_core` is the XeWeCore range in the tools' syntax, `>=2.0.0,<3.0.0`. The
  repo tag (`v0.2.0`) is what an xewe-os `xewe.lock` pins.

## Adding a module

1. Copy an existing module that looks like yours (`modules/pins` has no dependencies and no
   stored state; `modules/time` depends on `wifi`), or start from XeWeCore's
   `extras/ModuleTemplate`. Rename the folder, files and class.
2. Fill in `module.properties` with every key, in the order of CONTRACT.md section 2:
   `repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>`,
   `declare=<Folder> <var>(os[, <dep var>...]);`, `depends_libraries=` for Arduino libraries
   (not esp32-core libraries, not XeWeCore). Pin each one in [`libraries.toml`](libraries.toml),
   the library catalogue (`[FastLED] repo = "..." ref = "3.10.3"`): `xewe setup` installs the
   libraries of the selected modules from it, unless the harness `xewe.lock` `[libraries]` pins
   the same name (the lock wins).
3. Follow the C++ rules in [AGENTS.md](AGENTS.md) (`host` parameter, `[this]` captures, no `cli(`,
   `xewe::span`, a `status()` override).
4. Write `tests/test_<slug>.py` with `test_compiles`, `test_status` and one behaviour test
   (template in CONTRACT.md section 4).
5. `tools/validate.py --write-index`, then run the checks below.

## Testing a module through an xewe-os harness

Modules are tested inside a **copy** of the [xewe-os](https://github.com/xewe-labs/xewe-os)
template, never the template itself (`setup --modules` rewrites its `xewe.lock`). Phase 1 takes
the local checkouts through environment variables:

```sh
cp -r /path/to/xewe-os "$SCRATCH/harness" && cd "$SCRATCH/harness"
export XEWE_TOOLS_SOURCE=/path/to/xewe-os-tools XEWE_CORE_SOURCE=/path/to/xewe-os-core \
       XEWE_MODULES_SOURCE=/path/to/xewe-os-modules
# optional: reuse an installed esp32 core instead of downloading it
export XEWE_ARDUINO_DATA=/path/to/arduino15

./setup.sh --modules wifi </dev/null                            # wifi and its dependencies
build/.venv/bin/python -m xewe test --module wifi               # one chip (the lock's chip)
build/.venv/bin/python -m xewe test --module wifi --all-chips   # c3, c6, s3
build/.venv/bin/python -m xewe test --host-only                 # host tests only, no build
```

`setup.sh` copies the modules checkout, so re-run it after editing a module here. Without a
board, `test_compiles` really builds and passes or fails, and the serial tests report
`compiled, not run`; the run exits 0. Hardware tests assume a provisioned board (first-boot
prompts answered); each test file states its preconditions.

Repo checks, from the module repo with the harness venv:

```sh
$HARNESS/build/.venv/bin/python tools/validate.py --harness $HARNESS   # tools rules + repo rules
$HARNESS/build/.venv/bin/python -m xewe --project $HARNESS modules validate "$PWD"
$HARNESS/build/.venv/bin/python tools/validate.py --write-index        # regenerate MODULES.md
```

The gate before a change is done: the validator exits 0; for each changed module,
`xewe modules select <slug>` and `xewe test --module <slug> --all-chips` (it compiles with only
its dependencies); and `./setup.sh --modules all` then `xewe build --all-chips` and `xewe test`
(no clashes between modules).
