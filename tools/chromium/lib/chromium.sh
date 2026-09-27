#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Shared helpers for the ./tools/chromium/* commands.
#
# Authority for every procedure here: docs/development/chromium-fork-and-build.md
#
# shellcheck shell=bash

if [ -n "${TAFFY_CHROMIUM_SH_LOADED:-}" ]; then return 0; fi
TAFFY_CHROMIUM_SH_LOADED=1

# shellcheck disable=SC1091
. "$(dirname "${BASH_SOURCE[0]}")/../../lib/common.sh"

TAFFY_PATCH_BRANCH='taffy/patched'
# The identity every exported patch carries, in `git format-patch`'s `From:`
# shape. It is one string in one place because 47 files must agree with it and
# with each other, and because `git am` turns this line into the commit author
# in the checkout -- which `git format-patch` then reads back on the next
# export. That round trip is why this is normalised at export rather than
# edited in the files: a hand-edit survives only until somebody exports from a
# checkout that was not re-synced first, and nothing would say so.
#
# It is a project identity, not a person's (decision 0203 keeps the signing
# identity outside the repository for the same reason a published patch queue
# should not carry a private mailbox). The holder matches NOTICE and LICENSE.
# shellcheck disable=SC2034  # consumed by export-patches and the chromium lane
TAFFY_PATCH_AUTHOR='Matterward Labs <admin@matterwardlabs.com>'
# The throwaway branch the rebase rehearsal replays onto. It is never checked
# out in the primary tree: chromium_rehearsal_worktree gives it one of its own,
# which is what makes the rehearsal unable to reach the tree someone works in.
# shellcheck disable=SC2034  # consumed by the scripts that source this file
TAFFY_REHEARSAL_BRANCH='taffy/rehearsal'
# The product profiles. Every one of these targets Android, and only these may
# be installed on a device.
TAFFY_PROFILES='dev-x64 dev-arm64 release-arm64'
# The diagnostic profiles: host-targeted, never shipped, and never installable.
# They are a separate register rather than more names in the one above so
# that a relaxation written for a diagnostic build cannot reach a profile that
# ships, and so that `run` can refuse one with a sentence instead of failing
# late on a missing APK. `./tools/check fast --only chromium` holds both
# directions of this split. diag-sanitizer-x64 answers whether TaffyGo leaks;
# diag-fuzz-x64 is the only configuration under which the thirteen fuzzers in
# //taffy/test/fuzz are compiled at all.
# shellcheck disable=SC2034  # consumed by the scripts that source this file
TAFFY_HOST_PROFILES='diag-sanitizer-x64 diag-fuzz-x64'
# shellcheck disable=SC2034  # consumed by the scripts that source this file
TAFFY_DEFAULT_PROFILE='dev-x64'
# shellcheck disable=SC2034
TAFFY_DEFAULT_TARGET='taffy_public_apk'

chromium_workspace() {
  local ws
  ws=${TAFFY_CHROMIUM_WORKSPACE:-$(taffy_state_get chromium_workspace "")}
  [ -n "$ws" ] || die \
    "no Chromium workspace recorded" \
    "Run: ./tools/bootstrap --profile chromium --workspace <path>" \
    "The checkout lives outside this repository (decision 0012)."
  taffy_reject_windows_mount "$ws"
  printf '%s\n' "$ws"
}

chromium_src() { printf '%s/src\n' "$(chromium_workspace)"; }

chromium_require_checkout() {
  local src; src=$(chromium_src)
  # `-e`, not `-d`. A linked `git worktree` carries a `.git` *file* pointing at
  # the repository it belongs to, so a directory test refuses exactly the one
  # kind of tree it is safe to replay a patch queue in. That is the trap the 153
  # rebase rehearsal hit (docs/development/chromium-fork-and-build.md section 6
  # item 16): the safe host was the host the tool rejected.
  [ -e "$src/.git" ] || die \
    "no Chromium checkout at $src" \
    "Run: ./tools/chromium/sync"
  printf '%s\n' "$src"
}

chromium_require_pin() {
  taffy_chromium_is_pinned || die \
    "chromium/REVISION is unpinned" \
    "SP-01 selects the Chromium stable milestone and records milestone, tag, and" \
    "commit in chromium/REVISION. Every build resolves that commit plus" \
    "chromium/patches/ and taffy-core/ — nothing else." \
    "Procedure: docs/development/chromium-fork-and-build.md section 1.3"
  taffy_chromium_pin commit
}

