#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/release.d/rollback.sh — halt criteria, the rollback package, and the
# rehearsal.
#
# Authority boundary: this module owns the executable half of
# docs/development/testing-and-delivery.md section 16 — what must be in the
# rollback package, and which of the eight rehearsal steps can be proved on
# this host. It owns no threshold: every halt criterion below names the metric
# that owns its number (docs/quality/metrics.md) and never restates it.
#
# Owning milestone: M1 (WP-M1-07) for the mechanism; the full rehearsal is
# required before the M8 launch-readiness exit review (PAR-SEC-005, OD-062).
#
# What it deliberately does not do: it never halts a real deployment, never
# calls a store or a provider, and never touches a credential. It proves the
# package is complete and the procedure is runnable. Executing the procedure
# against production is a human decision recorded in the runbook
# (docs/development/release-and-incident-runbook.md).
#
# Sourced by ./tools/release. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_RELEASE_ROLLBACK_SH_LOADED:-}" ]; then return 0; fi
TAFFY_RELEASE_ROLLBACK_SH_LOADED=1

# --- halt criteria ----------------------------------------------------------
#
# id | what stops the deployment | who owns the number
#
# The middle column is a rule, not a value. A criterion whose threshold lived
# here would be a second source of truth for a number that already has one, and
# the first divergence would be found during an incident.
#
# Every register in this file is one single-quoted string, so no cell may
# contain an apostrophe -- "the person's provider" closes the string and the
# rest of the file becomes code. `bash -n` in the shell lane catches it, which
# is the only thing that does.

TAFFY_HALT_CRITERIA='HALT-01|Crash-free task sessions fall below the milestone target|MET-012 in docs/quality/metrics.md
HALT-02|ANR-free task sessions fall below the milestone target|MET-013 in docs/quality/metrics.md
HALT-03|Any disallowed high-risk action is observed|MET-009 — zero at every milestone
HALT-04|Any seeded or real secret is disclosed to a model, log, export or index|MET-010 — zero at every milestone
HALT-05|Any cross-tenant data access is observed|MET-011 — zero at every milestone
HALT-06|Manual browsing degrades when the assistant or the configured provider is unavailable|PAR-BR rows of docs/product/browser-parity-matrix.md
HALT-07|A durable-journal migration ships that an older installed client cannot tolerate|docs/development/testing-and-delivery.md section 11
HALT-08|An upstream security advisory lands that the shipped artifact does not carry|chromium/SECURITY_PATCH_LEVEL and OD-052'

# --- the rollback package ---------------------------------------------------
#
# name | file or directory expected in the package | why it is needed at 3am

TAFFY_ROLLBACK_PACKAGE='previous-manifest|manifest.json|The verified manifest of the artifact being rolled back to — without it there is nothing to prove the replacement is the artifact it claims to be
previous-artifact|*.aab or *.apk|The signed package that is being restored
previous-mapping|mapping.txt|Crashes arriving during the incident are unreadable without the mapping of the build users are on
previous-symbols|symbols*.zip|Native crashes during the incident are unreadable without symbols
migration-plan|migration-forward-fix.md|The forward fix for a bad but non-destructive migration; a backward migration that drops data is not a rollback
route-config|provider-routes.json|The provider and model route snapshot that can be changed without an app update
halt-procedure|halt-procedure.md|Who may halt the gradual deployment, where the control is, and the criteria they apply
contacts|contacts.md|On-call, escalation, and the security contact, with a channel that works when the usual one does not'

# --- the rehearsal ----------------------------------------------------------
#
# id | step | how this host proves it
#
# The seven steps are testing-and-delivery section 16, in its order -- it was
# eight until decision 0200 removed the Worker whose version the eighth rolled
# back to. That document and this list are one list in two files and must be
# changed together; a rehearsal against a list nobody wrote down proves nothing.
# "command"
# means a command in this repository runs it; "package" means the check is that
# the package carries what the step consumes; "environment" means the step
# needs a live system this repository does not own, and the rehearsal reports
# it as blocked rather than pretending.

TAFFY_ROLLBACK_STEPS='RB-1|Halt the gradual deployment|package:halt-procedure
RB-2|Disable one provider or model route by shipping a build|package:route-config
RB-3|Recover from a bad but non-destructive durable-journal migration with a forward fix|package:migration-plan
RB-4|Revoke and rotate the Play service account and the upload signing key without exposing either|environment:the credential stores; no credential is ever read by this repository
RB-5|Ship a simulated urgent Chromium security update|command:./tools/chromium/security-patch --self-test
RB-6|Export and delete one test data set on the device|environment:an installed build on the device matrix (OD-017)
RB-7|Recover an interrupted task without repeating a mutation|environment:an installed build on the device matrix (OD-017)'

