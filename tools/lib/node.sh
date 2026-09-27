#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/node.sh — readiness of the JS workspace (the Worker and the website).
#
# Authority boundary: package.json owns the Node and pnpm rows of TOOLCHAIN.md,
# pnpm-workspace.yaml owns which directories are members. This module reads both
# and reports whether a package can be exercised on this host. It installs
# nothing — that is ./tools/bootstrap — and it runs no package script; the lanes
# and ./tools/website do that.
#
# Owning milestone: M0 (WP-M0-09), serving WP-M0-04 and WP-M0-06.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_NODE_SH_LOADED:-}" ]; then return 0; fi
TAFFY_NODE_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR source=versions.sh
. "$(dirname "${BASH_SOURCE[0]}")/versions.sh"

# --- the recorded pins ------------------------------------------------------

taffy_node_engine_range() {
  taffy_version_value json "$TAFFY_ROOT/package.json" node
}

# `pnpm@<version>` exactly as Corepack reads it.
taffy_node_package_manager() {
  taffy_version_value json "$TAFFY_ROOT/package.json" packageManager
}

taffy_node_pnpm_pin() {
  local pm; pm=$(taffy_node_package_manager) || return 1
  printf '%s\n' "${pm#pnpm@}"
}

# --- host comparison --------------------------------------------------------
#
# Not a semver range evaluator. It understands exactly the shape package.json
# records here — a lower bound and an exclusive upper bound on the major
# version — and says "not compared" for anything else rather than guessing.
#
# taffy_node_engine_verdict <running-version>
#   prints one of: ok | too-old | too-new | not-compared

taffy_node_engine_verdict() {
  local running=$1 range low high major
  range=$(taffy_node_engine_range) || { printf 'not-compared\n'; return 0; }
  low=$(printf '%s' "$range" | sed -n 's/.*>=[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  high=$(printf '%s' "$range" | sed -n 's/.*<[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  major=$(printf '%s' "$running" | sed -n 's/^v\{0,1\}\([0-9][0-9]*\).*/\1/p')
  if [ -z "$low" ] || [ -z "$high" ] || [ -z "$major" ]; then
    printf 'not-compared\n'; return 0
  fi
  if [ "$major" -lt "$low" ]; then printf 'too-old\n'
  elif [ "$major" -ge "$high" ]; then printf 'too-new\n'
  else printf 'ok\n'
  fi
}

# The scripts that make up one package's check, in the order the CI gate runs
# them: lint before types before tests, so the cheapest failure is reported
# first. Declared once here because ./tools/check's lanes and ./tools/website
# must run the same list or one of them is lying.
# shellcheck disable=SC2034  # read by ./tools/check and ./tools/website
TAFFY_NODE_CHECK_SCRIPTS='lint typecheck test'

# --- package readiness ------------------------------------------------------

# taffy_node_blocker <package-dir>   (path relative to the repository root)
#   0  the package can be exercised; prints nothing
#   1  it cannot; prints "<reason>\t<remediation>"
taffy_node_blocker() {
  local dir=$1 path="$TAFFY_ROOT/$1"
  if [ ! -f "$path/package.json" ]; then
    printf '%s is not scaffolded\tIt arrives with its work package; see docs/development/implementation-plan.md.\n' "$dir"
    return 1
  fi
  if ! have pnpm; then
    printf 'pnpm is not installed\tRun: corepack enable pnpm   (the version is pinned by package.json#packageManager)\n'
    return 1
  fi
  if [ ! -d "$path/node_modules" ]; then
    printf '%s has no node_modules\tRun: pnpm install --frozen-lockfile   (or ./tools/bootstrap --profile android)\n' "$dir"
    return 1
  fi
  return 0
}
