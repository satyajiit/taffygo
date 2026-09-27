#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/chromium/lib/security_queue.sh — the urgent-security cherry-pick queue.
#
# Authority boundary: this module owns the files the fast path writes —
# chromium/patches/security/, its numbering, what counts as an applicable patch,
# and chromium/SECURITY_PATCH_LEVEL. The command line around it is
# ./tools/chromium/security-patch; the response clock is lib/drill.py.
# Procedure: docs/development/chromium-fork-and-build.md section 1.4.
#
# Owning milestone: M1 (WP-M1-08).
#
# What it deliberately does not do: it never edits a patch's contents, never
# invents a patch header, and never decides whether a fix is needed. The
# editing surface for upstream changes is the taffy/patched branch, and
# ./tools/chromium/export-patches is the queue's only other writer.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_SECURITY_QUEUE_SH_LOADED:-}" ]; then return 0; fi
TAFFY_SECURITY_QUEUE_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR source=chromium.sh
. "$(dirname "${BASH_SOURCE[0]}")/chromium.sh"

# shellcheck disable=SC2034  # read by ./tools/chromium/security-patch
SECURITY_DIR="$TAFFY_ROOT/chromium/patches/security"
LEVEL_FILE="$TAFFY_ROOT/chromium/SECURITY_PATCH_LEVEL"
# shellcheck disable=SC2034  # read by ./tools/chromium/security-patch
RECORD_ROOT="$TAFFY_ROOT/artifacts/security-drill"

slug() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' \
    | sed -e 's/[^a-z0-9]\{1,\}/-/g' -e 's/^-//' -e 's/-$//'
}

record_path_for() { # <advisory> <explicit>
  if [ -n "${2:-}" ]; then printf '%s\n' "$2"; else printf '%s/%s.json\n' "$RECORD_ROOT" "$(slug "$1")"; fi
}

# The next free queue number across both patch directories. Numbers name one
# logical change for its whole life (chromium/patches/README.md), so a security
# cherry-pick never reuses one.
next_patch_number() {
  local highest=0 name number
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    number=$(basename "$name" | sed -n 's/^\([0-9]\{4\}\)-.*/\1/p')
    [ -n "$number" ] || continue
    number=$(printf '%s' "$number" | sed 's/^0*//')
    [ -n "$number" ] || number=0
    [ "$number" -gt "$highest" ] && highest=$number
  done <<EOF
$(find "$TAFFY_ROOT/chromium/patches" -maxdepth 2 \( -name '*.patch' -o -name '*.md' \) 2>/dev/null || true)
EOF
  printf '%04d\n' $((highest + 1))
}

# A patch this repository can actually apply. ./tools/chromium/sync applies the
# queue with `git am`, which needs a mailbox header, and
# ./tools/chromium/export-patches routes a patch back into this directory by
# looking for "security:" in its subject. A patch that fails either of those is
# a patch that silently leaves the queue at the next export.
validate_patch_file() {
  local patch=$1
  [ -f "$patch" ] || die "no such patch file: $patch"
  grep -qE '^From [0-9a-f]{40}' "$patch" || die \
    "$patch is not a git mailbox patch" \
    "The queue is applied with \`git am\`, which needs the From/Subject header." \
    "Export it with: git -C <checkout> format-patch -1 --no-signature <commit>"
  grep -qE '^Subject: .*security:' "$patch" || die \
    "$patch has no \"security:\" in its subject line" \
    "./tools/chromium/export-patches routes a patch into chromium/patches/security/" \
    "by that marker. Without it the next export drops the cherry-pick from this" \
    "directory, and the fix leaves the build without anyone noticing." \
    "Amend the commit subject to start with \"security: \" and re-export."
}

patch_upstream_commit() { sed -n 's/^From \([0-9a-f]\{40\}\).*/\1/p' "$1" | head -n 1; }
patch_subject() { sed -n 's/^Subject: \(\[PATCH[^]]*\] \)\{0,1\}//p' "$1" | head -n 1; }
patch_digest() { python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

current_level() { taffy_kv_get "$LEVEL_FILE" level 2>/dev/null || printf '0'; }

record_advisory_in_level_file() { # <advisory>
  local existing
  existing=$(taffy_kv_get "$LEVEL_FILE" advisories 2>/dev/null || printf 'none')
  case "$existing" in
    ''|none) printf '%s\n' "$1" ;;
    *"$1"*)  printf '%s\n' "$existing" ;;
    *)       printf '%s,%s\n' "$existing" "$1" ;;
  esac
}

