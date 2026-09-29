# xewe-os-modules — the registry of XeWe OS modules

XeWe OS module registry · created 2026-09-15 · Solo: Max Dokukin · Status: Active (6 modules listed)

## Overview

The registry of [XeWe OS](https://github.com/xewe-labs/xewe-os) modules.
[`repositories.txt`](repositories.txt) lists module repositories, one per line.
`xewe-os/setup.sh` reads that list to show the available modules, resolve their
requirements and install the ones you choose. Anyone can add a module with a pull request. The
registry was created when the modules were split out of the firmware into their own repositories
(September 2026); it holds no code, only the list and the contract a module must meet.

## Highlights

- One file, one module repository per line; blank lines and `#` comments are ignored
- Each module describes itself in `module.properties` (11 keys) — the registry never duplicates that metadata
- `setup.sh` rejects duplicate slugs, invalid slugs, reused install folders and dependency cycles
- Any public git repository may be listed, not only repositories in the `xewe-labs` organization

## How it works

```
setup.sh → download repositories.txt (main) → for each URL: fetch module.properties (raw URL, else shallow clone)
         → checklist of slug + description → add depends_modules → clone chosen modules → copy src/<Folder>/ into src/modules/
```

1. Downloads `repositories.txt` from this repository (`main` branch).
2. Reads each listed repository's `module.properties` for its slug, description and required
   modules.
3. Shows the modules in a checklist; required modules are added automatically.
4. Clones each chosen module (branch `main` unless `--modules-ref` is given) and copies its
   `src/<Folder>/` into the firmware.

### Listed modules

| Module | Slug / CLI id | Requires | Repository |
|---|---|---|---|
| Wifi | `wifi` / `$wifi` | — | [xewe-os-module-wifi](https://github.com/xewe-labs/xewe-os-module-wifi) |
| WebInterface | `web-interface` / `$web_interface` | wifi | [xewe-os-module-web-interface](https://github.com/xewe-labs/xewe-os-module-web-interface) |
| Time | `time` / `$time` | wifi | [xewe-os-module-time](https://github.com/xewe-labs/xewe-os-module-time) |
| Scheduler | `scheduler` / `$schedule` | time | [xewe-os-module-scheduler](https://github.com/xewe-labs/xewe-os-module-scheduler) |
| Buttons | `buttons` / `$buttons` | — | [xewe-os-module-buttons](https://github.com/xewe-labs/xewe-os-module-buttons) |
| Pins | `pins` / `$pins` | — | [xewe-os-module-pins](https://github.com/xewe-labs/xewe-os-module-pins) |

### Adding a module

Open a pull request that adds one line with your repository's URL to `repositories.txt`, e.g.

```text
https://github.com/<you>/xewe-os-module-relay
```

Your repository must meet these requirements (reviewers check them):

- [ ] **Public git repository** reachable at the URL, with the module on branch `main`.
- [ ] **`module.properties`** at the root with all of these keys:

  ```
  name=Relay
  slug=relay
  id=relay
  version=0.1.0
  description=Switches a relay from the command line and schedules
  repo=https://github.com/<you>/xewe-os-module-relay
  folder=Relay
  include=src/Relay/Relay.h
  declare=Relay relay(os, time_module);
  depends_modules=time
  depends_libraries=XeWeOS (>=0.1.0)
  ```

- [ ] **Unique names:** `slug`, `id` (CLI group and NVS namespace, at most 15 characters),
  `folder`, the class name and the variable name in `declare` must not be used by any module
  already listed. Modules share one firmware, one CLI and one NVS partition.
- [ ] **`slug`** uses lowercase letters, digits and dashes; the repository should be named
  `xewe-os-module-<slug>`.
- [ ] **Source layout:** the module is in `src/<Folder>/`, one folder named like its class, and
  includes other modules relatively (`#include "../Wifi/Wifi.h"`).
- [ ] **Requirements exist:** every slug in `depends_modules` is already in this registry.
  `declare` may only use `os` and the variable names declared by those modules.
- [ ] **It builds:** the validation firmware compiles for ESP32-C3, C6 and S3
  (`scripts/validate.sh`, copied from any existing module repository).
- [ ] **README** describing the module and its commands, and a **license**.

Start from an existing module such as
[xewe-os-module-pins](https://github.com/xewe-labs/xewe-os-module-pins), the
[module guideline](https://github.com/xewe-labs/.github/blob/main/guidelines/modules.md) and the
[XeWeOS README](https://github.com/xewe-labs/xewe-library-os) for the module API and lifecycle.

### Changing or removing a module

Open a pull request that edits or removes the line. A module's `slug` and `id` should not change
once it is listed: firmware stores settings under the `id`, and other modules may depend on the
`slug`.

## Results

| Metric | Value | Baseline / note |
|---|---|---|
| Modules listed | 6 | `repositories.txt` |
| Contract | 11 `module.properties` keys, 8 review checks | this README |

A registry has no measured results; the table lists what it holds.

## Getting started

Use the registry through xewe-os:

```bash
git clone https://github.com/xewe-labs/xewe-os
cd xewe-os
./setup.sh                                          # checklist of the modules listed here
```

To try a modified list before it is merged:

```bash
./setup.sh --modules-index path/to/repositories.txt
```

Entries in a local list may also be folders with a module checkout, which is handy while
developing a module.

## Documents

- [repositories.txt](repositories.txt)
- Firmware: [xewe-os](https://github.com/xewe-labs/xewe-os) · framework: [xewe-library-os](https://github.com/xewe-labs/xewe-library-os)
- License: GPL-3.0. See [LICENSE.txt](LICENSE.txt).