# depot_tools must be on PATH for fetch/gclient/gn/autoninja to resolve.
chromium_add_depot_tools_to_path() {
  local dt
  dt=$(taffy_state_get depot_tools "")
  if [ -n "$dt" ] && [ -d "$dt" ]; then
    case ":$PATH:" in
      *":$dt:"*) : ;;
      *) PATH="$PATH:$dt"; export PATH ;;
    esac
  fi
  export DEPOT_TOOLS_UPDATE=${DEPOT_TOOLS_UPDATE:-0}
  have gclient || die \
    "depot_tools is not on PATH" \
    "Run: ./tools/bootstrap --profile chromium" \
    "or export PATH=\"\$PATH:<workspace>/depot_tools\""
}

# chromium_validate_profile <profile> [class]
#
# `class` is `device` (installable product profiles only), `host` (diagnostic
# profiles only) or `any` (the default: either). A command asks for the class it
# can actually honour, so pointing `run` at a host profile is refused here with
# the reason rather than ten lines later with a missing file.
chromium_validate_profile() {
  local profile=$1 class=${2:-any} known accepted=''
  case "$class" in
    device) accepted=$TAFFY_PROFILES ;;
    host) accepted=$TAFFY_HOST_PROFILES ;;
    any) accepted="$TAFFY_PROFILES $TAFFY_HOST_PROFILES" ;;
    *) die "chromium_validate_profile: unknown class: $class" ;;
  esac
  for known in $accepted; do
    [ "$profile" = "$known" ] && return 0
  done
  if [ "$class" = device ]; then
    for known in $TAFFY_HOST_PROFILES; do
      [ "$profile" = "$known" ] && die \
        "$profile is a host profile: there is nothing to install or launch" \
        "Build it:  ./tools/chromium/build --profile $profile taffy_unittests" \
        "Run it:    ./tools/chromium/test --profile $profile taffy_unittests"
    done
  fi
  die "unknown build profile: $profile" \
    "Product profiles (chromium/args/): $TAFFY_PROFILES" \
    "Host profiles (chromium/args/): $TAFFY_HOST_PROFILES" \
    "GN args are never typed by hand (decision 0013)."
}

# True when this profile's committed args turn a sanitizer on. Read from the
# profile rather than matched against its name, so a second sanitizer profile
# is handled the day it is committed.
chromium_profile_is_sanitized() {
  grep -qE '^is_(asan|lsan|msan|tsan|ubsan) = true' "$(chromium_args_file "$1")" 2>/dev/null
}

chromium_args_file() { printf '%s/chromium/args/%s.gn\n' "$TAFFY_ROOT" "$1"; }
chromium_out_dir() { printf '%s/out/%s\n' "$(chromium_src)" "$1"; }

# The signing identity, in its own file (decision 0203).
# shellcheck source-path=SCRIPTDIR source=signing.sh
. "$(dirname "${BASH_SOURCE[0]}")/signing.sh"

# The product-root mount, in its own file.
# shellcheck source-path=SCRIPTDIR source=mount.sh
. "$(dirname "${BASH_SOURCE[0]}")/mount.sh"

# --- static analysis --------------------------------------------------------

# The flag Chromium's GN rules put on an analysis action to hand it to the
# background build server instead of running it. One string, named once, because
# both the probe below and its own self-check have to mean the same thing.
TAFFY_BUILD_SERVER_FLAG='--use-build-server'
# The file those rules add it in. Grepped, not imported: this is a self-check
# that the probe still describes the tree, so it must fail when upstream moves
# the mechanism rather than quietly finding nothing.
TAFFY_BUILD_SERVER_RULES='build/config/android/internal_rules.gni'

# A third string, and the reason there is one: it is what makes an *absence*
# readable. `gyp/lint.py` missing from the graph means either "lint is off" or
# "GN writes these edges somewhere this probe did not look", and those two need
# telling apart. Every Android analysis action is a `build/android/gyp/*.py`
# script, so finding other members of that family exactly where lint would be is
# what turns the absence into an answer.
TAFFY_ANDROID_GYP_ACTIONS='build/android/gyp/'

# True when either top-level generated ninja file contains a string. GN writes
# the default toolchain's action edges into these two — 183 MB against the
# 1.7 GB the whole output directory holds in 15,160 `*.ninja` files — so this is
# the pass worth taking first. It is never the last word: nothing here knows
# that GN will keep writing them there, which is what the exhaustive walk below
# is for.
chromium_ninja_contains() {
  local out=$1 needle=$2 f
  for f in "$out/toolchain.ninja" "$out/build.ninja"; do
    [ -f "$f" ] || continue
    grep -q -- "$needle" "$f" 2>/dev/null && return 0
  done
  return 1
}

