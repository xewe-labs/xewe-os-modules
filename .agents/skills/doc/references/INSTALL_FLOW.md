# How a project installs a module

`./setup.sh --modules <list>` or `xewe modules select <list>` writes `[modules] selected` in the
project's `xewe.toml`, then runs `xewe modules generate`, which:

- copies `modules/<slug>/src/<Folder>/` to `<project>/build/modules/src/<Folder>/`, the
  generated library `XeWeModules`, whose `XeWeModules.h` has one `#include "<Folder>/<Folder>.h"`
  per module;
- copies `tests/board/` and `tests/unit/` to `<project>/build/modules/tests/<slug>/`, where
  `xewe test` collects them;
- writes `<project>/src/Modules.h` with `#include <XeWeModules.h>` and each module's `declare=`
  line, dependencies first;
- installs the selected modules' `depends_libraries` from `libraries.toml` into
  `build/libraries/<name>`, unless the project's `xewe.toml` `[libraries]` pins the same name.

The tools read the modules checkout in place from `XEWE_MODULES_SOURCE` (or
`setup.sh --modules-source DIR`). Otherwise they use the shared checkout
`~/.xewe-os/build-tools/sources/xewe-os-modules/<ref>/`.

No module is an Arduino library. Modules are compiled only inside an xewe-os project.

## Generated files

Never edit `doc/modules.md` by hand; `tools/validate.py --write-index` writes it. Never edit a
project's `build/modules/`, `src/Modules.h` or `XeWeModules.h`, because generate rewrites them
from this repository.
