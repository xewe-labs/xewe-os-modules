#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
# SPDX-License-Identifier: GPL-3.0-only
# xewe-os-modules/scripts/release.sh
#
# release.sh: tag and publish one module, or the module set, from a prepared, pushed main.
# Usage: scripts/release.sh <version> <module>|--set [--dry-run] [--ci-timeout <min>] [--skip-ci] | --prepare <version> <module>|--set [--dry-run]
# Each module is released on its own, as tag <module>/<version> (e.g. wifi/3.0.0); the set tag
# <version> then marks a commit where every module is released at <version>, and is what projects
# pin (xewe.toml [modules] ref). No `v` prefix anywhere.
# Prepare first: `--prepare <version> <module>` sets version=<version> in
# modules/<module>/module.properties and creates the missing skeletons
# modules/<module>/releases/<version>/notes.txt (the GitHub Release text) and testcard.md (what was
# tested, attached to the release); `--prepare <version> --set` creates releases/<version>/notes.txt
# (the set's text). No git change: fill the files, commit them as "prepare <version>" and push.
# Then `scripts/release.sh <version> <module>`:
#   1 preflight: version X.Y.Z; the module exists; on main, clean, level with origin/main after a
#     fetch; tag <module>/<version> free here and on origin; its releases/<version>/ files
#     committed; module.properties says version=<version>; gh installed and logged in;
#   2 CI gate on HEAD: the newest run of workflow `tests` for HEAD on main must succeed (waits for
#     it to appear and finish; --ci-timeout minutes, default 30; --skip-ci for emergencies only);
#   3 local checks: tools/validate.py and the module's unit tests (a Python with xewe-os-tools:
#     XEWE_PYTHON, else python3 when it imports xewe, else the one behind `xewe` on PATH);
#   4 annotated tag <module>/<version> on HEAD, push it, create its GitHub Release (notes.txt,
#     testcard.md attached, not marked latest);
#   5 print how consumers get it (the set tag).
# And `scripts/release.sh <version> --set`, after every module's release: the same preflight for
# tag <version> and releases/<version>/notes.txt, plus for every module in modules/: version=<version>,
# tag <module>/<version> on HEAD or an ancestor, its files unchanged since that tag, its GitHub
# Release present; then the CI gate on HEAD, tag <version> on HEAD and a GitHub Release whose notes
# are releases/<version>/notes.txt plus a list of the module releases.
# Re-running resumes: a tag without a GitHub Release only gets the release (and a tag push if origin
# lacks it), provided the tagged commit's files say <version> (for the set: pass the module checks);
# tag and release both present: already released. --dry-run changes nothing (read-only gh and git
# queries still run), reports every failed preflight check, shows HEAD's CI state, never waits, and
# exits 1 if a check failed.
set -euo pipefail

MODULES_REPO="xewe-labs/xewe-os-modules"
CI_WORKFLOW="tests"
CI_POLL="${RELEASE_CI_POLL:-15}" # seconds between CI polls

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }
stage() { echo "== $1/5 $2"; }

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