# The same question asked of every generated ninja file. About 7 s against 0.3 s,
# so it is the fallback rather than the rule — reached only when the fast pass
# found neither lint nor its neighbours, which is the one case where the fast
# pass has not earned an answer.
chromium_ninja_contains_anywhere() {
  local out=$1 needle=$2 hit
  hit=$(find "$out" -name '*.ninja' -print0 2>/dev/null \
        | xargs -0 grep -l -- "$needle" 2>/dev/null | head -n 1)
  [ -n "$hit" ]
}

# chromium_static_analysis_mode <src> <out dir> — print `inline`, `build-server`
# or `off` for the graph that was just generated.
#
# It reads the graph rather than the argument. `android_static_analysis` is
# resolved inside build/config/android/config.gni *after* its `declare_args`
# block — an unset value becomes `off` for an official build and `build_server`
# otherwise — so `gn args --list` reports `default` for the very profiles whose
# behaviour is in question and cannot answer this. What the build actually does
# is written into the edges, and that is what is read.
chromium_static_analysis_mode() {
  local src=$1 out=$2

  # The probe's own premise, checked first. If the flag is no longer in the
  # rules that add it, "not found in the graph" stops meaning "runs inline" and
  # starts meaning nothing at all, which is the failure this whole check exists
  # to prevent one directory over.
  grep -q -- "$TAFFY_BUILD_SERVER_FLAG" "$src/$TAFFY_BUILD_SERVER_RULES" 2>/dev/null || die \
    "$TAFFY_BUILD_SERVER_FLAG is not in $TAFFY_BUILD_SERVER_RULES at this pin" \
    "That file is where Chromium's GN rules hand lint, Error Prone, the direct-" \
    "dependency check and TraceReferences to the background build server, and" \
    "this build cannot tell whether they run inline without it. The mechanism" \
    "moved at a rebase: re-derive the probe in tools/chromium/lib/chromium.sh" \
    "before trusting another build." \
    "Procedure: docs/development/chromium-fork-and-build.md section 6 item 18"

  if chromium_ninja_contains "$out" "$TAFFY_BUILD_SERVER_FLAG"; then
    printf 'build-server\n'
  elif chromium_ninja_contains "$out" 'gyp/lint.py'; then
    printf 'inline\n'
  elif chromium_ninja_contains "$out" "$TAFFY_ANDROID_GYP_ACTIONS"; then
    # Lint's neighbours are here and lint is not, so lint is genuinely absent.
    printf 'off\n'
  elif chromium_ninja_contains_anywhere "$out" "$TAFFY_BUILD_SERVER_FLAG"; then
    printf 'build-server\n'
  elif chromium_ninja_contains_anywhere "$out" 'gyp/lint.py'; then
    printf 'inline\n'
  elif ! chromium_ninja_contains_anywhere "$out" "$TAFFY_ANDROID_GYP_ACTIONS"; then
    # No Android build steps of any kind, so there is no Android static
    # analysis to have been turned off. Saying `off` here would be true of the
    # outcome and false about the reason, and the reason is what the caller
    # prints.
    printf 'not-android\n'
  else
    printf 'off\n'
  fi
}

# chromium_require_honest_static_analysis <src> <out dir> <profile> — refuse to
# run a build whose exit code would not include its own static analysis.
#
# `not-android` is a host build, where there is nothing to analyse.
# `build_server` is the mode that makes this command a liar. autoninja dispatches
# the analysis to build/android/fast_local_dev_server.py, touches the stamp so
# the build step completes, and returns; the server runs the work afterwards and
# reports a failure only by writing `FAILED:` into buildserver.log.0 and
# deleting the stamp. Nothing in that path reaches an exit code, and
# `--wait-for-idle` cannot rescue it — `_wait_for_idle()` returns 0 whatever
# happened, and the build-info message it waits on carries pending and completed
# counts with no failure among them.
chromium_require_honest_static_analysis() {
  local src=$1 out=$2 profile=$3 mode
  step "static analysis: does a lint failure fail this command?"
  mode=$(chromium_static_analysis_mode "$src" "$out")
  case "$mode" in
    inline)
      ok "inline — Android Lint, Error Prone and the dependency checks are ninja actions here"
      ;;
    not-android)
      ok "not applicable — profile $profile builds nothing for Android"
      note "Android Lint and Error Prone have no work here. The dev profiles are"
      note "where they run. See chromium/args/$profile.gn."
      ;;
    off)
      warn "off in profile $profile — this build proves nothing about lint or Error Prone"
      note "is_official_build resolves android_static_analysis to \"off\"; the dev"
      note "profiles are where the analysis runs. See chromium/args/$profile.gn."
      ;;
    *)
      die "profile $profile hands static analysis to the background build server" \
        "In that mode autoninja returns before Android Lint, Error Prone and the" \
        "dependency checks have run, so their failures never reach this command's" \
        "exit code — the build prints success while lint fails. It has happened on" \
        "this builder: out/<profile>/buildserver.log.* holds the FAILED lines." \
        "Add android_static_analysis = \"on\" to chromium/args/$profile.gn." \
        "Procedure: docs/development/chromium-fork-and-build.md section 6 item 18"
      ;;
  esac
}

