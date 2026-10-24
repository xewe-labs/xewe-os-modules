# scripts/

`release.sh` releases the named modules, or every module, in one command. It tests them once,
commits the version lines, tags the commit with the notes and one release card per tag in the
tag message, and creates the GitHub Releases. Only maintainers run it.

```sh
scripts/release.sh 3.1.0 wifi --notes "What changed, in one line."
scripts/release.sh 3.1.0 --all --hardware c3 --notes "First line." --notes "Second line."
```

The full form is
`scripts/release.sh <version> (<module>... | --all) [--hardware c3] [--notes "line" ...] [--notes-file FILE] [--resume] [--ci-timeout <min>]`.

## Tags

Each named module is released as tag `<module>/X.Y.Z`, such as `wifi/3.1.0`. `--all` releases
every module and also the set tag `X.Y.Z`, such as `3.1.0`, which marks a commit where every
module is released at that version. Projects pin either in `[modules] ref` of `xewe.toml`, and
a module tag's commit holds every module at its then-released version. Tags have no `v` prefix.

`<version>` is `X.Y.Z`, or `X.Y.Z-suffix` for a pre-release, and the version lines get `X.Y.Z`.
The notes are 1 to 3 lines: `--notes` once per line, the lines of `--notes-file FILE`, or both.
They apply to every tag of the run, so name together only the modules that share one reason, and
otherwise run the script once per module. Everything a run writes goes under
`build/release/<version>/`, which is git-ignored.

## Stages

1. Preflight lists every problem before it refuses. It requires the `main` branch with no change
   at all in the work tree (untracked files included), `main` level with `origin/main` after a
   fetch, and every tag of the run free here and on origin (`--resume` excepted). `tests.yml`
   must call the tools workflow at the ref of its `tools_ref` input, a tools tag unless
   `XEWE_TOOLS_SOURCE` is set. The notes must pass `xewe release-notes check`, `build/` must be
   ignored, and Python 3.11 or newer, the harness project and a logged-in `gh` must be there.
2. Tests set `version=` and `requires_core=` (`>=X.Y.0,<X+1.0.0`) in the named modules'
   `module.properties` and regenerate `doc/modules.md` with `tools/validate.py --write-index`.
   Then `xewe release-card record` records every step: the checkers' venv (ruff, clang-format),
   `tools/validate.py --harness`, `xewe format --check modules tools`, `xewe brand-lint .`, for
   each named module and each chip (c3, c6, s3) `xewe modules select <module>` and
   `xewe build --chip C`, then `xewe test --unit-only`, the all-modules build for the chip and its
   board tests. The steps run in two copies of the harness project with every module selected
   and this tree as the modules source: one for the tests and the all-modules image, one for the
   module-alone builds. A failing step lets the other steps run, and the run then stops
   before the cards, the commit and the tags, with the log in the logs folder.
3. Release cards: `xewe release-card render` writes `build/release/<version>/<module>/release_card.md`
   for each named module with only that module's tests and, with `--all`,
   `build/release/<version>/_set/release_card.md` with every module's tests. It refuses
   otherwise, and the draft is in the logs folder.
4. Commit: only the version-line files are added, as `release <version>` with `--all`, else
   `release <version> <module>...` (`<n> modules` when there are many). Any other changed file
   is a refusal.
5. Tags: each tag's message is the notes, one blank line and that tag's card, written with
   `git tag -a --cleanup=verbatim` because git would otherwise drop the card's `#` lines. The
   script pushes `main` first, so the workflow is registered before a tag triggers it, checks
   that `origin/main` is `HEAD`, then pushes the tags one per push, because GitHub starts no
   workflow when more than 3 tags arrive in one push.
6. Releases: for each tag it waits for the `tests` workflow run (`--ci-timeout <min>` in all,
   default 60) and requires its `xewe release-card verify --ci` step to have run and passed.
   Then `gh release create --verify-tag` makes the Release with the notes as the text, the card
   as its only asset `release_card.md`, and `--prerelease` for a suffix, and the script prints
   the release URLs. The workflow runs on a tag only when `tests.yml` triggers on tags
   (`push: tags`); otherwise the script reports that no run exists for the tag.

The chip is the `--hardware` chip, else c3. With `--hardware` that board is flashed and tested
with `--require-board` and every gate in `BOARD_GATES`. Without it the board tests are compiled,
not run.

A run that stops before stage 4 puts the version lines back. `--resume` continues a run that
stopped. Tags that exist, here or on origin, are not made again, and the missing ones are made
on the same commit. When `HEAD` is the release commit and no tag exists, it reuses
`build/release/<version>/` if every card is there, else runs stage 2 again, then goes on from
stage 4. A Release that exists is only reported, and a missing one is created from its tag's own
message.

Exit code 0 means done, and 1 means a refusal or a failed check.

## Environment

- `XEWE_RELEASE_HARNESS` is the harness project, an xewe project with `xewe.toml` and
  `setup.sh`. The default is `../xewe-os`.
- `XEWE_RELEASE_DEFINES_<CHIP>` holds space-separated `KEY=VALUE` defines for the build, flash
  and board steps of that chip.
- `XEWE_ENV` names the dotenv file of the board run. Without it the harness's dotenv file is
  used, else the tools checkout's. The tools read it; the script never does.
- `XEWE_TOOLS_SOURCE` points to a local tools checkout.
- `RELEASE_CI_POLL` sets the seconds between CI polls (default 15).
