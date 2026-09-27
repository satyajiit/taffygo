#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/report.sh — the finding store behind ./tools/doctor.
#
# Authority boundary: this module owns *how* a read-only audit accumulates and
# prints findings, and nothing about what is audited. Adding a check never
# touches this file; adding an output format only touches this file.
#
# Owning milestone: M0 (WP-M0-09).
#
# A finding is five fields: section, label, status, value, hint. Status is one
# of ok / warn / fail / info. `info` is the honest default: most of what a host
# audit reports is neither a pass nor a problem, and inflating it into either
# would make the summary meaningless.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_REPORT_SH_LOADED:-}" ]; then return 0; fi
TAFFY_REPORT_SH_LOADED=1

# One tab-separated line per finding.
TAFFY_FINDINGS=()
TAFFY_N_FAIL=0
TAFFY_N_WARN=0

record() { # <section> <label> <status> <value> [hint]
  TAFFY_FINDINGS+=("$1"$'\t'"$2"$'\t'"$3"$'\t'"$4"$'\t'"${5:-}")
  case "$3" in
    fail) TAFFY_N_FAIL=$((TAFFY_N_FAIL + 1)) ;;
    warn) TAFFY_N_WARN=$((TAFFY_N_WARN + 1)) ;;
  esac
  return 0
}

# version_of <command> [args...] -> first line of its version output, or ''
version_of() {
  local cmd=$1; shift
  have "$cmd" || return 1
  "$cmd" "$@" 2>&1 | head -n 1 | sed 's/[[:space:]]*$//'
}

report_print_text() {
  local last='' line section label status value hint
  for line in "${TAFFY_FINDINGS[@]}"; do
    section=${line%%$'\t'*}; line=${line#*$'\t'}
    label=${line%%$'\t'*};   line=${line#*$'\t'}
    status=${line%%$'\t'*};  line=${line#*$'\t'}
    value=${line%%$'\t'*};   hint=${line#*$'\t'}
    if [ "$section" != "$last" ]; then
      printf '\n%s%s%s\n' "$C_BOLD" "$section" "$C_RESET"
      last=$section
    fi
    case "$status" in
      ok)   printf '  %sok%s   ' "$C_GREEN" "$C_RESET" ;;
      warn) printf '  %swarn%s ' "$C_YELLOW" "$C_RESET" ;;
      fail) printf '  %sfail%s ' "$C_RED" "$C_RESET" ;;
      *)    printf '       ' ;;
    esac
    printf '%-32s %s\n' "$label" "$value"
    [ -n "$hint" ] && printf '         %s%s%s\n' "$C_DIM" "$hint" "$C_RESET"
  done
  printf '\n'
  if [ "$TAFFY_N_FAIL" -gt 0 ]; then
    printf '%s%d failing, %d warning%s\n' "$C_RED" "$TAFFY_N_FAIL" "$TAFFY_N_WARN" "$C_RESET"
  elif [ "$TAFFY_N_WARN" -gt 0 ]; then
    printf '%s%d warning(s), nothing failing%s\n' "$C_YELLOW" "$TAFFY_N_WARN" "$C_RESET"
  else
    printf '%sno problems found%s\n' "$C_GREEN" "$C_RESET"
  fi
  return 0
}

report_print_json() {
  local line section label status value hint first=1
  printf '{\n  "tool": "taffy-doctor",\n  "findings": [\n'
  for line in "${TAFFY_FINDINGS[@]}"; do
    section=${line%%$'\t'*}; line=${line#*$'\t'}
    label=${line%%$'\t'*};   line=${line#*$'\t'}
    status=${line%%$'\t'*};  line=${line#*$'\t'}
    value=${line%%$'\t'*};   hint=${line#*$'\t'}
    [ "$first" = 1 ] || printf ',\n'
    first=0
    printf '    {"section": "%s", "label": "%s", "status": "%s", "value": "%s", "hint": "%s"}' \
      "$(taffy_json_escape "$section")" "$(taffy_json_escape "$label")" \
      "$(taffy_json_escape "$status")" "$(taffy_json_escape "$value")" \
      "$(taffy_json_escape "$hint")"
  done
  printf '\n  ],\n  "failing": %d,\n  "warnings": %d\n}\n' "$TAFFY_N_FAIL" "$TAFFY_N_WARN"
  return 0
}
