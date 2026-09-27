#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

#
# Can this host run a given lane, and if not, why not.
#
# One predicate per lane. Each answers with a reason and a remediation on
# stdout so ./tools/check and ./tools/doctor print the same sentence from the
# same place — a lane is never reported as passing for something it did not
# run, and never reported as broken when the host is simply missing a tool.
#
# Sourced by tools/lib/lanes.sh, never on its own and never executed.
# Targets bash 3.2.
#
# shellcheck shell=bash

# --- readiness predicates ---------------------------------------------------
#
# Each: 0 = the lane would run here, prints nothing.
#       1 = it would skip, prints "<reason>\t<remediation>".

# Ahead of `rust` in the running order on purpose. A stale embedded baseline
# fails a cargo test with a message about a catalog assertion, which sends the
# reader into the portable model router; this lane names the real defect — the
# committed baseline and its source have parted company — before that happens.
lane_ready_architecture() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/build/component_graph.py" ]; then
    printf 'the //taffy component register is not scaffolded\tCreate taffy-core/build/components.toml and its validator together.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe component register check is standard-library-only.\n'; return 1
  fi
  if ! python3 -c 'import tomllib' >/dev/null 2>&1; then
    printf 'python3 has no tomllib module\tUse Python 3.11 or newer to read components.toml without third-party packages.\n'; return 1
  fi
  return 0
}

# The store listing and the track ladder are ordinary data on this host: no
# credential, no network, no Chromium checkout. The lane exists because until
# it did, the most widely read text this product will ever publish — its Play
# listing — was the one user-facing surface with no vocabulary gate and no
# field-limit check, and both of those are refusals at upload rather than
# warnings.
lane_ready_play() {
  if [ ! -f "$TAFFY_ROOT/tools/play.d/store.toml" ]; then
    printf 'the Play store configuration is not scaffolded\tCreate tools/play.d/store.toml and tools/lib/play_config.py together.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tEvery ./tools/play checker is standard-library-only.\n'; return 1
  fi
  if ! python3 -c 'import tomllib' >/dev/null 2>&1; then
    printf 'python3 has no tomllib module\tUse Python 3.11 or newer to read tools/play.d/store.toml.\n'; return 1
  fi
  return 0
}

lane_ready_evidence() {
  if [ ! -x "$TAFFY_ROOT/tools/evidence" ]; then
    printf 'the evidence-index tool is not scaffolded\tCreate tools/evidence and its schema together.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe evidence schema and integrity checks are standard-library-only.\n'; return 1
  fi
  return 0
}

lane_ready_catalog() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py" ]; then
    printf 'the model catalog generator is not scaffolded\tIt arrives with the model-router crate.\n'; return 1
  fi
  if [ ! -f "$TAFFY_ROOT/taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py" ]; then
    printf 'the asset catalog generator is not scaffolded\tIt arrives with the asset-plane crate.\n'; return 1
  fi
  if [ ! -f "$TAFFY_ROOT/taffy-core/third_party/cpython/tools/artifact_zip.py" ]; then
    printf 'the artifact packagers are not scaffolded\tThey arrive with taffy-core/third_party/cpython.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe catalog generator is stdlib-only; any host python3 runs it.\n'; return 1
  fi
  return 0
}

lane_ready_rust() {
  if [ ! -f "$TAFFY_ROOT/Cargo.toml" ]; then
    printf 'no cargo workspace at the repository root\tThe workspace arrives with WP-M0-01.\n'; return 1
  fi
  if ! have cargo; then
    printf 'cargo is not installed\tInstall rustup; rust-toolchain.toml then selects the pinned toolchain by itself.\n'; return 1
  fi
  return 0
}

# The //taffy product root's build graph. Three stdlib-only checkers, no checkout and no
# toolchain beyond python3 — which is why they were runnable by hand for months
# and gated by nothing.
lane_ready_rust-graph() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/services/core/tools/check_build_graph.py" ]; then
    printf 'the //taffy build graph is not scaffolded\tCreate the core-service graph and its checks together.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tAll three checkers are stdlib-only and need no checkout.\n'; return 1
  fi
  return 0
}

lane_ready_contracts() {
  local contract
  for contract in bip core-api core-service tool-runtime; do
    if [ ! -f "$TAFFY_ROOT/taffy-core/contracts/$contract/codegen/generate.py" ]; then
      printf 'taffy-core/contracts/%s is not scaffolded\tAll four product contracts are one gated set.\n' "$contract"
      return 1
    fi
  done
  if ! have python3; then
    printf 'python3 is not installed\tThe generator is stdlib-only; any host python3 runs it.\n'; return 1
  fi
  return 0
}

lane_ready_strings() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/resources/catalog/tools/check_strings.py" ]; then
    printf 'the //taffy string catalogues are not scaffolded\tCreate the product resources and their checker together.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe catalogue check is stdlib-only.\n'; return 1
  fi
  return 0
}

