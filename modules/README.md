# modules/

Each folder is one module, named by its slug. The tools copy a selected module's
`src/<Folder>/` into a project's build, and its tests into the project's test tree.

```
modules/<slug>/
├── module.properties          # name, id, version, dependencies, the declare line
├── src/<Folder>/<Folder>.h    # class <Folder> : public xewe::Module
├── src/<Folder>/<Folder>.cpp
├── src/<Folder>/Config.h      # optional: the module's build-time values
├── tests/board/test_<slug>.py # tests on a board, through the xewe-os tools
├── tests/unit/                # optional tests on the developer machine
└── README.md                  # commands, settings, NVS keys, build defines, tests
```

Open a module's `README.md` first, then its `module.properties`. To start a new module, copy
`pins/` (no dependencies, nothing stored), `time/` (one dependency, one setting) or `buttons/`
(a stored blob and pin claims). [doc/CONTRACT.md](../doc/CONTRACT.md) is the specification
every module follows.

A module's tests run inside an xewe-os project that selects it:

```sh
build/tools/.venv/bin/python -m xewe test --module <slug>
build/tools/.venv/bin/python -m xewe test --module <slug> --unit-only
```
