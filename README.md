# xewe-os-modules

The registry of [XeWe OS](https://github.com/xewe-labs/xewe-os) modules.
[`repositories.txt`](repositories.txt) lists module repositories, one per line.
`xewe-os/setup.sh` reads that list to show the available modules, resolve their
requirements and install the ones you choose. Anyone can add a module with a pull request.

## How setup.sh uses it

1. Downloads `repositories.txt` from this repository (`main` branch).
2. Reads each listed repository's `module.properties` for its slug, description and required
   modules.
3. Shows the modules in a checklist; required modules are added automatically.
4. Clones each chosen module (branch `main` unless `--modules-ref` is given) and copies its
   `src/<Folder>/` into the firmware.

To try a modified list before it is merged:

```bash
./setup.sh --modules-index path/to/repositories.txt
```

Entries in a local list may also be folders with a module checkout, which is handy while
developing a module.

## Adding a module

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
[xewe-os-module-pins](https://github.com/xewe-labs/xewe-os-module-pins) and the
[XeWe OS guide to adding a module](https://github.com/xewe-labs/xewe-os/blob/main/doc/ADDING_A_MODULE.md).

## Changing or removing a module

Open a pull request that edits or removes the line. A module's `slug` and `id` should not change
once it is listed: firmware stores settings under the `id`, and other modules may depend on the
`slug`.

## License

GPL-3.0. See [LICENSE.txt](LICENSE.txt).