# --- patch queue ------------------------------------------------------------

# Every patch in application order: chromium/patches/*.patch then security/*.
chromium_patch_list() {
  local dir="$TAFFY_ROOT/chromium/patches"
  find "$dir" -maxdepth 1 -name '*.patch' -type f 2>/dev/null | sort
  find "$dir/security" -maxdepth 1 -name '*.patch' -type f 2>/dev/null | sort
}

chromium_patch_attribution() { # print the owner/reason header of a patch
  sed -n '1,25p' "$1" | grep -E '^(From|Subject|Owner|Reason|Retire):' || true
}

# Verdicts of the last chromium_apply_patches run, reset at the start of each.
# The callers report the summary, so the counts live here rather than being
# recomputed from output that has already been printed.
TAFFY_PATCH_TOTAL=0
TAFFY_PATCH_CLEAN=0
TAFFY_PATCH_MERGED=0
TAFFY_PATCH_FAILED=0

# Print a block of captured text as notes, so it lines up with the verdict it
# belongs to instead of interleaving with git's own stream.
chromium_note_block() {
  local line
  while IFS= read -r line; do note "$line"; done
}

# chromium_apply_one <tree> <patch> — apply one patch and classify the result.
# Returns 0 clean, 1 auto-merged, 2 failed, and prints its own verdict line.
#
# The strict apply is tried first and the three-way fallback only after it,
# because the two are different facts about the queue and only one of them is
# reproducible. `git am --3way` reports both as plain success: at the 153
# rehearsal two patches merged through blob-ancestry reconstruction and were
# read as clean applies (docs/development/chromium-fork-and-build.md section 6
# item 16). A patch that needed the fallback still applies today and will need a
# human sooner than the queue suggests, so it says so.
chromium_apply_one() {
  local tree=$1 patch=$2 name output
  name=$(basename "$patch")

  if output=$(git -C "$tree" am --quiet "$patch" 2>&1); then
    ok "applied $name"
    return 0
  fi
  git -C "$tree" am --abort >/dev/null 2>&1 || true

  if output=$(git -C "$tree" am --3way --quiet "$patch" 2>&1); then
    warn "auto-merged $name — the strict apply failed and a three-way merge did not"
    note "The context has moved. Re-export the queue before it drifts further."
    return 1
  fi
  git -C "$tree" am --abort >/dev/null 2>&1 || true

  bad "does not apply: ${patch#"$TAFFY_ROOT"/}"
  chromium_patch_attribution "$patch" | chromium_note_block
  printf '%s\n' "$output" | sed -n '1,20p' | chromium_note_block
  return 2
}

# The one account of a queue that does not apply, so the import and the
# rehearsal cannot drift into two descriptions of the same failure.
chromium_patch_queue_failed() {
  local tree=$1 commit=$2 branch=$3
  info ""
  die "the patch queue does not apply to ${commit:0:12}" \
    "Of $TAFFY_PATCH_TOTAL tried: $TAFFY_PATCH_CLEAN clean, $TAFFY_PATCH_MERGED auto-merged, $TAFFY_PATCH_FAILED failed." \
    "Fix it as a commit on $branch in $tree, then run" \
    "./tools/chromium/export-patches. If this is a milestone rebase, that is" \
    "expected work — record the conflict in the rebase report." \
    "Procedure: docs/development/chromium-fork-and-build.md section 1.3"
}

