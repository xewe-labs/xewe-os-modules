#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
# xewe-os-modules/scripts/release.sh
#
# release.sh: test the named modules (or every module) once, tag the commit with the notes and one
# release card per tag in the tag message, then create the GitHub Releases.
# Usage: scripts/release.sh <version> (<module>... | --all) [--hardware c3] [--notes "line" ...] [--notes-file FILE] [--resume] [--ci-timeout <min>]
#
# One command, 6 stages. <version> is X.Y.Z or X.Y.Z-suffix (a pre-release), no `v`; the version
# lines get X.Y.Z. Each named module is released as tag <module>/<version> (e.g. wifi/3.1.7);
# --all releases every module and also the set tag <version>, which marks a commit where every module
# is released at <version>. Projects pin either (xewe.toml [modules] ref). The notes are 1 to 3 lines
# and apply to every tag of the run: --notes once per line, and/or the lines of --notes-file. Run
# the script once per module when the modules do not share one reason. Everything the run writes
# is under build/release/<version>/ (gitignored).
#   1 preflight: refuses, and lists every problem, unless: on main; the work tree has no change at
#     all (untracked files included); main is level with origin/main after a fetch; every tag of
#     the run is free here and on origin (--resume excepted); tests.yml calls the tools workflow at
#     the ref of its tools_ref input (a tools tag, unless XEWE_TOOLS_SOURCE is set); the notes pass
#     `xewe release-notes check`; build/ is ignored; Python >= 3.11, the harness project (an xewe
#     project: XEWE_RELEASE_HARNESS, else ../xewe-os) and gh (logged in) are there;
#   2 tests: the version lines set (module.properties version= and requires_core= of the named
#     modules, the matching cells of doc/modules.md via tools/validate.py --write-index), then a
#     card run (`xewe release-card init` on the logs folder), every step recorded by `xewe
#     release-card record`: the checkers' venv (ruff, clang-format) and two copies of the harness
#     project with every module selected and this tree as the modules source
#     (XEWE_MODULES_SOURCE; one for the tests and the all-modules image, one for module-alone
#     builds), tools/validate.py --harness, `xewe format --check modules tools`, `xewe brand-lint .`,
#     for each named module and every chip (c3, c6, s3) `xewe modules select <module>` and `xewe
#     build --chip C`, then `xewe test --unit-only` (unit, JUnit), the all-modules build for the
#     chip and its board tests. A failing step stops the run before the cards, the commit and the
#     tags (the log is in the logs folder). The chip
#     is the --hardware chip, else c3: a --hardware chip is flashed and tested with
#     --require-board and the gates in BOARD_GATES; without it the board tests are compiled, not
#     run. XEWE_RELEASE_DEFINES_<CHIP> (environment, KEY=VALUE separated by spaces) are the defines
#     of the build, flash and board steps. XEWE_ENV, else the harness's dotenv file, else the
#     tools checkout's is the dotenv the board run uses: the script never reads it, the tools do;
#   3 release cards: `xewe release-card render` writes build/release/<version>/<module>/
#     release_card.md for each named module (only that module's tests) and, with --all,
#     build/release/<version>/_set/release_card.md (every module's tests), or refuses (the draft
#     is in the logs folder);
#   4 commit: only the version-line files are added, `release <version>` with --all, else
#     `release <version> <module>...` (`<n> modules` when there are many); refuses when any other
#     file changed;
#   5 tag: per tag the message is the notes, one blank line, that tag's card; `git tag -a
#     --cleanup=verbatim` (git would drop the card's `#` lines otherwise). Then `git push origin
#     main`, a check that origin/main equals HEAD, and one `git push origin <tag>` per tag (GitHub
#     starts no workflow when more than 3 tags arrive in one push): main first, so that the
#     workflow is registered before a tag triggers it;
#   6 release: per tag waits for its tests.yml run (--ci-timeout minutes in all, default 60); its
#     card job compares the tag's card with the run (the `xewe release-card verify --ci` step must
#     have run and passed). Then `gh release create` (--verify-tag, the notes as the text, the
#     card as its only asset release_card.md; --prerelease for a suffix); prints the release URLs.
#     The modules CI runs on tags only when tests.yml triggers on them (`push: tags`); a tag
#     without a run is reported as such when the wait ends.
# A run that stops before stage 4 puts the version lines back. --resume: tags that exist (here or on
# origin) are not made again; the missing ones are made on the same commit. When HEAD is the
# release commit and no tag exists, build/release/<version>/ is reused if every card is there (else
# stage 2 runs again), then the run goes on from stage 4. A release that exists is only reported;
# a missing one is created from its tag's own message.
# Exit codes: 0 done, 1 a refusal or a failed check.
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
HARNESS="build/release-harness/project"             # harness copy: tests and the all-modules image
EXPORT="build/release-venv/xewe-os-modules"          # the tree as a commit of it would hold it
REQUIRE="setup,validate,unit,build,board"
SET_SLUG="_set"                                      # the set's folder under build/release/<version>/
CHIPS=(c3 c6 s3)
ALONE="build/release-harness/alone"                  # harness copy: module-alone builds
CHECKERS=("ruff==0.17.0" "clang-format==23.1.3")    # xewe-modules-tests.yml format job pins
# the gated board tests the card session runs: all gates in one session
BOARD_GATES=(XEWE_TEST_LED_RESET=1 XEWE_TEST_LED_WEB_HTTP=1 XEWE_TEST_WEB_INTERFACE_HTTP=1 XEWE_TEST_WEB_INTERFACE_RESET=1)

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }
STAGES=6
stage() { echo "== $1/${STAGES} $2"; }

