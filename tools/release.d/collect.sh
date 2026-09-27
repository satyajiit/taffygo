#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/release.d/collect.sh — the recorded pins and revisions of this checkout.
#
# Authority boundary: this module reads the values that only the repository's
# own version machinery knows how to read — the pin register behind
# TOOLCHAIN.md, chromium/REVISION, chromium/SECURITY_PATCH_LEVEL, the version
# catalog, and git. Reading bytes off disk (digests, sizes, the patch queue) is
# repo_facts.py, so no digest has two implementations that can drift; shaping
# the result is repository_fragment.py.
#
# Owning milestone: M1 (WP-M1-07). Field authority:
# docs/development/testing-and-delivery.md section 13.
#
# What it deliberately does not do: it never observes a build, a signing job, a
# device or a store upload, because it is not present when any of those happen.
# Those facts arrive as fragments from the jobs that did observe them. A
# manifest that guessed a signing certificate would be a forgery of evidence.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_RELEASE_COLLECT_SH_LOADED:-}" ]; then return 0; fi
TAFFY_RELEASE_COLLECT_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR/../lib source=../lib/versions.sh
. "$(dirname "${BASH_SOURCE[0]}")/../lib/versions.sh"
# shellcheck source-path=SCRIPTDIR/../lib source=../lib/pins.sh
. "$(dirname "${BASH_SOURCE[0]}")/../lib/pins.sh"

TAFFY_RELEASE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)

# --- the toolchain table ----------------------------------------------------

# One "component<TAB>version<TAB>owner file" row per entry of the pin register,
# read from the machine owner file — never from TOOLCHAIN.md, which is the
# human index. `./tools/check fast --only pins` is what proves the two agree.
release_toolchain_rows() {
  printf '%s\n' "$TAFFY_PIN_REGISTER" | while IFS='|' read -r label owner reader key; do
    [ -n "$label" ] || continue
    local value
    value=$(taffy_version_value "$reader" "$TAFFY_ROOT/$owner" "$key" 2>/dev/null) || value=''
    [ -n "$value" ] || value=$TAFFY_UNPINNED
    printf '%s\t%s\t%s\n' "$label" "$value" "$owner"
  done
}

# --- build configuration ----------------------------------------------------

# The ABIs a committed args profile packages, from its own target_cpu. The
# mapping is Android's, not ours: GN says arm64, the package says arm64-v8a.
release_profile_abis() { # <profile>
  local cpu
  cpu=$(taffy_kv_get "$TAFFY_ROOT/chromium/args/$1.gn" 'target_cpu' 2>/dev/null | tr -d '"') || cpu=''
  case "$cpu" in
    arm64) printf 'arm64-v8a\n' ;;
    arm)   printf 'armeabi-v7a\n' ;;
    x64)   printf 'x86_64\n' ;;
    x86)   printf 'x86\n' ;;
    *)     return 1 ;;
  esac
}

# --- provenance -------------------------------------------------------------
#
# Every build is a local build, and the manifest says so plainly. There is no
# hosted continuous integration to identify itself instead (decision 0023), so
# these read the checkout and the machine and nothing else. A manifest that
# named a workflow would be naming something this repository does not have.

release_provenance_repository() {
  git -C "$TAFFY_ROOT" config --get remote.origin.url 2>/dev/null || printf 'local-checkout\n'
}

release_provenance_ref() {
  git -C "$TAFFY_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || printf 'unknown\n'
}

release_provenance_builder() {
  printf 'local:%s@%s\n' \
    "$(id -un 2>/dev/null || printf 'unknown')" "$(hostname 2>/dev/null || printf 'unknown')"
}

# The predicate this repository actually produces. The release lane replaces it
# with the signed attestation's own predicate type when it emits one; claiming
# a provenance format we did not generate would be the exact fabrication this
# manifest exists to prevent.
TAFFY_RELEASE_PREDICATE='https://taffygo.com/predicate/build-invocation/v1'

