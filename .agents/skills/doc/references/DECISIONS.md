# Decisions

Each line records one decision that a later change could undo by accident. Write it as a date,
the decision, and the reason, with the newest line last. The dates are the commits that first
contained the decision (git log -S), so the real decision may be older.

2026-10-09 Module classes live in the global namespace, so the generated `declare` lines and the template sketch name them unqualified. A clash with a user's global name fails at compile time and is fixed by a rename.
2026-10-09 The class name `Time` stays, although it is generic, because renaming the folder changes the install path for existing firmware.
2026-10-11 Each module is released on its own as tag `<slug>/X.Y.Z`, and the set tag `X.Y.Z` marks a commit where every module is released at that version. Projects pin either in `[modules] ref`.
2026-10-11 wifi's `loop()` waits for the network while disconnected. time, web-interface and scheduler need the network, so the device holds until it is back instead of running them offline. It is the one exception to the non-blocking `loop()` rule.
2026-10-11 Unfinished modules (fan, mlx90614) are parked outside the repository until they are ready, so every module here passes the validator and the release gate.
2026-10-20 The 3.1.0 rename of `XEWE_MODULE_<SLUG>_<VAR>` to `XEWE_<SLUG>_<VAR>` is an announced exception to "renaming a define is breaking": it is listed under BREAKING in the 3.1.0 notes and keeps no alias.
2026-10-22 No release folder is tracked. A release's notes and card live in its tag message and its GitHub Release, and `scripts/release.sh` writes everything else under the ignored `build/release/`.