chromium_report_patch_verdicts() {
  if [ "$TAFFY_PATCH_TOTAL" -eq 0 ]; then
    note "patch queue is empty — the checkout is pristine upstream at the pin"
    return 0
  fi
  local summary="$TAFFY_PATCH_TOTAL patch(es): $TAFFY_PATCH_CLEAN clean"
  [ "$TAFFY_PATCH_MERGED" -eq 0 ] || summary="$summary, $TAFFY_PATCH_MERGED auto-merged"
  [ "$TAFFY_PATCH_FAILED" -eq 0 ] || summary="$summary, $TAFFY_PATCH_FAILED failed"
  if [ "$TAFFY_PATCH_FAILED" -gt 0 ]; then
    bad "$summary"
  elif [ "$TAFFY_PATCH_MERGED" -gt 0 ]; then
    warn "$summary"
  else
    ok "$summary"
  fi
  return 0
}

# chromium_apply_patches <tree> <commit> [branch] [stop|continue]
#
# Apply the queue onto a branch rooted at a commit, in the working tree given.
# Loud, per patch, and per verdict. Returns non-zero when any patch failed.
#
# The mode is the difference between the two jobs that call this. `stop` is an
# import: a branch missing a patch from the middle of its stack is a worse thing
# to hand somebody than a stopped run, so the first failure ends it. `continue`
# is the rehearsal, whose entire product is how many of the queue fail and
# where — it applies what it can on top of the last good commit, exactly as a
# real rebase traversal does, and reports every one.
chromium_apply_patches() {
  local tree=$1 commit=$2 branch=${3:-$TAFFY_PATCH_BRANCH} mode=${4:-stop}
  local patch status
  TAFFY_PATCH_TOTAL=0; TAFFY_PATCH_CLEAN=0; TAFFY_PATCH_MERGED=0; TAFFY_PATCH_FAILED=0

  git -C "$tree" rev-parse --verify --quiet "$commit^{commit}" >/dev/null || die \
    "${commit:0:12} is not in the checkout at $tree" \
    "The queue is applied to that commit and nothing else, so it has to be" \
    "fetched before it can be a base: git -C $tree fetch origin"

  step "creating $branch at ${commit:0:12}"
  git -C "$tree" checkout --quiet -B "$branch" "$commit" || die \
    "could not create $branch at ${commit:0:12} in $tree" \
    "Another git worktree may have that branch checked out; a branch cannot be" \
    "checked out twice. List them: git -C $tree worktree list"

  while IFS= read -r patch; do
    [ -n "$patch" ] || continue
    TAFFY_PATCH_TOTAL=$((TAFFY_PATCH_TOTAL + 1))
    status=0
    chromium_apply_one "$tree" "$patch" || status=$?
    case "$status" in
      0) TAFFY_PATCH_CLEAN=$((TAFFY_PATCH_CLEAN + 1)) ;;
      1) TAFFY_PATCH_MERGED=$((TAFFY_PATCH_MERGED + 1)) ;;
      *)
        TAFFY_PATCH_FAILED=$((TAFFY_PATCH_FAILED + 1))
        [ "$mode" = 'continue' ] || chromium_patch_queue_failed "$tree" "$commit" "$branch"
        ;;
    esac
  done < <(chromium_patch_list)

  chromium_report_patch_verdicts
  [ "$TAFFY_PATCH_FAILED" -eq 0 ] || return 1
  return 0
}

# --- rehearsal isolation ----------------------------------------------------

# The absolute path of the repository a working tree belongs to, or nothing.
# `--git-common-dir` can answer relatively, and relative to the working
# directory rather than to the tree, so it is resolved from inside the tree.
chromium_git_common_dir() {
  local tree=$1 common
  common=$(cd "$tree" 2>/dev/null && git rev-parse --git-common-dir 2>/dev/null) || return 1
  [ -n "$common" ] || return 1
  ( cd "$tree" && cd "$common" && pwd -P ) 2>/dev/null
}

