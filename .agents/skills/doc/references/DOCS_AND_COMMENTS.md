# Docs and comments

## Docs are part of the change

A command, setting, NVS key, build define, listener or test that is added or changed updates
the module's `README.md` in the same change. A contract change updates `doc/CONTRACT.md`, and
also `tools/validate.py` when the validator enforces it. READMEs and the contract describe
current behaviour only, with no history, no dates and no version tags on features.

`tools/validate.py` requires `modules/<slug>/README.md` and reads and writes `doc/modules.md` by
path, and `scripts/release.sh` edits `doc/modules.md`. Keep both names.

Some source comments point at module README sections by name, such as wifi's "Known issues".
Keep those headings when editing a README.

## Comments

A comment or docstring says what the code does or why: an invariant, a limit, a hardware or
library fact, a stored-data compatibility reason. It never carries process: no agent or session
names, dates, decision or finding ids, "was ..." history, review, report or verification
status. History lives in the xewe-labs `docs/`. Source files keep their SPDX and path header
lines.

`test_led.py` and `test_led-web.py` fail when module sources contain process words such as
`DECISION`, `PORT-FIX`, `golden`, `oracle`, `2.3.0`, `TODO` or `CONTRACT`, so never cite the
contract by file name in led-web sources.

A module's `Config.h` is not ordinary commentary. Its `/* */` lines are the entries the tools
copy into a project's `Config.h`, and the validator checks their shape (`doc/CONTRACT.md`
section 3.2). Edit them only as part of a value change.

## Elsewhere

The xewe-labs organization guidelines at `https://github.com/xewe-labs/.github/tree/main/guidelines` apply where this skill is silent.
XeWeCore's reference is its `doc/` (start at `doc/README.md`; `doc/CONCEPTS.md` covers
listeners and `doc/CONFIG.md` the config scheme), `src/XeWeCore/Module/README.md` for module
hooks, and `src/XeWeCore/Settings/README.md` for the settings table.
