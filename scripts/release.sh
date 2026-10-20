#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
# xewe-os-modules/scripts/release.sh
#
# release.sh: test every module in one shared run, write the release cards, then tag and publish
# each module and the module set.
# Usage: scripts/release.sh --prepare <version> --all [--hardware c3,c6,s3] [--harness <dir>] [--resume] [--dry-run] | <version> <module>|--set [--dry-run] [--ci-timeout <min>] [--skip-ci]
# Each module is released on its own, as tag <module>/<version> (e.g. wifi/3.1.0); the set tag
# <version> then marks a commit where every module is released at <version>, and is what projects
# pin (xewe.toml [modules] ref). No `v` prefix anywhere. A release is two kinds of runs with one
# commit in between.
# `scripts/release.sh --prepare <version> --all` (10 stages) makes no git change:
#   1 preflight: refuses a detached HEAD, a merge/rebase/cherry-pick/revert in progress, unmerged
#     paths, any change outside the version lines (module.properties version= and requires_core=,
#     the matching cells of doc/modules.md), releases/<version>/ and modules/*/releases/<version>/,
#     a testcard.md in any of those release folders, and a .gitignore that does not ignore
#     releases/*/logs/; finds the tools ref (tests.yml's xewe-modules-tests.yml@<ref>, a tag),
#     Python >= 3.11 and the harness project (an xewe project: --harness, else
#     XEWE_RELEASE_HARNESS, else ../xewe-os);
#   2 card tools: a venv with the tools (build/release-venv/card);
#   3 versions: version=<version> and requires_core=>=<major>.<minor>.0,<<major+1>.0.0 in every
#     modules/*/module.properties, then doc/modules.md regenerated (tools/validate.py --write-index);
#   4 release notes: modules/<module>/releases/<version>/notes.txt for every module and the set's
#     releases/<version>/notes.txt; a missing one gets a skeleton, and while any skeleton marker is
#     left the run stops with exit 2 (fill them, run again);
#   5 card run: `xewe release-card init` on releases/<version>/logs/ (gitignored), the ONE run every
#     card of this release is rendered from;
#   6 setup (recorded): the checkers' venv (tools + ruff + clang-format, as CI's format job) and two
#     copies of the harness project with every module selected and this tree as the modules source
#     (XEWE_MODULES_SOURCE=<this tree, exported to build/release-venv/xewe-os-modules>):
#     build/release-harness/project (tests, all-modules images) and build/release-harness/alone
#     (module-alone builds);
#   7 checks (recorded): tools/validate.py --harness on the export (validate; the validator flags
#     every .ino under its repo, so it never sees build/), `xewe test --unit-only` (unit, JUnit, the
#     command CI runs), `xewe format --check modules tools`, `xewe brand-lint .`;
#   8 module-alone builds (recorded, build): for every module and chip, `xewe modules select
#     <module>` (the module and what it depends on) and `xewe build --chip C`;
#   9 boards (recorded, per chip): the all-modules image (build), then `xewe test --chip C -- -m
#     board` (board, JUnit) with the board-test gates set. A chip without --hardware is built with
#     XEWE_NO_BOARD=1: its board tests are "compiled, not run". A --hardware chip is flashed (a
#     flash step) and tested with --require-board, all gates in one session;
#  10 cards: `xewe release-card render` writes one card per module
#     (modules/<module>/releases/<version>/release_card.md, only that module's tests: --tests
#     modules/<module>/) and the set card (releases/<version>/release_card.md, every module's
#     tests), or refuses (the draft is in the logs folder); prints the commit command.
# A failing step does not stop the others; the cards refuse it. --resume (after a failed step):
# keeps the run when commit and tree are unchanged and re-runs only the steps that are not green.
# XEWE_RELEASE_DEFINES_<CHIP> (environment, e.g. XEWE_RELEASE_DEFINES_S3="XEWE_LED_PIN_DATA=48")
# are the defines of that chip's all-modules image, flash and board session. XEWE_ENV, else the
# harness's dotenv file, else the tools checkout's is the dotenv the board run uses: the script
# never reads it, the tools do.
# Then commit the listed files as "prepare <version>" and push; the tests workflow's card job checks
# every card against its own unit test run.
# `scripts/release.sh <version> <module>` (5 stages), one module's tag run:
#   1 preflight: version X.Y.Z; the module exists; on main, clean, level with origin/main after a
#     fetch; tag <module>/<version> free here and on origin; its notes.txt (no skeleton marker) and
#     release_card.md committed; no testcard.md; module.properties says version=<version>; gh
#     logged in;
#   2 release card: `xewe release-card verify` on the module's card (as rendered, its tested tree
#     is HEAD's clean tree, the shared run's local logs present with their sha256);
#   3 CI gate on HEAD: the newest `tests` run for HEAD on main must succeed, with the card job's
#     verify step run and green (waits; --ci-timeout minutes, default 30);
#   4 annotated tag <module>/<version> on HEAD, push it, `gh release create` (not marked latest,
#     notes from notes.txt, release_card.md the only asset);
#   5 print how consumers get it (the set tag).
# `scripts/release.sh <version> --set`, after every module's release: the same preflight for tag
# <version> and releases/<version>/, plus for every module in modules/: version=<version>, tag
# <module>/<version> on HEAD or an ancestor, its files unchanged since that tag, its GitHub Release
# present; then verify on the set card, the CI gate, tag <version> on HEAD and a GitHub Release
# whose notes are releases/<version>/notes.txt plus a list of the module releases, with the set's
# release_card.md as the only asset.
# Re-running a tag run resumes: a tag without a GitHub Release only creates the release (pushing the
# tag first if origin lacks it) from the tagged commit's notes.txt and release_card.md, provided the
# tagged commit's files say <version> (for the set: pass the module checks). Tag and release both
# present: already released. --skip-ci skips the CI gate (emergencies only). --dry-run prints every
# command and changes nothing: only read-only git, gh and tool lookups and `release-card verify`
# run; it reports every failed check, never waits, and exits 1 if a check failed.
# Exit codes: 0 done, 1 a refusal or a failed check, 2 notes.txt files to fill.
set -euo pipefail

REPO="xewe-labs/xewe-os-modules"
TITLE="xewe-os-modules"
TOOLS_URL="https://github.com/xewe-labs/xewe-os-tools"
CI_WORKFLOW="tests"                                  # .github/workflows/tests.yml
CI_FILE=".github/workflows/tests.yml"
CI_CARD_JOB="modules / release card check"           # caller job id / xewe-modules-tests.yml job name
CI_CARD_STEP="xewe release-card verify --ci"         # its step that compares the cards with the run
CI_POLL="${RELEASE_CI_POLL:-15}"                     # seconds between CI polls
VENVS="build/release-venv"                           # gitignored (build/)
HARNESS="build/release-harness/project"             # harness copy: tests and all-modules images
ALONE="build/release-harness/alone"                  # harness copy: module-alone builds
EXPORT="build/release-venv/xewe-os-modules"          # the tree as a commit of it would hold it
CHIPS=(c3 c6 s3)
REQUIRE="setup,validate,unit,build,board"
CHECKERS=("ruff==0.17.0" "clang-format==23.1.3")    # xewe-modules-tests.yml format job pins
# the gated board tests the card session runs: one session per chip, all gates
BOARD_GATES=(XEWE_TEST_LED_RESET=1 XEWE_TEST_LED_WEB_HTTP=1 XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1)
NOTES_MARKER="XEWE-NOTES-SKELETON"

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }
STAGES=5
stage() { echo "== $1/${STAGES} $2"; }