# --- the fragment -----------------------------------------------------------

# release_repository_fragment <out.json> <gn-profile> <release-kind>
#
# Values reach python through the environment rather than through a hand-built
# JSON string: a manifest whose escaping is done by printf is a manifest that
# breaks on the first branch name that contains a quote.
release_repository_fragment() {
  local out=$1 profile=$2 kind=$3

  R_ROOT=$TAFFY_ROOT
  R_PROFILE=$profile
  R_KIND=$kind
  R_REVISION=$(git -C "$TAFFY_ROOT" rev-parse HEAD 2>/dev/null || printf '')
  R_BRANCH=$(git -C "$TAFFY_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || printf '')
  R_REMOTE=$(git -C "$TAFFY_ROOT" config --get remote.origin.url 2>/dev/null || printf '')
  if [ -n "$(git -C "$TAFFY_ROOT" status --porcelain 2>/dev/null)" ]; then
    R_DIRTY=true
  else
    R_DIRTY=false
  fi

  R_CHROMIUM_MILESTONE=$(taffy_chromium_pin milestone 2>/dev/null || printf '')
  R_CHROMIUM_TAG=$(taffy_chromium_pin tag 2>/dev/null || printf '')
  R_CHROMIUM_COMMIT=$(taffy_chromium_pin commit 2>/dev/null || printf '')
  R_SPL=$(taffy_kv_get "$TAFFY_ROOT/chromium/SECURITY_PATCH_LEVEL" level 2>/dev/null || printf '')
  R_ADVISORIES=$(taffy_kv_get "$TAFFY_ROOT/chromium/SECURITY_PATCH_LEVEL" advisories 2>/dev/null || printf '')
  R_DEPOT_TOOLS=$(taffy_pin depot_tools revision 2>/dev/null || printf '')

  R_HOST_OS=$(taffy_os)
  R_HOST_ARCH=$(taffy_arch)
  R_RUNNER=$(hostname 2>/dev/null || printf '')
  R_ABIS=$(release_profile_abis "$profile" 2>/dev/null || printf '')

  R_MIN_SDK=$(taffy_version_value toml "$TAFFY_ROOT/gradle/libs.versions.toml" minSdk 2>/dev/null || printf '')
  R_TARGET_SDK=$(taffy_version_value toml "$TAFFY_ROOT/gradle/libs.versions.toml" targetSdk 2>/dev/null || printf '')
  R_COMPILE_SDK=$(taffy_version_value toml "$TAFFY_ROOT/gradle/libs.versions.toml" compileSdk 2>/dev/null || printf '')

  R_TOOLCHAIN=$(release_toolchain_rows)

  R_PREDICATE=$TAFFY_RELEASE_PREDICATE
  R_BUILDER=$(release_provenance_builder)
  # The schema requires a non-empty invocation name and there is exactly one
  # honest value for it. Run id and attempt stay empty: nothing numbers a run.
  R_WORKFLOW=local
  R_PROV_REPO=$(release_provenance_repository)
  R_PROV_REF=$(release_provenance_ref)
  R_RUN_ID=
  R_RUN_ATTEMPT=
  R_COMMAND="./tools/release manifest --kind $kind --profile $profile"

  export R_ROOT R_PROFILE R_KIND R_REVISION R_BRANCH R_REMOTE R_DIRTY \
    R_CHROMIUM_MILESTONE R_CHROMIUM_TAG R_CHROMIUM_COMMIT R_SPL R_ADVISORIES \
    R_DEPOT_TOOLS R_HOST_OS R_HOST_ARCH R_RUNNER R_ABIS \
    R_MIN_SDK R_TARGET_SDK R_COMPILE_SDK R_TOOLCHAIN \
    R_PREDICATE R_BUILDER R_WORKFLOW R_PROV_REPO R_PROV_REF R_RUN_ID R_RUN_ATTEMPT \
    R_COMMAND

  python3 "$TAFFY_RELEASE_DIR/repository_fragment.py" > "$out"
}
