---
name: doc
description: >
  What an agent needs to know about xewe-os-modules that the code does not show: the harness
  that builds and tests modules (XEWE_MODULES_SOURCE, never the template itself), the validator
  and --write-index, unit tests, the compile gate and board tests, the release script that is
  never run, module layout, ids and stored keys, forbidden source patterns, class shape,
  requires_core, libraries.toml and test rules, XeWeCore conventions (settings table,
  schema_extra, listeners, pin claims, FlexData blobs), adding or changing a module, how the
  tools install a module into a project, generated files, docs and comment rules, and the
  decisions log. Use it before adding, changing, validating, testing or releasing a module,
  before editing a README, doc/CONTRACT.md or a comment, and when asking why something is the
  way it is.
---

# doc

This is the index of the project's agent knowledge. Open only the file whose row matches the
task. Each row names one file under `references/`, and every file there has a row.

| File | Read it when |
|---|---|
| `references/COMMANDS.md` | validating, testing, compiling or setting up a harness, or asked about a release |
| `references/MODULE_RULES.md` | writing or reviewing module code, `module.properties`, `libraries.toml` or tests |
| `references/CORE_CONVENTIONS.md` | a module stores settings, announces changes, drives GPIOs or keeps a blob |
| `references/ADDING_A_MODULE.md` | adding a module or bumping a module's version |
| `references/INSTALL_FLOW.md` | explaining what a project gets from this repo, or about to edit a generated file |
| `references/DOCS_AND_COMMENTS.md` | editing a README, `doc/CONTRACT.md`, a comment or a module's `Config.h` |
| `references/DECISIONS.md` | changing anything a past decision covers |