# A failed preflight check: fatal in a real run; a dry run notes it and goes on.
FAILED=0
fail() {
  [[ "${DRY_RUN}" == 1 ]] || die "$*"
  echo "error: $*" >&2
  FAILED=$((FAILED + 1))
}

# Runs a command, or only prints it with --dry-run.
run() {
  if [[ "${DRY_RUN}" == 1 ]]; then
    printf '+'; printf ' %q' "$@"; printf '\n'
  else
    "$@"
  fi
}

# file_at REF FILE: FILE's content at commit REF, or in the working tree when REF is empty.
file_at() {
  if [[ -z "$1" ]]; then cat -- "$2"; else git show "$1:$2"; fi
}

# module_version MODULE REF: the version= value of MODULE's module.properties at REF (see file_at).
module_version() {
  file_at "$2" "modules/$1/module.properties" 2>/dev/null | sed -n 's/^version=//p' | head -n 1
}

# module_names: every module of the working tree (a folder of modules/ with a module.properties).
module_names() {
  local dir
  for dir in modules/*/; do
    [[ -f "${dir}module.properties" ]] && basename "${dir}"
  done
  return 0
}

# core_range VERSION: the requires_core value of a release VERSION: >=MAJOR.MINOR.0,<MAJOR+1.0.0.
core_range() {
  local major minor
  IFS=. read -r major minor _ <<< "$1"
  echo ">=${major}.${minor}.0,<$((major + 1)).0.0"
}

# versioned_props MODULE VERSION REF: MODULE's module.properties at REF with version= and
# requires_core= set for VERSION; exit 1 unless each key is on exactly one line.
versioned_props() {
  local file="modules/$1/module.properties" key
  for key in version requires_core; do
    [[ "$(file_at "$3" "${file}" 2>/dev/null | grep -c "^${key}=" || true)" == 1 ]] || return 1
  done
  file_at "$3" "${file}" | sed -e "s/^version=.*/version=$2/" -e "s/^requires_core=.*/requires_core=$(core_range "$2")/"
}

