# Adding a module

1. Copy the closest module and rename the directory, folder, files and class.
   `modules/pins` has no dependencies and no stored state. `modules/time` depends on `wifi` and
   has one settings row. `modules/buttons` has a stored blob, `schema_extra` rows and pin claims.
   XeWeCore's `examples/02_MyModule` is the other starting point.
2. Fill `module.properties` with every key in the order of `doc/CONTRACT.md` section 2:
   `repo=https://github.com/xewe-labs/xewe-os-modules/tree/main/modules/<slug>`,
   `include=src/<Folder>/<Folder>.h`, `declare=<Folder> <var>(os[, <dep var>...]);`,
   `requires_core=>=3.0.0,<4.0.0`. A library in `depends_libraries` gets a table in
   `libraries.toml`, sorted, with exactly `repo` and `ref`.
3. Write the class to `doc/CONTRACT.md` section 3 and MODULE_RULES.md: the `host` parameter,
   `[this]` captures, `xewe::span`, a `status()` override, and `reset()` when it holds state or
   pins.
4. Put settings in a table, values that are not rows in `schema_extra`, changes other code needs
   through a `ListenerSet` with an `origin`, and claim driven GPIOs in `xewe::pins`
   (CORE_CONVENTIONS.md).
5. Write `tests/board/test_<slug>.py` from the template in `doc/CONTRACT.md` section 4, with
   `test_compiles`, `test_status` and one behaviour test whose regex comes from the module's
   real output. Write `tests/unit/test_<slug>.py` with at least
   `test_properties_match_source`. Put pure logic in a header with a C++ unit test.
6. Write `README.md` in the shape of the other module READMEs: what it does, commands,
   settings, NVS keys, build defines, listeners, requirements, known issues, tests.
7. Run `tools/validate.py --write-index`, add the module to the table in the root `README.md`,
   and run the checks in COMMANDS.md.

When changing a module, bump `version` (MODULE_RULES.md), update its README in the same change,
and keep its stored keys.