# ci_gate SHA: wait for CI_WORKFLOW on SHA and fail unless it succeeds, within CI_TIMEOUT minutes.
ci_gate() {
  local sha="$1" deadline=$((SECONDS + CI_TIMEOUT * 60)) line id status url conclusion pid
  if [[ "${SKIP_CI}" == 1 ]]; then
    warn "!!!!!!!! --skip-ci: CI on ${sha:0:12} is NOT checked; this release may ship a red build !!!!!!!!"
    return 0
  fi
  if [[ "${DRY_RUN}" == 1 ]]; then
    echo "+ gh run list --commit ${sha} --workflow ${CI_WORKFLOW} --branch main ... (poll every ${CI_POLL}s until a run appears, up to ${CI_TIMEOUT} min)"
    echo "+ gh run watch <run id> --exit-status (if not completed; fail unless success)"
    if [[ "${GH_OK}" == 1 && "${sha}" =~ ^[0-9a-f]{40}$ ]]; then
      line="$(ci_run "${sha}" 2>/dev/null)" || line="(gh run list failed)"
      echo "   current CI state of ${sha:0:12}: ${line:-no ${CI_WORKFLOW} run yet}"
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
  echo "   CI passed: ${url}"
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

# prepare VERSION MODULE: set the module's version and create its releases/VERSION/ skeletons; with
# MODULE empty (--set), create the set's releases/VERSION/notes.txt skeleton.
prepare() {
  local v="$1" m="$2" dir props
  if [[ -z "${m}" ]]; then
    dir="releases/${v}"
    if [[ ! -s "${dir}/notes.txt" ]]; then
      echo "create ${dir}/notes.txt (skeleton: fill it in)"
      [[ "${DRY_RUN}" == 1 ]] || { mkdir -p "${dir}"; cat > "${dir}/notes.txt" <<EOF
xewe-os-modules ${v}

The module set ${v}: every module in modules/ released at ${v}. Projects pin this tag.

MODULES

- <module> ${v}, ... (each one's release is linked below)

REQUIREMENTS

- XeWeCore >=<X.Y.Z>,<<X.Y.Z>; xewe-os-tools <X.Y.Z> or later.
EOF
      }
    fi
    echo "no git change was made. Review and fill ${dir}/notes.txt, then commit and push:"
    echo "  git add ${dir} && git commit -m \"prepare ${v}\" && git push origin main"
    echo "then, once every module is released: scripts/release.sh ${v} --set --dry-run; scripts/release.sh ${v} --set"
    return
  fi
  dir="modules/${m}/releases/${v}"
  props="modules/${m}/module.properties"
  if [[ "$(module_version "${m}" "")" == "${v}" ]]; then
    echo "version ${v}: ${props} already says it"
  else
    [[ "$(grep -c '^version=' "${props}")" == 1 ]] || die "${props}: expected one version= line"
    echo "version ${v}: ${props}"
    if [[ "${DRY_RUN}" != 1 ]]; then
      sed "s/^version=.*/version=${v}/" "${props}" > "${props}.tmp"
      cat "${props}.tmp" > "${props}" # cat keeps the file's mode
      rm -f "${props}.tmp"
    fi
  fi
  if [[ ! -s "${dir}/notes.txt" ]]; then
    echo "create ${dir}/notes.txt (skeleton: fill it in)"
    [[ "${DRY_RUN}" == 1 ]] || { mkdir -p "${dir}"; cat > "${dir}/notes.txt" <<EOF
${m} ${v}

What the module does, in two or three sentences.

CHANGED SINCE <previous version>

- ...

REQUIREMENTS

- XeWeCore (requires_core in module.properties); libraries; modules it depends on.

SETTINGS AND COMMANDS

- ...

TESTING

What was tested on which commits, and what was not: modules/${m}/releases/${v}/testcard.md
(attached to this release).
EOF
    }
  fi
  if [[ ! -s "${dir}/testcard.md" ]]; then
    echo "create ${dir}/testcard.md (skeleton: fill it in)"
    [[ "${DRY_RUN}" == 1 ]] || { mkdir -p "${dir}"; cat > "${dir}/testcard.md" <<EOF
# ${m} ${v} test card

| Component or command | Host unit tests | Board tests | Not covered |
|---|---|---|---|

## Environment

## Commits
EOF
    }
  fi
  echo "no git change was made. Review, fill ${dir}/, then commit and push:"
  echo "  git add ${props} ${dir} && git commit -m \"prepare ${m} ${v}\" && git push origin main"
  echo "then: scripts/release.sh ${v} ${m} --dry-run; scripts/release.sh ${v} ${m}"
}

# xewe_python: a Python that imports xewe (xewe-os-tools), or nothing.
xewe_python() {
  local py xewe_bin
  for py in "${XEWE_PYTHON:-}" python3; do
    [[ -n "${py}" ]] && "${py}" -c 'import xewe' >/dev/null 2>&1 && { echo "${py}"; return; }
  done
  xewe_bin="$(command -v xewe 2>/dev/null)" || return 0
  py="$(head -n 1 "${xewe_bin}" | sed -n 's/^#! *//p')"
  [[ -n "${py}" ]] && "${py}" -c 'import xewe' >/dev/null 2>&1 && echo "${py}"
  return 0
}

usage() { sed -n 's/^# Usage: //p' "${BASH_SOURCE[0]}"; }

main() {
  VERSION=""
  MODULE=""
  SET=0
  DRY_RUN=0
  SKIP_CI=0
  PREPARE=0
  CI_TIMEOUT=30
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dry-run) DRY_RUN=1 ;;
      --skip-ci) SKIP_CI=1 ;;
      --prepare) PREPARE=1 ;;
      --set) SET=1 ;;
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
  [[ "${SET}" == 1 && -n "${MODULE}" ]] && die "a module or --set, not both ($(usage))"
  [[ "${SET}" == 1 || -n "${MODULE}" ]] || die "name a module or --set ($(usage))"

  # Repo root: the top of the git work tree this is run in.
  command -v git >/dev/null 2>&1 || die "git is required"
  ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || die "not inside a git work tree"
  cd "${ROOT}"
  [[ -f tools/validate.py && -d modules ]] || die "${ROOT} is not the xewe-os-modules repo"
  if [[ -n "${MODULE}" ]]; then
    [[ "${MODULE}" =~ ^[a-z0-9][a-z0-9-]*$ && -f "modules/${MODULE}/module.properties" ]] \
      || die "no module ${MODULE}: modules/${MODULE}/module.properties is missing"
  fi
  [[ "${DRY_RUN}" == 1 ]] && echo "== dry run: nothing is changed; commands are printed with a leading +"

  if [[ "${PREPARE}" == 1 ]]; then
    [[ "${SKIP_CI}" == 0 ]] || die "--skip-ci does not apply to --prepare"
    prepare "${VERSION}" "${MODULE}"
    return
  fi
  [[ "${SKIP_CI}" == 1 ]] && warn "!!!!!!!! --skip-ci: the CI gate is skipped (emergencies only) !!!!!!!!"

  local tag="${VERSION}" title="xewe-os-modules ${VERSION}" dir="releases/${VERSION}"
  if [[ -n "${MODULE}" ]]; then
    tag="${MODULE}/${VERSION}"; title="${MODULE} ${VERSION}"; dir="modules/${MODULE}/releases/${VERSION}"
  fi
  local notes="${dir}/notes.txt" card="${dir}/testcard.md"

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
  fi

  [[ -s "${notes}" ]] || fail "${notes} is missing (the release notes; scripts/release.sh --prepare ${VERSION} ${MODULE:---set})"
  local files=("${notes}")
  if [[ -n "${MODULE}" ]]; then
    [[ -s "${card}" ]] || fail "${card} is missing (the test card; scripts/release.sh --prepare ${VERSION} ${MODULE})"
    files+=("${card}")
  fi
  git ls-files --error-unmatch -- "${files[@]}" >/dev/null 2>&1 || fail "${dir}/ is not committed"
  local head
  head="$(git rev-parse HEAD)"
  # A fresh release needs a clean main at origin's head; a resume only needs the tagged commit.
  if [[ "${resume}" == 0 ]]; then
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
        || fail "modules/${MODULE}/module.properties says version=${have} (scripts/release.sh --prepare ${VERSION} ${MODULE})"
    else
      problems="$(set_problems "${VERSION}" "${head}")"
      [[ -z "${problems}" ]] || fail "not every module is released at ${VERSION} on HEAD:"$'\n'"${problems}"
    fi
  fi
  local py=""
  if [[ -n "${MODULE}" && "${resume}" == 0 ]]; then
    py="$(xewe_python)"
    [[ -n "${py}" ]] || fail "no Python with xewe-os-tools for the local checks (set XEWE_PYTHON, e.g. <harness>/build/tools/.venv/bin/python)"
  fi
  if [[ "${FAILED}" -gt 0 ]]; then
    echo "== dry run: ${FAILED} preflight check(s) failed; a real run stops at the first"
  else
    echo "   ok: ${tag} on ${head:0:12}"
  fi

  if [[ "${resume}" == 1 ]]; then
    echo "== resuming at stage 4: tag ${tag} exists (${tagged:0:12}), no GitHub Release yet"
  else
    stage 2 "CI on HEAD"
    ci_gate "${head}"

    stage 3 "local checks"
    if [[ -n "${MODULE}" ]]; then
      run "${py:-<python with xewe>}" tools/validate.py
      run env XEWE_NO_BOARD=1 "${py:-<python with xewe>}" -m pytest -q -p no:cacheprovider "modules/${MODULE}/tests/unit"
    else
      echo "   none for the set: each module's release ran them"
    fi
  fi

  stage 4 "tag and GitHub Release"
  if [[ "${resume}" == 0 ]]; then
    run git tag -a "${tag}" -m "xewe-os-modules ${tag}" "${head}"
  fi
  tag_remote "${tag}" || run git push origin "refs/tags/${tag}"
  if [[ -n "${MODULE}" ]]; then
    run gh release create "${tag}" --verify-tag --latest=false --title "${title}" --notes-file "${notes}" \
      "${card}#${MODULE} ${VERSION} test card"
  else
    # The set's notes: releases/<version>/notes.txt, then one line per module release.
    local body m
    body="$(cat "${notes}"; printf '\nMODULE RELEASES\n\n'
      for m in modules/*/; do
        m="$(basename "${m}")"
        [[ -f "modules/${m}/module.properties" ]] || continue
        echo "- ${m} ${VERSION}: https://github.com/${MODULES_REPO}/releases/tag/${m}/${VERSION}"
      done)"
    if [[ "${DRY_RUN}" == 1 ]]; then
      SET_NOTES="<set notes>"
      echo "   <set notes>:"; while IFS= read -r m; do echo "   | ${m}"; done <<< "${body}"
    else
      SET_NOTES="$(mktemp)"
      trap 'rm -f "${SET_NOTES}"' EXIT
      printf '%s\n' "${body}" > "${SET_NOTES}"
    fi
    run gh release create "${tag}" --verify-tag --title "${title}" --notes-file "${SET_NOTES}"
  fi

  stage 5 consumers
  if [[ -n "${MODULE}" ]]; then
    echo "Projects get ${MODULE} ${VERSION} through the set tag: once every module is released at ${VERSION},"
    echo "scripts/release.sh ${VERSION} --set; then xewe.toml [modules] ref = \"${VERSION}\" and ./setup.sh."
  else
    echo "Projects: xewe.toml [modules] ref = \"${VERSION}\" (or xewe manifest update), then ./setup.sh."
  fi
  if [[ "${DRY_RUN}" == 1 && "${FAILED}" -gt 0 ]]; then
    exit 1
  fi
}

# Sourcing the file (tests) defines the functions without running a release.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