# --- reporting --------------------------------------------------------------

TAFFY_ROLLBACK_FAIL=0
TAFFY_ROLLBACK_BLOCKED=0

release_rollback_criteria() {
  step "halt criteria (docs/development/testing-and-delivery.md section 16)"
  local id rule owner
  printf '%s\n' "$TAFFY_HALT_CRITERIA" | while IFS='|' read -r id rule owner; do
    [ -n "$id" ] || continue
    info "  $id  $rule"
    note "      number owned by: $owner"
  done
  info ""
  info "Any one criterion halts the gradual deployment. Halting is not a"
  info "decision that waits for a meeting; resuming is."
  return 0
}

# release_rollback_pattern <item-name> — the filename pattern that item uses.
# One lookup, so the contents check and the rehearsal can never disagree about
# what "the route-config input" is called.
release_rollback_pattern() {
  printf '%s\n' "$TAFFY_ROLLBACK_PACKAGE" \
    | awk -F'|' -v want="$1" '$1 == want { print $2; exit }'
}

# release_rollback_find <dir> <item-name> — the file, or nothing.
# Under `set -euo pipefail` an assignment whose command substitution fails is
# fatal, and `find` on a directory that does not exist is exactly the case this
# check exists to report — hence the guard on every pipeline below.
release_rollback_find() {
  local dir=$1 pattern first second
  pattern=$(release_rollback_pattern "$2")
  [ -n "$pattern" ] || return 0
  case "$pattern" in
    *' or '*)
      first=${pattern%% or *}; second=${pattern##* or }
      find "$dir" -maxdepth 2 \( -name "$first" -o -name "$second" \) 2>/dev/null \
        | head -n 1 || true
      ;;
    *) find "$dir" -maxdepth 2 -name "$pattern" 2>/dev/null | head -n 1 || true ;;
  esac
}

# release_rollback_check_package <dir>
release_rollback_check_package() {
  local dir=$1 name pattern why found missing=0
  step "rollback package: ${dir}"
  if [ ! -d "$dir" ]; then
    # Reported, and then the contents check still runs: an operator with no
    # package needs the list of what to assemble, not one line saying no.
    bad "no rollback package at $dir"
    note "Assemble it with the contents listed below, then re-run."
    missing=$((missing + 1))
  fi
  # A here-document, not a pipe: bash 3.2 runs the right side of a pipe in a
  # subshell, and a counter incremented in a subshell always reports zero.
  while IFS='|' read -r name pattern why; do
    [ -n "$name" ] || continue
    found=$(release_rollback_find "$dir" "$name")
    if [ -n "$found" ]; then
      ok "$name — ${found#"$dir"/}"
    else
      bad "$name — no $pattern in $dir"
      note "$why"
      missing=$((missing + 1))
    fi
  done <<EOF
$TAFFY_ROLLBACK_PACKAGE
EOF
  TAFFY_ROLLBACK_FAIL=$((TAFFY_ROLLBACK_FAIL + missing))
  return 0
}

# release_rollback_rehearse <package-dir>
release_rollback_rehearse() {
  local dir=$1 id description proof kind detail
  step "rollback rehearsal (docs/development/testing-and-delivery.md section 16)"
  while IFS='|' read -r id description proof; do
    [ -n "$id" ] || continue
    kind=${proof%%:*}
    detail=${proof#*:}
    case "$kind" in
      command)
        note "$id  $description"
        note "\$ $detail"
        if ( cd "$TAFFY_ROOT" && eval "$detail" >/dev/null 2>&1 ); then
          ok "$id passed: $detail"
        else
          bad "$id failed: $detail"
          note "Reproduce it directly; a rehearsal step that fails here fails in an incident."
          TAFFY_ROLLBACK_FAIL=$((TAFFY_ROLLBACK_FAIL + 1))
        fi
        ;;
      package)
        if [ -n "$(release_rollback_find "$dir" "$detail")" ]; then
          ok "$id ready: the package carries its $detail input"
        else
          bad "$id not ready: the package carries no $detail input"
          note "$description needs it. Add it to the rollback package."
          TAFFY_ROLLBACK_FAIL=$((TAFFY_ROLLBACK_FAIL + 1))
        fi
        ;;
      environment)
        warn "$id blocked: $description"
        note "Needs $detail."
        TAFFY_ROLLBACK_BLOCKED=$((TAFFY_ROLLBACK_BLOCKED + 1))
        ;;
    esac
  done <<EOF
$TAFFY_ROLLBACK_STEPS
EOF
  return 0
}
