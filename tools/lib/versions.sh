#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/versions.sh — read one recorded version out of its machine owner file.
#
# TOOLCHAIN.md is the only human-readable index of versions, and every row in it
# names exactly one machine owner file that CI treats as truth. Owner files are
# written in four different formats, so this module is the one place that knows
# how to read each of them. Nothing here knows what a row means; that is
# lib/pins.sh.
#
# Sourced, never executed. Targets bash 3.2: no associative arrays, no mapfile,
# no process substitution.
#
# Deliberately not a parser. Each reader recognises a *scalar assignment* and
# nothing else — no nesting, no arrays, no inline tables. That is enough for
# every owner file in this repository and it fails loudly rather than guessing:
# a key that appears twice with two different values is an error, not a coin
# toss (see taffy_version_value).
#
# shellcheck shell=bash

if [ -n "${TAFFY_VERSIONS_SH_LOADED:-}" ]; then return 0; fi
TAFFY_VERSIONS_SH_LOADED=1

# --- readers ----------------------------------------------------------------
# Each prints every value it finds for the key, one per line, in file order.
# A reader that finds nothing prints nothing and still succeeds; the caller
# decides whether that is a defect.

# `key=value`, `#` comments — tools/pins/*, chromium/REVISION.
taffy_version_read_kv() {
  local file=$1 key=$2
  [ -f "$file" ] || return 0
  sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" "$file" \
    | sed 's/[[:space:]]*$//'
}

# `key = "value"` or `key = 17` — gradle/libs.versions.toml, rust-toolchain.toml,
# rust-toolchain.toml. Values that are not scalars (a plugin alias table in the
# catalog's [plugins] block, for example) are skipped, so a version key and a
# plugin alias may share a name without colliding.
taffy_version_read_toml() {
  local file=$1 key=$2
  [ -f "$file" ] || return 0
  sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" "$file" \
    | sed 's/[[:space:]]*$//' \
    | grep -E '^("[^"]*"|[0-9][0-9A-Za-z._+-]*)$' \
    | sed -e 's/^"//' -e 's/"$//' || true
}

# `"key": "value"` — package.json, and any JSONC beside it: comments and
# trailing commas are irrelevant to a scalar match, which is why this reader
# survives them.
taffy_version_read_json() {
  local file=$1 key=$2
  [ -f "$file" ] || return 0
  sed -n "s/.*\"${key}\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" "$file"
}

# The Gradle version inside gradle-wrapper.properties' distributionUrl. The URL
# is the owner of the row because it is what the wrapper actually downloads, and
# it is checksum-verified by distributionSha256Sum in the same file.
taffy_version_read_gradle_dist() {
  local file=$1
  [ -f "$file" ] || return 0
  # Extended regex: the alternation below is not portable BRE (BSD sed on macOS
  # rejects \|), and this file has to read the same on macOS and Linux.
  sed -n -E 's@^distributionUrl=.*gradle-([0-9][0-9A-Za-z._-]*)-(bin|all)\.zip.*@\1@p' "$file"
}

# --- single-value contract --------------------------------------------------

# taffy_version_values <reader> <file> <key>
# Every distinct value the reader found, one per line.
taffy_version_values() {
  local reader=$1 file=$2 key=${3:-} raw
  # The reader runs outside the pipeline on purpose: a `case ... esac | sed`
  # would swallow the unknown-reader status in a subshell.
  case "$reader" in
    kv|toml|json) raw=$("taffy_version_read_$reader" "$file" "$key") ;;
    gradle-dist)  raw=$(taffy_version_read_gradle_dist "$file") ;;
    *) printf 'taffy: unknown owner-file reader: %s\n' "$reader" >&2; return 2 ;;
  esac
  printf '%s\n' "$raw" | sed '/^$/d' | sort -u
}

# taffy_version_value <reader> <file> <key>
# Prints the one value the owner file records for the key.
#   0  exactly one value, printed on stdout
#   1  the file or the key is missing
#   2  the key is recorded more than once with different values, or the reader
#      name is unknown; the conflicting values go to stderr
taffy_version_value() {
  local values count
  values=$(taffy_version_values "$@") || return 2
  count=$(printf '%s' "$values" | grep -c . || true)
  case "$count" in
    0) return 1 ;;
    1) printf '%s\n' "$values" ;;
    *) printf '%s\n' "$values" >&2; return 2 ;;
  esac
}