# chromium_rehearsal_worktree <src> <commit> — print the path of a working tree
# the rehearsal owns, creating it if it does not exist yet.
#
# The rebase rehearsal must not be able to touch the tree someone is working in,
# and the way to get that is structural rather than an order of guards: it is
# handed its own `git worktree`, and every command it runs is given that
# directory. A linked worktree has its own HEAD, its own index and its own
# working tree, so the primary checkout's are not reachable from inside it at
# all — no flag, no argument and no ordering mistake in this file can reach
# them, which is the property the old code did not have.
#
# It is created once and reused. The cost is a checkout of the src repository
# alone: on the builder, `git worktree add --detach` materialised 502,849 files
# in 18.6 s (docs/development/chromium-fork-and-build.md section 6 item 16),
# because the gclient-managed directories are not tracked by that repository and
# are never copied.
chromium_rehearsal_worktree() {
  local src=$1 commit=$2 dir head common expect
  dir="$(chromium_workspace)/rehearsal"

  # The rehearsal branch belongs to the rehearsal worktree. A checkout stranded
  # on it — which is how the previous version of this tool left one — would make
  # `git worktree add` fail for a reason that reads like a git problem.
  head=$(git -C "$src" symbolic-ref --quiet --short HEAD 2>/dev/null) || head=''
  [ "$head" != "$TAFFY_REHEARSAL_BRANCH" ] || die \
    "the checkout at $src is on $TAFFY_REHEARSAL_BRANCH" \
    "That branch belongs to the rehearsal worktree, and a branch cannot be" \
    "checked out twice. Put the checkout back on the queue branch first:" \
    "git -C $src checkout $TAFFY_PATCH_BRANCH"

  git -C "$src" worktree prune >/dev/null 2>&1 || true

  if [ -e "$dir/.git" ]; then
    # Generated and thrown away on every run, so the last rehearsal's leftovers
    # are cleared here rather than left for a human. It is proved to be a
    # worktree of this checkout first: a reset must never be able to land on a
    # tree that somebody's work is in.
    common=$(chromium_git_common_dir "$dir") || common=''
    expect=$(chromium_git_common_dir "$src") || expect=''
    if [ -z "$common" ] || [ "$common" != "$expect" ]; then
      die "$dir is not a worktree of $src" \
        "The rehearsal resets that path on every run and will not do it to a" \
        "tree it does not own. Move it aside, or point" \
        "TAFFY_CHROMIUM_WORKSPACE at another workspace."
    fi
    step "reusing the rehearsal worktree at $dir"
    git -C "$dir" am --abort >/dev/null 2>&1 || true
    git -C "$dir" reset --hard --quiet >/dev/null 2>&1 || true
  elif [ -e "$dir" ]; then
    die "$dir exists and is not a git worktree" \
      "The rehearsal needs that path. Move it aside and re-run."
  else
    step "creating the rehearsal worktree at $dir"
    note "A checkout of the src repository only; it is reused by later runs."
    git -C "$src" worktree add --quiet --detach "$dir" "$commit" || die \
      "could not create the rehearsal worktree at $dir" \
      "$(taffy_free_gb "$(dirname "$dir")") GB free on that volume." \
      "Remove a stale one with: git -C $src worktree remove --force $dir"
  fi
  printf '%s\n' "$dir"
}

# chromium_restore_head <src> <ref> <commit> — put HEAD back where it was found.
#
# The rehearsal is isolated in a worktree and cannot move this HEAD, so in a
# correct run this changes nothing. It runs on every exit path anyway, error
# paths included, because "cannot" is a claim about code that can be edited and
# a stranded HEAD is precisely how the previous version of this tool left
# checkouts. It restores without --force, so git refuses rather than writing
# over uncommitted work, and says what to run when it refuses.
chromium_restore_head() {
  local src=$1 ref=$2 commit=$3 now
  now=$(git -C "$src" symbolic-ref --quiet HEAD 2>/dev/null) || now=''
  if [ "$now" = "$ref" ]; then
    if [ -n "$ref" ]; then return 0; fi
    # Detached both before and now: the same commit means nothing moved.
    if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null || printf '')" = "$commit" ]; then
      return 0
    fi
  fi

  if [ -n "$ref" ]; then
    warn "HEAD in $src moved; putting it back on ${ref#refs/heads/}"
    git -C "$src" checkout --quiet "${ref#refs/heads/}" 2>/dev/null || note \
      "could not restore it — run: git -C $src checkout ${ref#refs/heads/}"
  else
    warn "HEAD in $src moved; putting it back on ${commit:0:12}"
    git -C "$src" checkout --quiet --detach "$commit" 2>/dev/null || note \
      "could not restore it — run: git -C $src checkout --detach $commit"
  fi
  return 0
}

# --- cache reporting --------------------------------------------------------

chromium_report_cache() {
  have ccache || return 0
  local rate
  rate=$(ccache -s 2>/dev/null | awk -F: '/[Cc]ache hit rate/ { gsub(/^[ \t]+/,"",$2); print $2; exit }')
  [ -n "$rate" ] && note "ccache hit rate: $rate"
  return 0
}