# A failed preflight check: noted, and all of them are listed before the run stops.
PROBLEMS=()
fail() { PROBLEMS+=("$*"); }
check_problems() {
  [[ "${#PROBLEMS[@]}" -eq 0 ]] && return 0
  local p
  echo "error: the release cannot start:" >&2
  for p in "${PROBLEMS[@]}"; do echo "  - ${p}" >&2; done
  exit 1
}

# Prints a command and runs it.
run() {
  printf '+'; printf ' %q' "$@"; printf '\n'
  "$@"
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

# MODULES: the modules of the run (every module with --all).
MODULES=()

# in_run MODULE: true when MODULE is one of MODULES.
in_run() {
  local m
  for m in "${MODULES[@]}"; do [[ "${m}" == "$1" ]] && return 0; done
  return 1
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

# versioned_index VERSION REF: doc/modules.md at REF with the Version and Requires core cells of
# every module of the run set for VERSION (what `tools/validate.py --write-index` writes after
# stage 2's version lines).
versioned_index() {
  file_at "$2" doc/modules.md | awk -v ver="$1" -v req="\`$(core_range "$1")\` |" \
    -v mods=" ${MODULES[*]} " '
    BEGIN { FS = " \\| "; OFS = " | " }
    /^\| \[`/ && NF == 7 {
      slug = $1; sub(/^\| \[`/, "", slug); sub(/`.*/, "", slug)
      if (index(mods, " " slug " ")) { $4 = ver; $7 = req }
    }
    { print }'
}

# allowed_change PATH VERSION: true for a path stage 4 may find changed: the module.properties of a
# module of the run whose only change from HEAD is its version lines, and doc/modules.md whose only
# change is those modules' cells.
allowed_change() {
  local p slug
  if [[ "$1" == modules/*/module.properties ]]; then
    p="${1#modules/}"; slug="${p%%/*}"
    in_run "${slug}" || return 1
    [[ -f "$1" ]] && git cat-file -e "HEAD:$1" 2>/dev/null && cmp -s "$1" <(versioned_props "${slug}" "$2" HEAD)
    return
  fi
  if [[ "$1" == doc/modules.md ]]; then
    [[ -f "$1" ]] && git cat-file -e "HEAD:$1" 2>/dev/null && cmp -s "$1" <(versioned_index "$2" HEAD)
    return
  fi
  return 1
}

# version_files: the files the release commit holds: the module.properties of the run and doc/modules.md.
version_files() {
  local m
  for m in "${MODULES[@]}"; do echo "modules/${m}/module.properties"; done
  echo doc/modules.md
}

# tree_problems: one line per reason the work tree cannot be released from; none when it can.
tree_problems() {
  local p path unmerged dirty
  git symbolic-ref -q HEAD >/dev/null || echo "HEAD is detached: check out main"
  for p in MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD REBASE_HEAD rebase-merge rebase-apply; do
    path="$(git rev-parse --git-path "${p}")"
    [[ -e "${path}" ]] && echo "a merge, rebase, cherry-pick or revert is in progress (${path} exists): finish or abort it first"
  done
  unmerged="$(git diff --name-only --diff-filter=U)"
  [[ -z "${unmerged}" ]] || echo "unmerged (conflicted) paths: $(tr '\n' ' ' <<< "${unmerged}")"
  dirty="$(git status --porcelain=v1 --untracked-files=all | cut -c4- | head -n 10 | tr '\n' ' ')"
  [[ -z "${dirty}" ]] || echo "the work tree is not clean (commit, stash or remove): ${dirty% }"
  return 0
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

# ci_run SHA REF: the newest run of CI_WORKFLOW for SHA on REF (the tag name for a tag push), as
# "id status url conclusion" (empty when there is none yet; conclusion is empty while the run is
# not completed).
ci_run() {
  gh run list --commit "$1" --workflow "${CI_WORKFLOW}" --branch "$2" --limit 20 \
    --json databaseId,status,url,conclusion \
    --jq '.[0] | select(. != null) | "\(.databaseId) \(.status) \(.url) \(.conclusion)"'
}

# card_step_result RUN_ID: the conclusion of the card job's verify step in that run (skipped when
# the job found no card).
card_step_result() {
  gh run view "$1" --json jobs \
    --jq ".jobs[] | select(.name == \"${CI_CARD_JOB}\") | .steps[] | select(.name == \"${CI_CARD_STEP}\") | .conclusion"
}

# ci_gate TAG SHA: wait for CI_WORKFLOW on SHA and TAG; 0 when it succeeded (CI_RUN_ID is its id),
# else 1 with the reason on stderr. CI_DEADLINE (seconds) bounds the whole stage; every tag is
# polled at least once.
CI_RUN_ID=""
CI_DEADLINE=0
ci_gate() {
  local tag="$1" sha="$2" line id status url conclusion pid
  CI_RUN_ID=""
  echo "   ${tag}: waiting for a ${CI_WORKFLOW} run on ${sha:0:12} (timeout ${CI_TIMEOUT} min in all)"
  while :; do
    line="$(ci_run "${sha}" "${tag}")" || { echo "error: ${tag}: gh run list failed" >&2; return 1; }
    [[ -n "${line}" ]] && break
    if (( SECONDS >= CI_DEADLINE )); then
      echo "error: ${tag}: no ${CI_WORKFLOW} run for this tag within ${CI_TIMEOUT} min; ${CI_FILE} must trigger on tags (push: tags) for the modules CI to run on a tag. Re-run with --resume once it does" >&2
      return 1
    fi
    sleep "${CI_POLL}"
  done
  read -r id status url conclusion <<< "${line}"
  echo "   ${tag}: run ${url} (${status})"
  if [[ "${status}" != completed ]]; then
    gh run watch "${id}" --exit-status --interval "${CI_POLL}" >/dev/null &
    pid=$!
    while kill -0 "${pid}" 2>/dev/null; do
      if (( SECONDS >= CI_DEADLINE )); then
        kill "${pid}" 2>/dev/null || true
        echo "error: ${tag}: timed out after ${CI_TIMEOUT} min, CI still running: ${url} (re-run with --resume)" >&2
        return 1
      fi
      sleep 2
    done
    wait "${pid}" || { echo "error: ${tag}: CI failed: ${url}" >&2; return 1; }
    conclusion=success
  fi
  [[ "${conclusion}" == success ]] || { echo "error: ${tag}: CI ${conclusion:-did not succeed}: ${url}" >&2; return 1; }
  CI_RUN_ID="${id}"
  echo "   ${tag}: CI passed: ${url}"
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
  if [[ -n "${XEWE_TOOLS_SOURCE:-}" ]]; then
    commit="$(git -C "${XEWE_TOOLS_SOURCE}" rev-parse HEAD 2>/dev/null || echo -)"
  else
    commit="$(git ls-remote "${TOOLS_URL}" "refs/tags/${TOOLS_REF}^{}" "refs/tags/${TOOLS_REF}" "refs/heads/${TOOLS_REF}" \
      | awk '{ c[NR] = $1 } END { print (NR ? c[1] : "-") }')"
  fi
  echo "${commit:--}" > "${VENVS}/card/xewe-tools-commit"
}

# step LABEL RECORD_ARGS...: one recorded step; a failed command is noted and the run goes on (the
# cards refuse it), a usage error stops.
FAILED_STEPS=()
step() {
  local label="$1" rc=0
  shift
  "${XEWE[@]}" release-card record --dir "${LOGS}" "$@" </dev/null || rc=$?
  case "${rc}" in
    0) ;;
    1|3) FAILED_STEPS+=("${label}"); warn "${label} failed (exit ${rc}; the log is in ${LOGS}); going on" ;;
    *) die "xewe release-card record exited ${rc} on ${label}" ;;
  esac
}

# stop_on_failed: a failed step stops the run (nothing is committed or tagged).
stop_on_failed() {
  [[ "${#FAILED_STEPS[@]}" -eq 0 ]] || die "failed steps: ${FAILED_STEPS[*]} (the logs are in ${LOGS}); nothing was committed. Fix the cause, then run again"
}

# tree_export: EXPORT = the files a commit of this work tree would hold (tracked, plus untracked
# files that are not ignored; deleted ones left out), so nothing of build/: tools/validate.py runs
# on it (it reports every .ino and module.properties under its repo as stray, and the harness copy
# holds sketches and libraries) and the harness uses it as the modules source, as CI's checkout.
tree_export() {
  rm -rf "${EXPORT}"
  mkdir -p "${EXPORT}"
  git ls-files -z --cached --others --exclude-standard \
    | while IFS= read -r -d '' f; do if [[ -e "${f}" || -L "${f}" ]]; then printf '%s\0' "${f}"; fi; done \
    | tar --null -T - -cf - | tar -C "${EXPORT}" -xf -
}

# harness_copy DEST: (re)create a harness project at DEST from HARNESS_SRC: the project without
# .git, build/, releases/, static/firmware/, its tests/ (the cards hold this repo's tests only) and
# dotenv files; [tools] ref set to TOOLS_REF when that is a tag (XEWE_TOOLS_SOURCE wins in setup.sh).
harness_copy() {
  local dest="$1"
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

# write_notes: the notes lines into NOTES_FILE.
write_notes() {
  mkdir -p "${OUT}"
  if [[ "${#NOTES[@]}" -gt 0 ]]; then printf '%s\n' "${NOTES[@]}" > "${NOTES_FILE}"; else : > "${NOTES_FILE}"; fi
}

# notes_problems: one line per problem of the notes (none when `xewe release-notes check` accepts them).
notes_problems() {
  local out line
  if [[ "${#NOTES[@]}" -eq 0 ]]; then
    echo "no release notes: give 1 to 3 lines with --notes \"line\" (repeated) or --notes-file FILE"
    return 0
  fi
  out="$("${XEWE[@]}" release-notes check "${NOTES_FILE}" 2>&1)" || {
    while IFS= read -r line; do [[ -z "${line}" ]] || echo "release notes: ${line}"; done <<< "${out}"
  }
}

# restore_versions: put the version files back to HEAD when a run stops before the commit.
VERSIONS_WRITTEN=0
VFILES=()
restore_versions() {
  if [[ "${VERSIONS_WRITTEN}" == 1 ]]; then
    VERSIONS_WRITTEN=0
    git checkout -q -- "${VFILES[@]}" 2>/dev/null || true
    echo "   version lines put back" >&2
  fi
}

# tests VERSION: stage 2 (see the header).
tests() {
  local v="$1" m file x hp ahp py chip hw fw_bin
  local changed=0 defines=()
  stage 2 tests
  for m in "${MODULES[@]}"; do
    file="modules/${m}/module.properties"
    versioned_props "${m}" "${BASE}" "" >/dev/null || die "${file}: expected exactly one version= and one requires_core= line"
    if ! cmp -s "${file}" <(versioned_props "${m}" "${BASE}" ""); then
      [[ "${MODE}" == fresh ]] && VERSIONS_WRITTEN=1
      versioned_props "${m}" "${BASE}" "" > "${file}.tmp"
      cat "${file}.tmp" > "${file}" # cat keeps the file's mode
      rm -f "${file}.tmp"
      changed=$((changed + 1))
    fi
  done
  [[ "${MODE}" == fresh ]] && VERSIONS_WRITTEN=1
  echo "   version ${BASE}, requires_core $(core_range "${BASE}"): ${changed} of ${#MODULES[@]} module.properties changed"
  run "${VENVS}/card/bin/python" tools/validate.py --write-index
  run "${XEWE[@]}" release-card init --dir "${LOGS}" --repo "${REPO}" --version "${v}" --tag "${v}"

  # setup.sh installs the tools of [tools] (or XEWE_TOOLS_SOURCE), the shared toolchain, every
  # module's libraries and the modules from this tree (XEWE_MODULES_SOURCE: the export, used in place)
  tree_export
  harness_copy "${HARNESS}"
  harness_copy "${ALONE}"
  # record --kind setup keeps build/config/build_config.toml of the repo root: the harness's (pins)
  run mkdir -p build/config
  run ln -sfn "../../${HARNESS}/build/config/build_config.toml" build/config/build_config.toml
  py="$(find_python)"
  step venv --kind setup --name venv -- "${py}" -m venv --clear "${VENVS}/py"
  step install --kind setup --name install -- "${VENVS}/py/bin/python" -m pip install --disable-pip-version-check \
    --progress-bar off "$(tools_spec)" "${CHECKERS[@]}"
  for x in "${HARNESS}" "${ALONE}"; do
    step "setup ${x}" --kind setup --name "harness-${x##*/}" -- env -u XEWE_PROJECT "XEWE_MODULES_SOURCE=${ROOT}/${EXPORT}" \
      XEWE_NO_BOARD=1 bash "${x}/setup.sh" --modules all
  done
  hp="${HARNESS}/build/tools/.venv/bin/python"
  ahp="${ALONE}/build/tools/.venv/bin/python"

  step validate --kind validate --name validate -- "${hp}" "${EXPORT}/tools/validate.py" --harness "${HARNESS}"
  step format --kind check --name format -- env "XEWE_CLANG_FORMAT=${VENVS}/py/bin/clang-format" \
    "${VENVS}/py/bin/python" -m xewe format --check modules tools
  step brand-lint --kind check --name brand-lint -- "${VENVS}/py/bin/python" -m xewe brand-lint .
  # each named module with only what it depends on; the build steps are named <n>-build-<chip>-<module>
  for chip in "${CHIPS[@]}"; do
    for m in "${MODULES[@]}"; do
      # shellcheck disable=SC2016 # expanded by the inner bash
      step "build ${m} ${chip}" --kind build --chip "${chip}" --name "${m}" \
        --meta "${ALONE}/build/builds/${chip}/out/meta.json" -- env XEWE_NO_BOARD=1 bash -c \
        'set -euo pipefail; "$0" -m xewe --project "$1" modules select "$2"; "$0" -m xewe --project "$1" build --chip "$3"' \
        "${ahp}" "${ALONE}" "${m}" "${chip}"
    done
  done
  stop_on_failed
  # the command of xewe-modules-tests.yml's unit step, so the card's unit rows are CI's
  step unit --kind unit --name unit --junit "${OUT}/junit-unit.xml" -- env XEWE_NO_BOARD=1 "${hp}" -m xewe \
    --project "${HARNESS}" test --unit-only -- "--junitxml=${ROOT}/${OUT}/junit-unit.xml" -o junit_family=xunit1 \
    -o junit_logging=no -ra

  chip="${RUN_CHIP}"
  hw=0
  [[ -z "${HARDWARE}" ]] || hw=1
  while IFS= read -r x; do defines+=("${x}"); done < <(chip_defines "${chip}")
  if [[ "${hw}" == 1 ]]; then
    step "build ${chip}" --kind build --chip "${chip}" --name all-modules \
      --meta "${HARNESS}/build/builds/${chip}/out/meta.json" -- env -u XEWE_NO_BOARD "${hp}" -m xewe \
      --project "${HARNESS}" build --chip "${chip}" ${defines[@]+"${defines[@]}"}
    fw_bin="$(find "${HARNESS}/build/builds/${chip}/out" -maxdepth 1 -name "*-${chip}-*.bin" ! -name '*bootloader*' ! -name '*partitions*' 2>/dev/null | head -n 1)"
    [[ -n "${fw_bin}" ]] || fw_bin="${HARNESS}/build/builds/${chip}/out/missing.bin"
    step "flash ${chip}" --kind flash --chip "${chip}" --firmware "${fw_bin}" --meta "${HARNESS}/build/builds/${chip}/out/meta.json" \
      -- env -u XEWE_NO_BOARD "${hp}" -m xewe --project "${HARNESS}" flash --chip "${chip}" --no-build --require-board \
      ${defines[@]+"${defines[@]}"}
    step "board ${chip}" --kind board --chip "${chip}" --junit "${OUT}/junit-board-${chip}.xml" \
      -- env -u XEWE_NO_BOARD "${BOARD_GATES[@]}" "${hp}" -m xewe --project "${HARNESS}" test --chip "${chip}" \
      --require-board ${defines[@]+"${defines[@]}"} -- -m board -o junit_family=xunit1 -ra \
      "--junitxml=${ROOT}/${OUT}/junit-board-${chip}.xml"
  else
    step "build ${chip}" --kind build --chip "${chip}" --name all-modules \
      --meta "${HARNESS}/build/builds/${chip}/out/meta.json" -- env XEWE_NO_BOARD=1 "${hp}" -m xewe \
      --project "${HARNESS}" build --chip "${chip}" ${defines[@]+"${defines[@]}"}
    step "board ${chip}" --kind board --chip "${chip}" --junit "${OUT}/junit-board-${chip}.xml" \
      -- env XEWE_NO_BOARD=1 "${BOARD_GATES[@]}" "${hp}" -m xewe --project "${HARNESS}" test --chip "${chip}" \
      ${defines[@]+"${defines[@]}"} -- -m board -o junit_family=xunit1 -ra \
      "--junitxml=${ROOT}/${OUT}/junit-board-${chip}.xml"
  fi
  stop_on_failed
}

# card_stage: stage 3 (see the header).
card_stage() {
  local i slug rc refused=() tests_prefix title
  stage 3 "release cards"
  for i in "${!SLUGS[@]}"; do
    slug="${SLUGS[i]}"
    if [[ "${slug}" == "${SET_SLUG}" ]]; then
      tests_prefix="modules/"; title="${TITLE}"
    else
      tests_prefix="modules/${slug}/"; title="${slug}"
    fi
    mkdir -p "${OUT}/${slug}"
    rc=0
    run "${XEWE[@]}" release-card render --dir "${LOGS}" --notes "${NOTES_FILE}" --out "${OUT}/${slug}/release_card.md" \
      --require "${REQUIRE}" --chips "${RUN_CHIP}" ${HARDWARE:+--hardware "${HARDWARE}"} \
      --title "${title}" --tests "${tests_prefix}" || rc=$?
    case "${rc}" in
      0) [[ "$(head -n 1 "${OUT}/${slug}/release_card.md")" == "# "* ]] || die "${OUT}/${slug}/release_card.md does not start with a '# ' title line" ;;
      1) refused+=("${TAGS[i]}") ;;
      *) die "xewe release-card render exited ${rc}" ;;
    esac
  done
  [[ "${#refused[@]}" -eq 0 ]] \
    || die "no card for ${refused[*]} (reasons above; draft of the last refusal: ${LOGS}/release_card.draft.md). Fix the cause, then run again"
}

# commit_message: the release commit's subject: the version, and the modules unless --all.
commit_message() {
  if [[ "${ALL}" == 1 ]]; then
    echo "release ${VERSION}"
  elif [[ "${#MODULES[@]}" -gt 4 ]]; then
    echo "release ${VERSION} ${#MODULES[@]} modules"
  else
    echo "release ${VERSION} ${MODULES[*]}"
  fi
}

# commit_stage: stage 4 (see the header).
commit_stage() {
  local entry path stray=()
  stage 4 commit
  if [[ "${MODE}" != fresh ]]; then
    echo "   HEAD is already the release commit"
    return 0
  fi
  while IFS= read -r -d '' entry; do
    path="${entry:3}"
    allowed_change "${path}" "${BASE}" || stray+=("${path}")
  done < <(git status --porcelain=v1 -z --untracked-files=all)
  [[ "${#stray[@]}" -eq 0 ]] || die "files other than the version lines changed (${stray[*]}): nothing was committed"
  run git add -- "${VFILES[@]}"
  run git commit -q --allow-empty -m "$(commit_message)"
  VERSIONS_WRITTEN=0
}

# push_main: push main and check that origin/main is HEAD (main first: the workflow is registered
# before a tag triggers it).
push_main() {
  run git push origin main
  [[ "$(git rev-parse origin/main)" == "$(git rev-parse HEAD)" ]] \
    || die "origin/main ($(git rev-parse --short=12 origin/main)) is not HEAD ($(git rev-parse --short=12 HEAD)) after the push"
}

# tag_stage COMMIT: stage 5 (see the header): the tags that do not exist are made on COMMIT.
tag_stage() {
  local c="$1" i tag slug
  stage 5 tag
  for i in "${!TAGS[@]}"; do
    tag="${TAGS[i]}"; slug="${SLUGS[i]}"
    if tag_local "${tag}"; then
      echo "   ${tag} exists"
      continue
    fi
    { cat "${NOTES_FILE}"; echo; cat "${OUT}/${slug}/release_card.md"; } > "${OUT}/${slug}/tag_message.txt"
    cp "${NOTES_FILE}" "${OUT}/${slug}/notes.txt"
    run git tag -a --cleanup=verbatim -F "${OUT}/${slug}/tag_message.txt" "${tag}" "${c}"
  done
  if ! git merge-base --is-ancestor "${c}" refs/remotes/origin/main 2>/dev/null; then
    if [[ "$(git symbolic-ref --quiet --short HEAD || true)" != main ]] || ! git merge-base --is-ancestor "${c}" HEAD; then
      die "origin/main lacks the tagged commit ${c:0:12} and this clone's main does not hold it: push main first"
    fi
    push_main
  fi
  # one push per tag: GitHub starts no workflow when more than 3 tags arrive in one push
  for tag in "${TAGS[@]}"; do
    tag_remote "${tag}" || run git push origin "refs/tags/${tag}"
  done
}

# release_stage: stage 6 (see the header).
release_stage() {
  local i tag slug card url failed=() urls=() pre=()
  [[ -z "${PRE}" ]] || pre=(--prerelease)
  stage 6 "release CI and GitHub Releases"
  CI_DEADLINE=$((SECONDS + CI_TIMEOUT * 60))
  for i in "${!TAGS[@]}"; do
    tag="${TAGS[i]}"; slug="${SLUGS[i]}"
    ci_gate "${tag}" "$1" || { failed+=("${tag}"); continue; }
    card="$(card_step_result "${CI_RUN_ID}")" || { echo "error: ${tag}: gh run view ${CI_RUN_ID} failed" >&2; failed+=("${tag}"); continue; }
    if [[ "${card}" != success ]]; then
      echo "error: ${tag}: CI passed, but the '${CI_CARD_JOB}' job compared no card with this tag (step '${CI_CARD_STEP}': ${card:-not found})" >&2
      failed+=("${tag}")
      continue
    fi
    echo "   ${tag}: card check: ${card}"
    if gh release view "${tag}" >/dev/null 2>&1; then
      echo "   GitHub Release ${tag} exists already"
    else
      if [[ "${slug}" == "${SET_SLUG}" ]]; then
        run gh release create "${tag}" --verify-tag --title "${TITLE} ${tag}" ${pre[@]+"${pre[@]}"} \
          --notes-file "${OUT}/${slug}/notes.txt" "${OUT}/${slug}/release_card.md"
      else
        run gh release create "${tag}" --verify-tag --latest=false --title "${slug} ${VERSION}" ${pre[@]+"${pre[@]}"} \
          --notes-file "${OUT}/${slug}/notes.txt" "${OUT}/${slug}/release_card.md"
      fi
    fi
    url="$(gh release view "${tag}" --json url --jq .url)" || { failed+=("${tag}"); continue; }
    urls+=("${url}")
  done
  for url in ${urls[@]+"${urls[@]}"}; do echo "   GitHub Release: ${url}"; done
  [[ "${#failed[@]}" -eq 0 ]] || die "no release for ${failed[*]} (reasons above; scripts/release.sh ${VERSION} ... --resume continues)"
  echo "Projects: xewe.toml [modules] ref = \"<tag>\" (${TAGS[*]}), then ./setup.sh."
}

usage() { sed -n 's/^# Usage: //p' "${BASH_SOURCE[0]}"; }

main() {
  VERSION=""
  ALL=0
  RESUME=0
  HARDWARE=""
  CI_TIMEOUT=60
  NOTES=()
  XEWE=()
  TAGS=()
  SLUGS=()
  local names=() line file x m n i p
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --all) ALL=1 ;;
      --resume) RESUME=1 ;;
      --hardware)
        [[ "${2:-}" =~ ^(c3|c6|s3)$ ]] || die "--hardware needs one chip: c3, c6 or s3 ($(usage))"
        HARDWARE="$2"
        shift
        ;;
      --notes)
        [[ $# -ge 2 ]] || die "--notes needs a line ($(usage))"
        NOTES+=("$2")
        shift
        ;;
      --notes-file)
        file="${2:-}"
        [[ -f "${file}" ]] || die "--notes-file needs a file: ${file}"
        while IFS= read -r line || [[ -n "${line}" ]]; do NOTES+=("${line}"); done < "${file}"
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
        if [[ -z "${VERSION}" ]]; then VERSION="$1"; else names+=("$1"); fi
        ;;
    esac
    shift
  done
  [[ -n "${VERSION}" ]] || die "no version given ($(usage))"
  [[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] \
    || die "version must be X.Y.Z or X.Y.Z-suffix with no 'v' (the ecosystem's tags are un-prefixed): ${VERSION}"
  BASE="${VERSION%%-*}"
  PRE=""
  [[ "${VERSION}" == "${BASE}" ]] || PRE=1
  if [[ "${ALL}" == 1 ]]; then
    [[ "${#names[@]}" -eq 0 ]] || die "module names or --all, not both ($(usage))"
  else
    [[ "${#names[@]}" -gt 0 ]] || die "name one or more modules, or --all ($(usage))"
  fi

  # Repo root: the top of the git work tree this is run in.
  command -v git >/dev/null 2>&1 || die "git is required"
  ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git work tree"
  cd "${ROOT}"
  [[ -f tools/validate.py && -d modules ]] || die "${ROOT} is not the xewe-os-modules repo"
  if [[ "${ALL}" == 1 ]]; then
    while IFS= read -r m; do MODULES+=("${m}"); done < <(module_names)
    [[ "${#MODULES[@]}" -gt 0 ]] || die "no modules in modules/"
  else
    for m in "${names[@]}"; do
      [[ "${m}" =~ ^[a-z0-9][a-z0-9-]*$ && -f "modules/${m}/module.properties" ]] \
        || die "no module ${m}: modules/${m}/module.properties is missing"
      for n in ${MODULES[@]+"${MODULES[@]}"}; do [[ "${n}" != "${m}" ]] || die "module ${m} is named twice"; done
      MODULES+=("${m}")
    done
  fi
  for m in "${MODULES[@]}"; do TAGS+=("${m}/${VERSION}"); SLUGS+=("${m}"); done
  if [[ "${ALL}" == 1 ]]; then TAGS+=("${VERSION}"); SLUGS+=("${SET_SLUG}"); fi
  while IFS= read -r line; do VFILES+=("${line}"); done < <(version_files)
  export XEWE_NO_BOARD=1 # only a --hardware chip's flash and board steps may look for a board
  OUT="build/release/${VERSION}"
  LOGS="${OUT}/logs"
  NOTES_FILE="${OUT}/notes.txt"
  RUN_CHIP="${HARDWARE:-c3}"
  trap restore_versions EXIT
  trap 'exit 130' INT TERM

  stage 1 preflight
  local head tagged="" existing=() missing=() py
  MODE=fresh
  head="$(git rev-parse HEAD)"
  if ! command -v gh >/dev/null 2>&1; then
    fail "gh is not installed (https://cli.github.com)"
  elif ! gh auth status >/dev/null 2>&1; then
    fail "gh is not logged in (gh auth login)"
  fi
  run git fetch --quiet origin || die "git fetch origin failed"
  for x in "${TAGS[@]}"; do
    if tag_local "${x}" || tag_remote "${x}"; then existing+=("${x}"); else missing+=("${x}"); fi
  done
  if [[ "${#existing[@]}" -gt 0 ]]; then
    if [[ "${RESUME}" == 1 ]]; then MODE=tag; else fail "tag ${existing[*]} exists (here or on origin); --resume continues it"; fi
  elif [[ "${RESUME}" == 1 ]]; then
    if [[ "$(git log -1 --format=%s)" == "$(commit_message)" ]]; then
      MODE=commit
    else
      fail "--resume: no tag of this run exists and HEAD is not the '$(commit_message)' commit: run without --resume"
    fi
  fi

  if [[ "${MODE}" == tag ]]; then
    # the commit every existing tag points at; the notes and the card of those tags are their own
    for x in "${existing[@]}"; do
      tag_local "${x}" || run git fetch --quiet origin "refs/tags/${x}:refs/tags/${x}" || true
      n="$(tag_commit "${x}")"
      if [[ -z "${tagged}" ]]; then
        tagged="${n}"
      elif [[ "${n}" != "${tagged}" ]]; then
        fail "tag ${x} points at ${n:0:12}, not at ${tagged:0:12} like the other tags of this run"
      fi
    done
    if ! git cat-file -e "${tagged}^{commit}" 2>/dev/null; then
      fail "the tagged commit ${tagged:0:12} is not in this clone (git fetch --tags origin)"
    else
      for m in "${MODULES[@]}"; do
        x="$(module_version "${m}" "${tagged}")"
        [[ "${x}" == "${BASE}" ]] || fail "tag commit ${tagged:0:12}: modules/${m}/module.properties says version=${x:-unset}, not ${BASE}"
      done
      mkdir -p "${OUT}"
      for i in "${!TAGS[@]}"; do
        x="${TAGS[i]}"; m="${SLUGS[i]}"
        mkdir -p "${OUT}/${m}"
        if tag_local "${x}"; then
          git for-each-ref --format='%(contents)' "refs/tags/${x}" | sed '/-----BEGIN PGP SIGNATURE-----/,$d' > "${OUT}/${m}/tag_message.txt"
          awk '/^# /{exit} {print}' "${OUT}/${m}/tag_message.txt" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' > "${OUT}/${m}/notes.txt"
          sed -n '/^# /,$p' "${OUT}/${m}/tag_message.txt" > "${OUT}/${m}/release_card.md"
          [[ -s "${OUT}/${m}/release_card.md" ]] || fail "the message of tag ${x} holds no release card (no line starting with '# ')"
        else
          # a missing tag is made from this run's notes and card
          [[ -s "${OUT}/${m}/release_card.md" ]] || fail "tag ${x} is missing and ${OUT}/${m}/release_card.md is not there: run without --resume"
          [[ -s "${NOTES_FILE}" ]] || fail "tag ${x} is missing: give its notes again (--notes or --notes-file)"
        fi
      done
      if [[ "${#missing[@]}" -gt 0 && "${#NOTES[@]}" -gt 0 ]]; then write_notes; fi
    fi
    check_problems
    echo "   ok: resuming, tags ${existing[*]} exist (${tagged:0:12})${missing[*]+; to make: ${missing[*]}}"
  else
    [[ "$(git symbolic-ref --quiet --short HEAD || echo '(detached)')" == main ]] \
      || fail "on branch $(git symbolic-ref --quiet --short HEAD || echo '(detached)'), not main"
    while IFS= read -r x; do [[ -z "${x}" ]] || fail "${x}"; done < <(tree_problems)
    if [[ "${MODE}" == commit ]]; then
      git merge-base --is-ancestor refs/remotes/origin/main HEAD 2>/dev/null \
        || fail "origin/main is not an ancestor of HEAD: pull first"
      for m in "${MODULES[@]}"; do
        x="$(module_version "${m}" HEAD)"
        [[ "${x}" == "${BASE}" ]] || fail "the release commit's modules/${m}/module.properties says version=${x:-unset}, not ${BASE}"
      done
    else
      x="$(git rev-parse -q --verify refs/remotes/origin/main || true)"
      if [[ -z "${x}" ]]; then
        fail "origin has no main branch"
      elif [[ "${head}" != "${x}" ]]; then
        fail "main (${head:0:12}) is not level with origin/main (${x:0:12}): pull or push first"
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
    git check-ignore -q "${LOGS}/run.json" || fail ".gitignore does not ignore build/ (the run folder must never be committed)"
    command -v tar >/dev/null 2>&1 || fail "tar is required"
    HARNESS_SRC="${XEWE_RELEASE_HARNESS:-../xewe-os}"
    if [[ -f "${HARNESS_SRC}/xewe.toml" && -f "${HARNESS_SRC}/setup.sh" ]]; then
      HARNESS_SRC="$(cd "${HARNESS_SRC}" && pwd)"
    else
      fail "no xewe project at ${HARNESS_SRC} (xewe.toml and setup.sh): set XEWE_RELEASE_HARNESS"
    fi
    py="$(find_python)"
    if [[ -z "${py}" ]]; then
      fail "Python >= 3.11 is required (python3.12 is what CI's card job uses)"
    else
      echo "   python: ${py}"
      write_notes
      card_tools "${py}"
      while IFS= read -r x; do [[ -z "${x}" ]] || fail "${x}"; done < <(notes_problems)
    fi
    check_problems
    # the dotenv the board run uses (the tools read it; this script only exports its path)
    if [[ -z "${XEWE_ENV:-}" ]]; then
      if [[ -f "${HARNESS_SRC}/.env" ]]; then
        export XEWE_ENV="${HARNESS_SRC}/.env"
      elif [[ -n "${XEWE_TOOLS_SOURCE:-}" && -f "${XEWE_TOOLS_SOURCE}/.env" ]]; then
        export XEWE_ENV="${XEWE_TOOLS_SOURCE}/.env"
      fi
    fi
    echo "   tools: $(tools_spec) (CI: xewe-modules-tests.yml@${TOOLS_REF})"
    echo "   harness: ${HARNESS_SRC} (copied to ${HARNESS}; modules all, source this tree)"
    echo "   dotenv: ${XEWE_ENV:-none}"
    echo "   tags: ${TAGS[*]}"
    echo "   hardware: ${HARDWARE:-none} (the ${RUN_CHIP} board tests are $([[ -n "${HARDWARE}" ]] && echo run || echo 'compiled, not run'))"
    [[ -z "${HARDWARE}" ]] || echo "   the ${HARDWARE} board must have had its first boot (xewe provision --chip ${HARDWARE}); never erase it for the run"
    x="$(chip_defines "${RUN_CHIP}" | { grep -v '^--define$' || true; } | tr '\n' ' ')"
    [[ -z "${x}" ]] || echo "   ${RUN_CHIP} defines: ${x% }"
    echo "   ok: ${VERSION} on ${head:0:12}"

    x=1
    if [[ "${MODE}" == commit && -f "${LOGS}/run.json" ]]; then
      for i in "${!SLUGS[@]}"; do [[ -s "${OUT}/${SLUGS[i]}/release_card.md" ]] || x=0; done
    else
      x=0
    fi
    if [[ "${x}" == 1 ]]; then
      echo "== reusing the results under ${OUT}/ (stages 2 and 3 skipped)"
    else
      tests "${VERSION}"
      card_stage
    fi
    commit_stage
    tagged="$(git rev-parse HEAD)"
  fi

  tag_stage "${tagged}"
  release_stage "${tagged}"
  echo "done in ${SECONDS} s"
}

# Sourcing the file (tests) defines the functions without running a release.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