# versioned_index VERSION REF: doc/modules.md at REF with every module row's Version and Requires
# core cells set for VERSION (what `tools/validate.py --write-index` writes after stage 3).
versioned_index() {
  file_at "$2" doc/modules.md | awk -v ver="$1" -v req="\`$(core_range "$1")\` |" '
    BEGIN { FS = " \\| "; OFS = " | " }
    /^\| \[`/ && NF == 7 { $4 = ver; $7 = req }
    { print }'
}

# allowed_change PATH VERSION: true for a path --prepare may find changed: anything under
# releases/VERSION/ or modules/<module>/releases/VERSION/, a module.properties whose only change
# from HEAD is its version lines, and doc/modules.md whose only change is those cells.
allowed_change() {
  local p slug
  [[ "$1" == "releases/$2/"* ]] && return 0
  if [[ "$1" == modules/* ]]; then
    p="${1#modules/}"; slug="${p%%/*}"
    [[ "${p}" == "${slug}/releases/$2/"* ]] && return 0
    if [[ "${p}" == "${slug}/module.properties" ]]; then
      [[ -f "$1" ]] && git cat-file -e "HEAD:$1" 2>/dev/null && cmp -s "$1" <(versioned_props "${slug}" "$2" HEAD)
      return
    fi
    return 1
  fi
  if [[ "$1" == doc/modules.md ]]; then
    [[ -f "$1" ]] && git cat-file -e "HEAD:$1" 2>/dev/null && cmp -s "$1" <(versioned_index "$2" HEAD)
    return
  fi
  return 1
}

# release_dirs VERSION: the nine (one per module, plus the set's) release folders of VERSION.
release_dirs() {
  local m
  while IFS= read -r m; do echo "modules/${m}/releases/$1"; done < <(module_names)
  echo "releases/$1"
}

# tree_problems VERSION: one line per reason the work tree cannot be prepared; none when it can.
tree_problems() {
  local v="$1" p path entry code unmerged dir dirty=()
  git symbolic-ref -q HEAD >/dev/null || echo "HEAD is detached: check out the branch to release from"
  for p in MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD REBASE_HEAD rebase-merge rebase-apply; do
    path="$(git rev-parse --git-path "${p}")"
    [[ -e "${path}" ]] && echo "a merge, rebase, cherry-pick or revert is in progress (${path} exists): finish or abort it first"
  done
  unmerged="$(git diff --name-only --diff-filter=U)"
  [[ -z "${unmerged}" ]] || echo "unmerged (conflicted) paths: $(tr '\n' ' ' <<< "${unmerged}")"
  # porcelain v1 -z: "XY path", a rename or copy followed by its original path as its own field
  while IFS= read -r -d '' entry; do
    code="${entry:0:2}"; path="${entry:3}"
    allowed_change "${path}" "${v}" || dirty+=("${path}")
    if [[ "${code}" == *[RC]* ]]; then
      IFS= read -r -d '' path || true
      allowed_change "${path}" "${v}" || dirty+=("${path}")
    fi
  done < <(git status --porcelain=v1 -z --untracked-files=all)
  if [[ "${#dirty[@]}" -gt 0 ]]; then
    echo "changes outside the version lines, releases/${v}/ and modules/*/releases/${v}/ (commit or stash them first): ${dirty[*]}"
  fi
  git check-ignore -q "releases/${v}/logs/run.json" \
    || echo ".gitignore does not ignore releases/*/logs/ (the run folder must never be committed)"
  while IFS= read -r dir; do
    [[ ! -e "${dir}/testcard.md" ]] \
      || echo "${dir}/testcard.md exists: the generated release_card.md replaces it; delete it"
  done < <(release_dirs "${v}")
}

# tools_ref: the ref of tests.yml's `uses: …/xewe-modules-tests.yml@<ref>` (the tools CI runs with).
tools_ref() {
  sed -nE 's|^ *uses: *xewe-labs/xewe-os-tools/[.]github/workflows/xewe-modules-tests[.]yml@([^ ]+) *$|\1|p' "${CI_FILE}" | head -n 1
}

# tools_ref_input: tests.yml's `tools_ref:` input (the tools of CI's format, brand-lint and card jobs).
tools_ref_input() {
  sed -nE 's|^ *tools_ref: *"?([^" ]+)"? *$|\1|p' "${CI_FILE}" | head -n 1
}

# find_python: python3.12 (CI's card and format jobs), else the first python3 >= 3.11 on PATH.
find_python() {
  local cand p
  for cand in python3.12 python3 python3.13 python3.14 python3.11; do
    p="$(command -v "${cand}" 2>/dev/null)" || continue
    if "${p}" -c 'import sys; sys.exit(sys.version_info < (3, 11))' 2>/dev/null; then
      echo "${p}"
      return 0
    fi
  done
  return 0
}

tag_local() { git rev-parse -q --verify "refs/tags/$1" >/dev/null; }
tag_remote() { git ls-remote --exit-code --tags origin "refs/tags/$1" >/dev/null 2>&1; }

# tag_commit TAG: the commit the tag points at (local tag first, else origin's peeled tag).
tag_commit() {
  if tag_local "$1"; then
    git rev-parse "refs/tags/$1^{commit}"
  else
    git ls-remote --tags origin \
      | awk -v t="refs/tags/$1" '$2 == t { c = $1 } $2 == t "^{}" { p = $1 } END { print (p != "" ? p : c) }'
  fi
}

# ci_run SHA: the newest run of CI_WORKFLOW for SHA on main, as "id status url conclusion" (empty
# when there is none yet; conclusion is empty while the run is not completed).
ci_run() {
  gh run list --commit "$1" --workflow "${CI_WORKFLOW}" --branch main --limit 20 \
    --json databaseId,status,url,conclusion \
    --jq '.[0] | select(. != null) | "\(.databaseId) \(.status) \(.url) \(.conclusion)"'
}

# card_step_result RUN_ID: the conclusion of the card job's verify step in that run (skipped when
# the job found no card for the commit).
card_step_result() {
  gh run view "$1" --json jobs \
    --jq ".jobs[] | select(.name == \"${CI_CARD_JOB}\") | .steps[] | select(.name == \"${CI_CARD_STEP}\") | .conclusion"
}

# ci_gate SHA: wait for CI_WORKFLOW on SHA and fail unless it succeeds with the cards compared,
# within CI_TIMEOUT minutes.
ci_gate() {
  local sha="$1" deadline=$((SECONDS + CI_TIMEOUT * 60)) line id status url conclusion pid card
  if [[ "${SKIP_CI}" == 1 ]]; then
    warn "!!!!!!!! --skip-ci: CI on ${sha:0:12} is NOT checked; this release may ship a red build !!!!!!!!"
    return 0
  fi
  if [[ "${DRY_RUN}" == 1 ]]; then
    echo "+ gh run list --commit ${sha} --workflow ${CI_WORKFLOW} --branch main ... (poll every ${CI_POLL}s until a run appears, up to ${CI_TIMEOUT} min)"
    echo "+ gh run watch <run id> --exit-status (if not completed; fail unless success)"
    echo "+ gh run view <run id> --json jobs (job '${CI_CARD_JOB}', step '${CI_CARD_STEP}' must be success, not skipped)"
    if [[ "${GH_OK}" == 1 && "${sha}" =~ ^[0-9a-f]{40}$ ]]; then
      line="$(ci_run "${sha}" 2>/dev/null)" || line="(gh run list failed)"
      echo "   current CI state of ${sha:0:12}: ${line:-no ${CI_WORKFLOW} run yet}"
      read -r id status url conclusion <<< "${line:-x x x x}"
      if [[ "${status}" == completed ]]; then
        card="$(card_step_result "${id}" 2>/dev/null)" || card="(gh run view failed)"
        echo "   card check of that run: ${card:-no \"${CI_CARD_STEP}\" step}"
      fi
    fi
    return 0
  fi
  echo "   waiting for a ${CI_WORKFLOW} run on ${sha:0:12} (timeout ${CI_TIMEOUT} min)"
  while :; do
    line="$(ci_run "${sha}")" || die "gh run list failed"
    [[ -n "${line}" ]] && break
    (( SECONDS < deadline )) || die "timed out after ${CI_TIMEOUT} min: no ${CI_WORKFLOW} run for ${sha:0:12} on main"
    sleep "${CI_POLL}"
  done
  read -r id status url conclusion <<< "${line}"
  echo "   run ${url} (${status})"
  if [[ "${status}" != completed ]]; then
    gh run watch "${id}" --exit-status --interval "${CI_POLL}" &
    pid=$!
    while kill -0 "${pid}" 2>/dev/null; do
      if (( SECONDS >= deadline )); then
        kill "${pid}" 2>/dev/null || true
        die "timed out after ${CI_TIMEOUT} min, CI still running: ${url}"
      fi
      sleep 2
    done
    wait "${pid}" || die "CI failed: ${url}"
    conclusion=success
  fi
  [[ "${conclusion}" == success ]] || die "CI ${conclusion:-did not succeed}: ${url}"
  card="$(card_step_result "${id}")" || die "gh run view ${id} failed"
  [[ "${card}" == success ]] \
    || die "CI ${url}: the '${CI_CARD_JOB}' job compared no card with this commit (step '${CI_CARD_STEP}': ${card:-not found}); are the release cards of ${VERSION} the cards of this tree?"
  echo "   CI passed, cards compared: ${url}"
}

# set_problems VERSION REF: print one line per module that keeps commit REF from being the set
# VERSION: version= is not VERSION, tag <module>/VERSION is missing or not REF or an ancestor of it,
# the module's files differ between that tag and REF, or the module's GitHub Release is missing.
set_problems() {
  local v="$1" ref="$2" dir m have tagged
  for dir in modules/*/; do
    m="$(basename "${dir}")"
    [[ -n "$(file_at "${ref}" "modules/${m}/module.properties" 2>/dev/null)" ]] || continue
    have="$(module_version "${m}" "${ref}")"
    if [[ "${have}" != "${v}" ]]; then
      echo "${m}: module.properties says version=${have}"
    elif ! { tag_local "${m}/${v}" || tag_remote "${m}/${v}"; }; then
      echo "${m}: no tag ${m}/${v} (scripts/release.sh ${v} ${m})"
    else
      tagged="$(tag_commit "${m}/${v}")"
      if ! git merge-base --is-ancestor "${tagged}" "${ref}" 2>/dev/null; then
        echo "${m}: tag ${m}/${v} (${tagged:0:12}) is not ${ref:0:12} or an ancestor of it"
      elif ! git diff --quiet "${tagged}" "${ref}" -- "modules/${m}"; then
        echo "${m}: modules/${m} changed since tag ${m}/${v}: release it again under a new version"
      elif [[ "${GH_OK}" == 1 ]] && ! gh release view "${m}/${v}" >/dev/null 2>&1; then
        echo "${m}: tag ${m}/${v} has no GitHub Release (scripts/release.sh ${v} ${m} resumes it)"
      fi
    fi
  done
}

# tools_spec: what pip installs as the tools: XEWE_TOOLS_SOURCE (a local checkout), else the tools
# repo at TOOLS_REF (the tag CI runs with).
tools_spec() {
  if [[ -n "${XEWE_TOOLS_SOURCE:-}" ]]; then echo "${XEWE_TOOLS_SOURCE}"; else echo "git+${TOOLS_URL}@${TOOLS_REF}"; fi
}

# card_tools PYTHON: the venv whose tools run `xewe release-card` and write doc/modules.md (not
# recorded: it makes the cards). XEWE is the command prefix.
card_tools() {
  local commit
  XEWE=("${VENVS}/card/bin/python" -m xewe)
  run "$1" -m venv --clear "${VENVS}/card"
  run "${VENVS}/card/bin/python" -m pip install -q --disable-pip-version-check --progress-bar off "$(tools_spec)"
  # the tools commit the card header names (the venv is not an editable install)
  if [[ "${DRY_RUN}" == 1 ]]; then
    echo "+ (tools commit) > ${VENVS}/card/xewe-tools-commit"
    return 0
  fi
  if [[ -n "${XEWE_TOOLS_SOURCE:-}" ]]; then
    commit="$(git -C "${XEWE_TOOLS_SOURCE}" rev-parse HEAD 2>/dev/null || echo -)"
  else
    commit="$(git ls-remote "${TOOLS_URL}" "refs/tags/${TOOLS_REF}^{}" "refs/tags/${TOOLS_REF}" "refs/heads/${TOOLS_REF}" \
      | awk '{ c[NR] = $1 } END { print (NR ? c[1] : "-") }')"
  fi
  echo "${commit:--}" > "${VENVS}/card/xewe-tools-commit"
}

# step LABEL RECORD_ARGS...: one recorded step; a failed command is noted and the run goes on (the
# cards refuse it), a usage error stops. STEP_RESUME (default RESUME_ARGS) is what it passes as
# --resume.
FAILED_STEPS=()
step() {
  local label="$1" rc=0
  shift
  if [[ "${DRY_RUN}" == 1 ]]; then
    run "${XEWE[@]}" release-card record --dir "${LOGS}" ${STEP_RESUME[@]+"${STEP_RESUME[@]}"} "$@"
    return 0
  fi
  "${XEWE[@]}" release-card record --dir "${LOGS}" ${STEP_RESUME[@]+"${STEP_RESUME[@]}"} "$@" || rc=$?
  case "${rc}" in
    0) ;;
    1|3) FAILED_STEPS+=("${label}"); warn "${label} failed (exit ${rc}; the log is in ${LOGS}); going on" ;;
    *) die "xewe release-card record exited ${rc} on ${label}" ;;
  esac
}

# tree_export: EXPORT = the files a commit of this work tree would hold (tracked, plus untracked
# files that are not ignored; deleted ones left out), so nothing of build/: tools/validate.py runs
# on it (it reports every .ino and module.properties under its repo as stray, and the harness copies
# hold sketches and libraries) and the harnesses use it as the modules source, as CI's checkout.
tree_export() {
  if [[ "${DRY_RUN}" == 1 ]]; then
    echo "+ (export) git ls-files --cached --others --exclude-standard -> ${EXPORT}"
    return 0
  fi
  rm -rf "${EXPORT}"
  mkdir -p "${EXPORT}"
  git ls-files -z --cached --others --exclude-standard \
    | while IFS= read -r -d '' f; do if [[ -e "${f}" || -L "${f}" ]]; then printf '%s\0' "${f}"; fi; done \
    | tar --null -T - -cf - | tar -C "${EXPORT}" -xf -
}

# harness_copy DEST: (re)create a harness project at DEST from HARNESS_SRC: the
# project without .git, build/, releases/, static/firmware/, its tests/ (the cards hold this
# repo's tests only) and dotenv files; [tools] ref set to TOOLS_REF when that is a tag
# (XEWE_TOOLS_SOURCE wins in setup.sh). --resume keeps an existing copy (its setup is green).
harness_copy() {
  local dest="$1"
  if [[ "${DRY_RUN}" == 1 ]]; then
    echo "+ (copy) ${HARNESS_SRC} -> ${dest} without .git build releases static/firmware tests .env*; [tools] ref = \"${TOOLS_REF}\" (a tag only)"
    return 0
  fi
  if [[ "${#RESUME_ARGS[@]}" -gt 0 && -f "${dest}/xewe.toml" ]]; then
    return 0
  fi
  rm -rf "${dest}"
  mkdir -p "${dest}"
  tar -C "${HARNESS_SRC}" --exclude=./.git --exclude=./build --exclude=./releases --exclude=./static/firmware \
    --exclude=./tests --exclude='./.env*' --exclude=__pycache__ -cf - . | tar -C "${dest}" -xf -
  if [[ "${TOOLS_REF}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    awk -v ref="${TOOLS_REF}" '
      /^\[/ { tools = ($0 == "[tools]") }
      tools && /^ref *=/ { print "ref = \"" ref "\""; next }
      { print }' "${dest}/xewe.toml" > "${dest}/xewe.toml.tmp"
    mv "${dest}/xewe.toml.tmp" "${dest}/xewe.toml"
  fi
}

# chip_defines CHIP: `--define KEY=VALUE` words from XEWE_RELEASE_DEFINES_<CHIP> (environment;
# space separated), one per line.
chip_defines() {
  local var d
  var="XEWE_RELEASE_DEFINES_$(tr '[:lower:]' '[:upper:]' <<< "$1")"
  for d in ${!var-}; do printf '%s\n%s\n' --define "${d}"; done
}

# last_build_is_all CHIP: true when the newest build step of CHIP in run.json is the green
# all-modules one, so the card's image of CHIP (the newest build step's) is the all-modules image.
last_build_is_all() {
  [[ -f "${LOGS}/run.json" ]] || return 1
  "${VENVS}/card/bin/python" - "${LOGS}/run.json" "$1" <<'PY'
import json, sys
steps = json.load(open(sys.argv[1], encoding="utf-8")).get("steps", [])
latest = {}
for s in steps:
    latest[json.dumps(s.get("key"))] = s
builds = sorted((s for s in latest.values() if s.get("kind") == "build" and s.get("chip") == sys.argv[2]), key=lambda s: int(s["n"]))
sys.exit(0 if builds and builds[-1].get("label") == "all-modules" and builds[-1].get("exit") == 0 else 1)
PY
}

module_notes_skeleton() {
  cat <<EOF
${NOTES_MARKER}: fill in this file, then delete this line (--prepare stops while it is here)
$1 $2

What the module does and what this release brings, in two or three sentences.

CHANGED

- ...

REQUIREMENTS

- XeWeCore $(core_range "$2") (requires_core); libraries; modules it depends on.

TESTING

What was tested, on which tree and with which result: release_card.md, generated by
scripts/release.sh --prepare $2 --all and attached to this release.
EOF
}

set_notes_skeleton() {
  cat <<EOF
${NOTES_MARKER}: fill in this file, then delete this line (--prepare stops while it is here)
${TITLE} $1

The module set $1: every module in modules/ released at $1. Projects pin this tag.

MODULES

- <module> $1, ... (each one's release is linked below)

REQUIREMENTS

- XeWeCore $(core_range "$1"); xewe-os-tools <X.Y.Z> or later.

TESTING

release_card.md, generated by scripts/release.sh --prepare $1 --all and attached to this release.
EOF
}

# prepare VERSION: the 10 stages of --prepare --all (see the header).
prepare() {
  local v="$1" set_dir="releases/$1" problems x py chip m hw hp ahp file rc dir notes
  local modules=() changed=() todo=() cards=() refused=() defines=() resume_build=()
  LOGS="${set_dir}/logs"
  STAGES=10
  while IFS= read -r m; do modules+=("${m}"); done < <(module_names)
  [[ "${#modules[@]}" -gt 0 ]] || die "no modules in modules/"

  stage 1 preflight
  problems="$(tree_problems "${v}")"
  if [[ -n "${problems}" ]]; then
    if [[ "${DRY_RUN}" == 1 ]]; then
      while IFS= read -r x; do fail "${x}"; done <<< "${problems}"
    else
      die "the work tree cannot be prepared:"$'\n'"${problems}"
    fi
  fi
  TOOLS_REF="$(tools_ref)"
  if [[ -z "${TOOLS_REF}" ]]; then
    fail "${CI_FILE} does not call xewe-os-tools/.github/workflows/xewe-modules-tests.yml@<ref>"
    TOOLS_REF="?"
  elif [[ ! "${TOOLS_REF}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    if [[ -n "${XEWE_TOOLS_SOURCE:-}" ]]; then
      warn "${CI_FILE} calls xewe-modules-tests.yml@${TOOLS_REF}, not a tools tag; XEWE_TOOLS_SOURCE is used here"
    else
      fail "${CI_FILE} calls xewe-modules-tests.yml@${TOOLS_REF}: pin a tools tag (X.Y.Z), so the cards and CI use the same tools"
    fi
  fi
  x="$(tools_ref_input)"
  [[ "${x}" == "${TOOLS_REF}" ]] \
    || fail "${CI_FILE}: tools_ref \"${x:-unset}\" is not the workflow's @${TOOLS_REF} (the card job installs tools_ref)"
  echo "   tools: $(tools_spec) (CI: xewe-modules-tests.yml@${TOOLS_REF})"
  py="$(find_python)"
  [[ -n "${py}" ]] || die "Python >= 3.11 is required (python3.12 is what CI's card job uses)"
  echo "   python: ${py}"
  command -v tar >/dev/null 2>&1 || fail "tar is required"
  HARNESS_SRC="${HARNESS_SRC:-${XEWE_RELEASE_HARNESS:-../xewe-os}}"
  if [[ -f "${HARNESS_SRC}/xewe.toml" && -f "${HARNESS_SRC}/setup.sh" ]]; then
    HARNESS_SRC="$(cd "${HARNESS_SRC}" && pwd)"
    echo "   harness: ${HARNESS_SRC} (copied to ${HARNESS} and ${ALONE}; modules all, source this tree)"
  else
    fail "no xewe project at ${HARNESS_SRC} (xewe.toml and setup.sh): --harness <dir> or XEWE_RELEASE_HARNESS"
  fi
  # the dotenv the board run uses (the tools read it; this script only checks that it exists)
  if [[ -z "${XEWE_ENV:-}" ]]; then
    if [[ -f "${HARNESS_SRC}/.env" ]]; then
      export XEWE_ENV="${HARNESS_SRC}/.env"
    elif [[ -n "${XEWE_TOOLS_SOURCE:-}" && -f "${XEWE_TOOLS_SOURCE}/.env" ]]; then
      export XEWE_ENV="${XEWE_TOOLS_SOURCE}/.env"
    fi
  fi
  echo "   dotenv: ${XEWE_ENV:-none}"
  echo "   modules: ${modules[*]}"
  echo "   hardware: ${HARDWARE:-none} (other chips: compiled, not run)"
  for chip in "${CHIPS[@]}"; do
    x="$(chip_defines "${chip}" | { grep -v '^--define$' || true; } | tr '\n' ' ')"
    [[ -z "${x}" ]] || echo "   ${chip} defines: ${x% }"
  done
  [[ "${FAILED}" -eq 0 ]] && echo "   ok: $(git symbolic-ref --short HEAD 2>/dev/null || echo '(detached)') at $(git rev-parse --short=12 HEAD)"

  stage 2 "card tools"
  card_tools "${py}"

  stage 3 versions
  for m in "${modules[@]}"; do
    file="modules/${m}/module.properties"
    versioned_props "${m}" "${v}" "" >/dev/null || die "${file}: expected exactly one version= and one requires_core= line"
    if ! cmp -s "${file}" <(versioned_props "${m}" "${v}" ""); then
      if [[ "${DRY_RUN}" != 1 ]]; then
        versioned_props "${m}" "${v}" "" > "${file}.tmp"
        cat "${file}.tmp" > "${file}" # cat keeps the file's mode
        rm -f "${file}.tmp"
      fi
      changed+=("${file}")
    fi
  done
  if [[ "${#changed[@]}" -gt 0 ]]; then
    echo "   version ${v}, requires_core $(core_range "${v}"): ${#changed[@]} of ${#modules[@]} module.properties$([[ "${DRY_RUN}" == 1 ]] && echo ' (would change)')"
  else
    echo "   version ${v}: every module.properties already says it"
  fi
  run "${VENVS}/card/bin/python" tools/validate.py --write-index

  stage 4 "release notes"
  for dir in $(release_dirs "${v}"); do
    notes="${dir}/notes.txt"
    if [[ ! -s "${notes}" ]]; then
      echo "   create ${notes} (skeleton)"
      if [[ "${DRY_RUN}" != 1 ]]; then
        mkdir -p "${dir}"
        if [[ "${dir}" == "${set_dir}" ]]; then
          set_notes_skeleton "${v}" > "${notes}"
        else
          m="${dir#modules/}"
          module_notes_skeleton "${m%%/*}" "${v}" > "${notes}"
        fi
      fi
      todo+=("${notes}")
    elif grep -q -- "${NOTES_MARKER}" "${notes}"; then
      echo "   ${notes} is still the skeleton"
      todo+=("${notes}")
    else
      echo "   ${notes}: filled ($(wc -l < "${notes}" | tr -d ' ') lines)"
    fi
  done
  if [[ "${#todo[@]}" -gt 0 ]]; then
    if [[ "${DRY_RUN}" == 1 ]]; then
      echo "   a real run stops here (exit 2) until these are filled: ${todo[*]}"
    else
      echo "error: fill ${todo[*]} (each the GitHub Release text, embedded in its card), delete each ${NOTES_MARKER} line, then run scripts/release.sh --prepare ${v} --all again" >&2
      exit 2
    fi
  fi

  stage 5 "card run"
  # every card's tested tree leaves out the release folders (the notes and cards themselves)
  run "${XEWE[@]}" release-card init --dir "${LOGS}" --repo "${REPO}" --version "${v}" --tag "${v}" \
    --exclude 'modules/*/releases' ${RESUME_ARGS[@]+"${RESUME_ARGS[@]}"}

  stage 6 setup
  STEP_RESUME=(${RESUME_ARGS[@]+"${RESUME_ARGS[@]}"})
  x="${VENVS}/py/bin/python"
  step venv --kind setup --name venv -- "${py}" -m venv --clear "${VENVS}/py"
  step install --kind setup --name install -- "${x}" -m pip install --disable-pip-version-check --progress-bar off \
    "$(tools_spec)" "${CHECKERS[@]}"
  tree_export
  harness_copy "${HARNESS}"
  harness_copy "${ALONE}"
  # record --kind setup keeps build/config/build_config.toml of the repo root: the harness's (pins)
  run mkdir -p build/config
  run ln -sfn "../../${HARNESS}/build/config/build_config.toml" build/config/build_config.toml
  # setup.sh installs the tools of [tools] (or XEWE_TOOLS_SOURCE), the shared toolchain, every
  # module's libraries and the modules from this tree (XEWE_MODULES_SOURCE: the export, used in place)
  for x in "${HARNESS}" "${ALONE}"; do
    step "setup ${x}" --kind setup --name "harness-${x##*/}" -- env -u XEWE_PROJECT "XEWE_MODULES_SOURCE=${ROOT}/${EXPORT}" \
      XEWE_NO_BOARD=1 bash "${x}/setup.sh" --modules all
  done
  hp="${HARNESS}/build/tools/.venv/bin/python"
  ahp="${ALONE}/build/tools/.venv/bin/python"

  stage 7 checks
  step validate --kind validate --name validate -- "${hp}" "${EXPORT}/tools/validate.py" --harness "${HARNESS}"
  # the command of xewe-modules-tests.yml's unit step, so the card's unit rows are CI's
  step unit --kind unit --name unit --junit "${VENVS}/junit-unit.xml" -- env XEWE_NO_BOARD=1 "${hp}" -m xewe \
    --project "${HARNESS}" test --unit-only -- "--junitxml=${ROOT}/${VENVS}/junit-unit.xml" -o junit_family=xunit1 \
    -o junit_logging=no -ra
  step format --kind check --name format -- env "XEWE_CLANG_FORMAT=${VENVS}/py/bin/clang-format" \
    "${VENVS}/py/bin/python" -m xewe format --check modules tools
  step brand-lint --kind check --name brand-lint -- "${VENVS}/py/bin/python" -m xewe brand-lint .

  stage 8 "module-alone builds"
  # each module with only what it depends on; the build steps are named <n>-build-<chip>-<module>
  for chip in "${CHIPS[@]}"; do
    for m in "${modules[@]}"; do
      # shellcheck disable=SC2016 # expanded by the inner bash
      step "build ${m} ${chip}" --kind build --chip "${chip}" --name "${m}" \
        --meta "${ALONE}/build/builds/${chip}/out/meta.json" -- env XEWE_NO_BOARD=1 bash -c \
        'set -euo pipefail; "$0" -m xewe --project "$1" modules select "$2"; "$0" -m xewe --project "$1" build --chip "$3"' \
        "${ahp}" "${ALONE}" "${m}" "${chip}"
    done
  done

  stage 9 boards
  for chip in "${CHIPS[@]}"; do
    hw=0
    [[ ",${HARDWARE}," == *",${chip},"* ]] && hw=1
    defines=()
    while IFS= read -r x; do defines+=("${x}"); done < <(chip_defines "${chip}")
    # the card's image of a chip is its newest build step: the all-modules build must come last
    # (CI compares it with its own all-modules build); on --resume it runs again unless it already
    # is the newest green build, and then the flash and board steps of a --hardware chip run again
    # too (a new image)
    resume_build=(${RESUME_ARGS[@]+"${RESUME_ARGS[@]}"})
    if [[ "${#RESUME_ARGS[@]}" -gt 0 && "${DRY_RUN}" != 1 ]] && ! last_build_is_all "${chip}"; then
      resume_build=()
    fi
    STEP_RESUME=(${resume_build[@]+"${resume_build[@]}"})
    if [[ "${hw}" == 1 ]]; then
      step "build all ${chip}" --kind build --chip "${chip}" --name all-modules \
        --meta "${HARNESS}/build/builds/${chip}/out/meta.json" -- env -u XEWE_NO_BOARD "${hp}" -m xewe \
        --project "${HARNESS}" build --chip "${chip}" ${defines[@]+"${defines[@]}"}
      x="${HARNESS}/build/builds/${chip}/out/<image>.bin"
      if [[ "${DRY_RUN}" != 1 ]]; then
        x="$(find "${HARNESS}/build/builds/${chip}/out" -maxdepth 1 -name "*-${chip}-*.bin" ! -name '*bootloader*' ! -name '*partitions*' 2>/dev/null | head -n 1)"
        [[ -n "${x}" ]] || x="${HARNESS}/build/builds/${chip}/out/missing.bin"
      fi
      step "flash ${chip}" --kind flash --chip "${chip}" --firmware "${x}" --meta "${HARNESS}/build/builds/${chip}/out/meta.json" \
        -- env -u XEWE_NO_BOARD "${hp}" -m xewe --project "${HARNESS}" flash --chip "${chip}" --no-build --require-board \
        ${defines[@]+"${defines[@]}"}
      step "board ${chip}" --kind board --chip "${chip}" --junit "${VENVS}/junit-board-${chip}.xml" \
        -- env -u XEWE_NO_BOARD "${BOARD_GATES[@]}" "${hp}" -m xewe --project "${HARNESS}" test --chip "${chip}" \
        --require-board ${defines[@]+"${defines[@]}"} -- -m board -o junit_family=xunit1 -ra \
        "--junitxml=${ROOT}/${VENVS}/junit-board-${chip}.xml"
    else
      step "build all ${chip}" --kind build --chip "${chip}" --name all-modules \
        --meta "${HARNESS}/build/builds/${chip}/out/meta.json" -- env XEWE_NO_BOARD=1 "${hp}" -m xewe \
        --project "${HARNESS}" build --chip "${chip}" ${defines[@]+"${defines[@]}"}
      STEP_RESUME=(${RESUME_ARGS[@]+"${RESUME_ARGS[@]}"})
      step "board ${chip}" --kind board --chip "${chip}" --junit "${VENVS}/junit-board-${chip}.xml" \
        -- env XEWE_NO_BOARD=1 "${BOARD_GATES[@]}" "${hp}" -m xewe --project "${HARNESS}" test --chip "${chip}" \
        ${defines[@]+"${defines[@]}"} -- -m board -o junit_family=xunit1 -ra \
        "--junitxml=${ROOT}/${VENVS}/junit-board-${chip}.xml"
    fi
  done
  if [[ "${#FAILED_STEPS[@]}" -gt 0 ]]; then
    warn "failed steps: ${FAILED_STEPS[*]} (the cards will refuse them)"
  fi

  stage 10 cards
  for dir in $(release_dirs "${v}"); do
    rc=0
    if [[ "${dir}" == "${set_dir}" ]]; then
      # every module's tests (the harness's own tests/ is not copied; CI's is left out by the prefix)
      run "${XEWE[@]}" release-card render --dir "${LOGS}" --notes "${dir}/notes.txt" --out "${dir}/release_card.md" \
        --require "${REQUIRE}" --chips "$(IFS=,; echo "${CHIPS[*]}")" ${HARDWARE:+--hardware "${HARDWARE}"} \
        --title "${TITLE}" --tests modules/ || rc=$?
    else
      m="${dir#modules/}"; m="${m%%/*}"
      run "${XEWE[@]}" release-card render --dir "${LOGS}" --notes "${dir}/notes.txt" --out "${dir}/release_card.md" \
        --require "${REQUIRE}" --chips "$(IFS=,; echo "${CHIPS[*]}")" ${HARDWARE:+--hardware "${HARDWARE}"} \
        --title "${m}" --tests "modules/${m}/" || rc=$?
    fi
    case "${rc}" in
      0) cards+=("${dir}/release_card.md") ;;
      1) refused+=("${dir}/release_card.md") ;;
      *) die "xewe release-card render exited ${rc}" ;;
    esac
  done
  if [[ "${#refused[@]}" -gt 0 ]]; then
    die "refused: ${refused[*]} (reasons above; draft of the last refusal: ${LOGS}/release_card.draft.md). Fix the cause, then: scripts/release.sh --prepare ${v} --all --resume"
  fi
  changed=()
  if [[ "${DRY_RUN}" != 1 ]]; then
    for m in "${modules[@]}"; do
      git diff --quiet HEAD -- "modules/${m}/module.properties" || changed+=("modules/${m}/module.properties")
    done
    git diff --quiet HEAD -- doc/modules.md || changed+=(doc/modules.md)
    echo "   prepared in ${SECONDS} s; no git change was made. Review the ${#cards[@]} cards, then commit and push:"
  else
    echo "   dry run: nothing was prepared; a real run ends with:"
    changed=("modules/*/module.properties" doc/modules.md)
  fi
  echo "  git add ${changed[*]+${changed[*]} }modules/*/releases/${v} ${set_dir}/notes.txt ${set_dir}/release_card.md && git commit -m \"prepare ${v}\" && git push origin $(git symbolic-ref --short HEAD 2>/dev/null || echo '<branch>')"
  echo "then, once on main with the ${CI_WORKFLOW} workflow green (card job included):"
  echo "  for m in ${modules[*]}; do scripts/release.sh ${v} \"\$m\"; done; scripts/release.sh ${v} --set"
  if [[ "${DRY_RUN}" == 1 && "${FAILED}" -gt 0 ]]; then
    echo "== dry run: ${FAILED} check(s) failed; a real run stops at the first"
    exit 1
  fi
}

usage() { sed -n 's/^# Usage: //p' "${BASH_SOURCE[0]}"; }

# cleanup: the temporary release files of a tag run (EXIT trap).
UPLOAD_DIR=""
SET_NOTES_TMP=""
cleanup() { rm -rf ${UPLOAD_DIR:+"${UPLOAD_DIR}"} ${SET_NOTES_TMP:+"${SET_NOTES_TMP}"}; }

main() {
  VERSION=""
  MODULE=""
  SET=0
  ALL=0
  DRY_RUN=0
  SKIP_CI=0
  PREPARE=0
  RESUME_ARGS=()
  STEP_RESUME=()
  HARDWARE=""
  HARNESS_SRC=""
  CI_TIMEOUT=30
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dry-run) DRY_RUN=1 ;;
      --skip-ci) SKIP_CI=1 ;;
      --prepare) PREPARE=1 ;;
      --set) SET=1 ;;
      --all) ALL=1 ;;
      --resume) RESUME_ARGS=(--resume) ;;
      --hardware)
        [[ "${2:-}" =~ ^(c3|c6|s3)(,(c3|c6|s3))*$ ]] || die "--hardware needs a list of c3, c6, s3 ($(usage))"
        HARDWARE="$2"
        shift
        ;;
      --harness)
        [[ -n "${2:-}" ]] || die "--harness needs a directory ($(usage))"
        HARNESS_SRC="$2"
        shift
        ;;
      --ci-timeout)
        [[ "${2:-}" =~ ^[0-9]+$ ]] || die "--ci-timeout needs whole minutes ($(usage))"
        CI_TIMEOUT="$2"
        shift
        ;;
      -h|--help) usage; exit 0 ;;
      -*) die "unknown option $1 ($(usage))" ;;
      *)
        if [[ -z "${VERSION}" ]]; then
          VERSION="$1"
        elif [[ -z "${MODULE}" ]]; then
          MODULE="$1"
        else
          die "one version and one module only ($(usage))"
        fi
        ;;
    esac
    shift
  done
  [[ -n "${VERSION}" ]] || die "no version given ($(usage))"
  [[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] \
    || die "version must be X.Y.Z with no 'v' (the ecosystem's tags are un-prefixed): ${VERSION}"
  if [[ "${PREPARE}" == 1 ]]; then
    [[ "${ALL}" == 1 && -z "${MODULE}" && "${SET}" == 0 ]] \
      || die "--prepare takes --all and no module or --set: one shared run prepares every module and the set ($(usage))"
    [[ "${SKIP_CI}" == 0 ]] || die "--skip-ci does not apply to --prepare"
  else
    [[ "${ALL}" == 0 ]] || die "--all applies to --prepare only; tag runs are one module or --set ($(usage))"
    [[ "${#RESUME_ARGS[@]}" -eq 0 ]] || die "--resume applies to --prepare only (the tag run resumes by itself)"
    [[ -z "${HARDWARE}" && -z "${HARNESS_SRC}" ]] || die "--hardware and --harness apply to --prepare only"
    [[ "${SET}" == 1 && -n "${MODULE}" ]] && die "a module or --set, not both ($(usage))"
    [[ "${SET}" == 1 || -n "${MODULE}" ]] || die "name a module or --set ($(usage))"
  fi

  # Repo root: the top of the git work tree this is run in.
  command -v git >/dev/null 2>&1 || die "git is required"
  ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git work tree"
  # a relative --harness is taken from where the script was started
  if [[ -n "${HARNESS_SRC}" && "${HARNESS_SRC}" != /* ]]; then HARNESS_SRC="${PWD}/${HARNESS_SRC}"; fi
  cd "${ROOT}"
  [[ -f tools/validate.py && -d modules ]] || die "${ROOT} is not the xewe-os-modules repo"
  if [[ -n "${MODULE}" ]]; then
    [[ "${MODULE}" =~ ^[a-z0-9][a-z0-9-]*$ && -f "modules/${MODULE}/module.properties" ]] \
      || die "no module ${MODULE}: modules/${MODULE}/module.properties is missing"
  fi
  [[ "${DRY_RUN}" == 1 ]] && echo "== dry run: nothing is changed; commands are printed with a leading +"
  export XEWE_NO_BOARD=1 # only a --hardware chip's flash and board steps may look for a board

  if [[ "${PREPARE}" == 1 ]]; then
    prepare "${VERSION}"
    return
  fi
  [[ "${SKIP_CI}" == 1 ]] && warn "!!!!!!!! --skip-ci: the CI gate is skipped (emergencies only) !!!!!!!!"

  local tag="${VERSION}" title="${TITLE} ${VERSION}" dir="releases/${VERSION}"
  if [[ -n "${MODULE}" ]]; then
    tag="${MODULE}/${VERSION}"; title="${MODULE} ${VERSION}"; dir="modules/${MODULE}/releases/${VERSION}"
  fi
  local notes="${dir}/notes.txt" card="${dir}/release_card.md" logs="releases/${VERSION}/logs"

  stage 1 preflight
  GH_OK=0
  if ! command -v gh >/dev/null 2>&1; then
    fail "gh is not installed (https://cli.github.com)"
  elif ! gh auth status >/dev/null 2>&1; then
    fail "gh is not logged in (gh auth login)"
  else
    GH_OK=1
  fi
  run git fetch --quiet origin

  # resume: the tag exists (here or on origin) but there is no GitHub Release yet.
  local resume=0 tagged="" problems
  if tag_local "${tag}" || tag_remote "${tag}"; then
    [[ "${GH_OK}" == 1 ]] || die "tag ${tag} exists; gh is needed to check for its GitHub Release"
    if gh release view "${tag}" >/dev/null 2>&1; then
      die "${tag} is already released (tag and GitHub Release exist)"
    fi
    resume=1
    tagged="$(tag_commit "${tag}")"
    git cat-file -e "${tagged}^{commit}" 2>/dev/null \
      || die "tag ${tag} points at ${tagged:0:12}, which is not in this clone (git fetch --tags origin)"
    if [[ -n "${MODULE}" ]]; then
      [[ "$(module_version "${MODULE}" "${tagged}")" == "${VERSION}" ]] \
        || die "tag ${tag} points at ${tagged:0:12}, whose modules/${MODULE}/module.properties does not say version=${VERSION}"
    else
      problems="$(set_problems "${VERSION}" "${tagged}")"
      [[ -z "${problems}" ]] || die "tag ${tag} points at ${tagged:0:12}, which is not a complete set:"$'\n'"${problems}"
    fi
    if ! git cat-file -e "${tagged}:${notes}" 2>/dev/null || ! git cat-file -e "${tagged}:${card}" 2>/dev/null; then
      die "tag ${tag} points at ${tagged:0:12}, which lacks ${notes} or ${card}"
    fi
  fi

  local head
  head="$(git rev-parse HEAD)"
  # A fresh release needs a clean main at origin's head; a resume only needs the tagged commit.
  if [[ "${resume}" == 0 ]]; then
    [[ -s "${notes}" ]] || fail "${notes} is missing (the release notes; scripts/release.sh --prepare ${VERSION} --all)"
    [[ -s "${card}" ]] || fail "${card} is missing (the release card; scripts/release.sh --prepare ${VERSION} --all)"
    git ls-files --error-unmatch -- "${notes}" "${card}" >/dev/null 2>&1 \
      || fail "${notes} and ${card} are not both committed (the prepare commit)"
    if [[ -s "${notes}" ]] && grep -q -- "${NOTES_MARKER}" "${notes}"; then
      fail "${notes} is still the skeleton (its ${NOTES_MARKER} line)"
    fi
    [[ ! -e "${dir}/testcard.md" ]] || fail "${dir}/testcard.md exists: the release card replaces it; delete it"
    [[ -z "$(git status --porcelain)" ]] || fail "the working tree is not clean (git status)"
    local branch remote
    branch="$(git symbolic-ref --quiet --short HEAD || echo "(detached)")"
    [[ "${branch}" == main ]] || fail "on branch ${branch}, not main"
    remote="$(git ls-remote origin refs/heads/main | cut -f1)"
    if [[ -z "${remote}" ]]; then
      fail "origin has no main branch"
    elif [[ "${head}" != "${remote}" ]]; then
      fail "main (${head:0:12}) is not level with origin/main (${remote:0:12}): pull or push first"
    fi
    if [[ -n "${MODULE}" ]]; then
      local have
      have="$(module_version "${MODULE}" "")"
      [[ "${have}" == "${VERSION}" ]] \
        || fail "modules/${MODULE}/module.properties says version=${have} (scripts/release.sh --prepare ${VERSION} --all)"
    else
      problems="$(set_problems "${VERSION}" "${head}")"
      [[ -z "${problems}" ]] || fail "not every module is released at ${VERSION} on HEAD:"$'\n'"${problems}"
    fi
  fi
  [[ "${FAILED}" -eq 0 ]] && echo "   ok: ${tag} on ${head:0:12}"

  local upload_notes="${notes}"
  if [[ "${resume}" == 1 ]]; then
    echo "== resuming at stage 4: tag ${tag} exists (${tagged:0:12}), no GitHub Release yet"
    # the release files are those of the tagged commit, whatever is checked out now
    UPLOAD_DIR="$(mktemp -d)"
    trap cleanup EXIT
    git show "${tagged}:${notes}" > "${UPLOAD_DIR}/notes.txt"
    git show "${tagged}:${card}" > "${UPLOAD_DIR}/release_card.md"
    upload_notes="${UPLOAD_DIR}/notes.txt"
    card="${UPLOAD_DIR}/release_card.md"
  else
    stage 2 "release card"
    # verify is read-only: it runs in a dry run too, with the card tools of --prepare; the local
    # logs are the shared run's, in releases/<version>/logs/
    if [[ -x "${VENVS}/card/bin/python" ]]; then
      XEWE=("${VENVS}/card/bin/python" -m xewe)
    elif [[ "${DRY_RUN}" == 1 ]]; then
      XEWE=()
    else
      local py
      py="$(find_python)"
      [[ -n "${py}" ]] || die "Python >= 3.11 is required"
      TOOLS_REF="$(tools_ref)"
      [[ -n "${TOOLS_REF}" ]] || die "${CI_FILE} does not call xewe-modules-tests.yml@<ref>"
      card_tools "${py}"
    fi
    if [[ "${#XEWE[@]}" -gt 0 ]]; then
      echo "+ ${XEWE[*]} release-card verify --card ${card} --dir ${logs}"
      "${XEWE[@]}" release-card verify --card "${card}" --dir "${logs}" \
        || fail "release-card verify failed on ${card} (reasons above)"
    else
      echo "+ ${VENVS}/card/bin/python -m xewe release-card verify --card ${card} --dir ${logs}"
      echo "   (not run: no card tools in ${VENVS}/card; --prepare ${VERSION} --all creates them, a real run too)"
    fi

    stage 3 "CI on HEAD"
    ci_gate "${head}"
  fi

  stage 4 "tag and GitHub Release"
  if [[ "${resume}" == 0 ]]; then
    run git tag -a "${tag}" -m "${TITLE} ${tag}" "${head}"
  fi
  tag_remote "${tag}" || run git push origin "refs/tags/${tag}"
  if [[ -n "${MODULE}" ]]; then
    run gh release create "${tag}" --verify-tag --latest=false --title "${title}" --notes-file "${upload_notes}" "${card}"
  else
    # The set's notes: releases/<version>/notes.txt, then one line per module release.
    local body m
    body="$(cat "${upload_notes}"; printf '\nMODULE RELEASES\n\n'
      for m in modules/*/; do
        m="$(basename "${m}")"
        [[ -f "modules/${m}/module.properties" ]] || continue
        echo "- ${m} ${VERSION}: https://github.com/${REPO}/releases/tag/${m}/${VERSION}"
      done)"
    if [[ "${DRY_RUN}" == 1 ]]; then
      SET_NOTES="<set notes>"
      echo "   <set notes>:"; while IFS= read -r m; do echo "   | ${m}"; done <<< "${body}"
    else
      SET_NOTES="$(mktemp)"
      SET_NOTES_TMP="${SET_NOTES}"
      trap cleanup EXIT
      printf '%s\n' "${body}" > "${SET_NOTES}"
    fi
    run gh release create "${tag}" --verify-tag --title "${title}" --notes-file "${SET_NOTES}" "${card}"
  fi

  stage 5 consumers
  if [[ -n "${MODULE}" ]]; then
    echo "Projects get ${MODULE} ${VERSION} through the set tag: once every module is released at ${VERSION},"
    echo "scripts/release.sh ${VERSION} --set; then xewe.toml [modules] ref = \"${VERSION}\" and ./setup.sh."
  else
    echo "Projects: xewe.toml [modules] ref = \"${VERSION}\" (or xewe manifest update), then ./setup.sh."
  fi
  if [[ "${DRY_RUN}" == 1 && "${FAILED}" -gt 0 ]]; then
    echo "== dry run: ${FAILED} check(s) failed; a real run stops at the first"
    exit 1
  fi
}

# Sourcing the file (tests) defines the functions without running a release.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