lane_ready_kotlin() {
  local sdk api platform
  if [ ! -x "$TAFFY_ROOT/gradlew" ]; then
    printf 'no Gradle wrapper at the repository root\tThe Android UI build arrives with WP-M0-07.\n'; return 1
  fi
  if [ ! -d "$TAFFY_ROOT/taffy-core/ui/android" ]; then
    printf 'taffy-core/ui/android is not scaffolded\tIt arrives with WP-M0-07.\n'; return 1
  fi
  if ! have java; then
    printf 'no JDK on PATH\tInstall the JDK recorded in TOOLCHAIN.md, then re-run.\n'; return 1
  fi
  if ! sdk=$(taffy_android_sdk_root); then
    local hint
    if hint=$(taffy_android_sdk_conventional); then
      printf 'no Android SDK is configured\tAn SDK exists at %s but nothing points at it: export ANDROID_HOME=%s\n' "$hint" "$hint"
    else
      printf 'no Android SDK is configured\tInstall the Android SDK command-line tools and platform-tools, export ANDROID_HOME, then run ./tools/bootstrap --profile android --only android for the pinned platform.\n'
    fi
    return 1
  fi
  api=$(taffy_android_compile_sdk 2>/dev/null) || api=''
  if [ -z "$api" ]; then
    printf 'gradle/libs.versions.toml records no compileSdk\tThe Android rows of TOOLCHAIN.md own that value.\n'; return 1
  fi
  if ! platform=$(taffy_android_platform_dir "$sdk" "$api"); then
    printf 'SDK platform android-%s is not installed in %s\tRun: ./tools/bootstrap --profile android --only android\n' "$api" "$sdk"
    return 1
  fi
  [ -n "$platform" ]
  return 0
}

lane_ready_website() {
  local blocker
  blocker=$(taffy_node_blocker website) && return 0
  printf '%s\n' "$blocker"
  return 1
}

lane_ready_fixtures() {
  if [ ! -f "$TAFFY_ROOT/test-fixtures/web/check.py" ]; then
    printf 'test-fixtures/web is not scaffolded\tIt arrives with WP-M0-08.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe corpus self-check is stdlib-only.\n'; return 1
  fi
  return 0
}

lane_ready_fixtures-tasks() {
  if [ ! -f "$TAFFY_ROOT/test-fixtures/tasks/check.py" ]; then
    printf 'test-fixtures/tasks is not scaffolded\tIt arrives with WP-M0-08, beside the page corpus.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe scenario corpus self-check is stdlib-only.\n'; return 1
  fi
  return 0
}

# Deliberately does not require cwebp. Deriving the icons needs the pinned
# encoder; verifying the committed ones needs only their digests and the pixel
# size in each file's own header, so this lane runs wherever python3 does.
lane_ready_icons() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/ui/android/tools/icons/generate_launcher_icons.py" ]; then
    printf 'the Android icon generator is not scaffolded\tIt arrives with the Compose UI, WP-M0-07.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe committed-icon check is stdlib-only and needs no image encoder.\n'; return 1
  fi
  return 0
}

lane_ready_mount() {
  if [ ! -f "$TAFFY_ROOT/taffy-core/app/android/tools/kotlin_mounts.py" ]; then
    printf 'the Kotlin mount contract is not scaffolded\tIt arrives with the fork shell, WP-M1-04.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tBoth mount tools are stdlib-only and need no checkout.\n'; return 1
  fi
  return 0
}

# The only lane that reads a real GN dependency graph. Every other lane here is
# answerable from this repository's own files; whether a *target* inside a build
# file GN does load is compiled into any binary is not, which is why
# taffy-core/build/tools/check_build_reachability.py names this command in its
# own header rather than trying to answer it. So the lane needs a synced
# checkout and an output directory somebody has run `gn gen` in, and it runs
# last because it is the only lane whose cost is a graph query rather than a
# file read.
#
# The predicate is the command's own `--lane-ready`, printing this contract's
# `reason<TAB>remediation`. Its preconditions are exactly the facts the sweep
# needs in order to run, and a second copy of them here would be a second
# opinion — the one thing ./tools/doctor exists to make impossible.
lane_ready_orphans() { "$TAFFY_ROOT/tools/chromium/orphan-targets" --lane-ready; }

# What a publication of this working tree would carry (decision 0251). Only
# the private repository holds the exporter: tools/export-public.d/exclude.tsv
# withholds it, so a tree it exported skips here by name rather than failing
# for want of a tool it is not meant to have.
lane_ready_public() {
  if [ ! -x "$TAFFY_ROOT/tools/export-public" ]; then
    printf 'this tree carries no exporter, which stays with the private source (decision 0251)\tNothing to do here: a published tree is what this lane produces, not what it checks.\n'; return 1
  fi
  if ! have python3; then
    printf 'python3 is not installed\tThe exporter and its verifier are standard-library Python 3.11 or newer.\n'; return 1
  fi
  if ! git -C "$TAFFY_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'not a Git checkout\tThe export copies what Git tracks or would track; run it in a clone.\n'; return 1
  fi
  return 0
}

# lane_ready <name> — dispatch, so callers iterate $TAFFY_COMPONENT_LANES
# without knowing the function names.
lane_ready() {
  case "$1" in
    architecture) lane_ready_architecture ;;
    evidence)  lane_ready_evidence ;;
    catalog)   lane_ready_catalog ;;
    rust)      lane_ready_rust ;;
    rust-graph) lane_ready_rust-graph ;;
    contracts) lane_ready_contracts ;;
    strings)   lane_ready_strings ;;
    kotlin)    lane_ready_kotlin ;;
    website)   lane_ready_website ;;
    fixtures)  lane_ready_fixtures ;;
    fixtures-tasks) lane_ready_fixtures-tasks ;;
    icons)     lane_ready_icons ;;
    mount)     lane_ready_mount ;;
    orphans)   lane_ready_orphans ;;
    play)      lane_ready_play ;;
    public)    lane_ready_public ;;
    *) printf 'no readiness predicate for lane "%s"\tThis is a defect in tools/lib/lanes.sh.\n' "$1"; return 1 ;;
  esac
}
