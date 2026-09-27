#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/pins.sh — prove TOOLCHAIN.md agrees with every machine owner file.
#
# Authority boundary: TOOLCHAIN.md is the only human-readable index of versions,
# and every row in it names exactly one machine owner file that CI treats as
# truth. This module owns the mapping between the two and nothing else. How an
# owner file is read is lib/versions.sh; what a version is used for is the
# component that owns it.
#
# Owning milestone: M0 (WP-M0-09).
#
# What it deliberately does not do: it never writes an owner file, never guesses
# a value, and never accepts a row it cannot check. A row that records a real
# value with no register entry below is reported, because an unchecked pin is
# indistinguishable from a wrong one.
#
# Sourced by ./tools/check after its reporting helpers (pass/soft/hard/note) and
# $TMP exist. Never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_PINS_SH_LOADED:-}" ]; then return 0; fi
TAFFY_PINS_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR source=versions.sh
. "$(dirname "${BASH_SOURCE[0]}")/versions.sh"

# --- the register -----------------------------------------------------------
#
# label | owner file | reader | key
#
# `label` is the exact text of the row's first column in TOOLCHAIN.md. The check
# is symmetric: the row must carry the owner's value *and* name the owner file,
# so neither half can drift without failing.
#
# Rows absent from this register are rows whose owner file does not exist yet.
# Each of those carries TO-VERIFY in TOOLCHAIN.md and is retired by the
# milestone that owns it; check_pins reports any row that has a value but no
# entry here.
TAFFY_PIN_REGISTER='Chromium revision (stable milestone tag)|chromium/REVISION|kv|commit
Chromium security patch level|chromium/SECURITY_PATCH_LEVEL|kv|level
depot_tools revision|tools/pins/depot_tools|kv|revision
Android Gradle Plugin|gradle/libs.versions.toml|toml|agp
Gradle|gradle/wrapper/gradle-wrapper.properties|gradle-dist|
Kotlin|gradle/libs.versions.toml|toml|kotlin
Compose BOM|gradle/libs.versions.toml|toml|composeBom
JDK (Android UI)|gradle/libs.versions.toml|toml|jvmToolchain
Android compileSdk|gradle/libs.versions.toml|toml|compileSdk
Android targetSdk|gradle/libs.versions.toml|toml|targetSdk
Android minSdk|gradle/libs.versions.toml|toml|minSdk
Dagger|gradle/libs.versions.toml|toml|dagger
CommonMark Java (core and GFM tables)|gradle/libs.versions.toml|toml|commonmark
KSP|gradle/libs.versions.toml|toml|ksp
androidx SplashScreen|gradle/libs.versions.toml|toml|splashscreen
cwebp (Android launcher icons)|taffy-core/ui/android/tools/icons/manifest.json|json|cwebp
flag-icons (country-flag pack, MIT)|taffy-core/third_party/flag-icons/tools/manifest.json|json|version
librsvg (country-flag pack rasteriser)|taffy-core/third_party/flag-icons/tools/manifest.json|json|librsvg
cwebp (country-flag pack)|taffy-core/third_party/flag-icons/tools/manifest.json|json|cwebp
Space Grotesk (variable font asset, OFL)|taffy-core/ui/android/core/designsystem/vendor/space-grotesk.txt|kv|sha256
Phosphor Icons (ImageVector subset, MIT)|taffy-core/ui/android/core/ui/vendor/phosphor.txt|kv|glyphs
Provider brand marks (ImageVector subset, trademarks)|taffy-core/ui/android/core/ui/vendor/provider-marks.txt|kv|sha256
Rust toolchain (portable host loop)|rust-toolchain.toml|toml|channel
ccache|tools/pins/build-cache|kv|ccache
sccache|tools/pins/build-cache|kv|sccache
Node.js|package.json|json|node
pnpm|package.json|json|packageManager
TypeScript (website)|website/package.json|json|typescript
Next.js|website/package.json|json|next
React|website/package.json|json|react
Tailwind CSS|website/package.json|json|tailwindcss
lucide-react|website/package.json|json|lucide-react
Lighthouse|website/package.json|json|lighthouse
gplay (Play Console CLI)|tools/pins/gplay|kv|version
CPython (vendored interpreter, decision 0046)|taffy-core/third_party/cpython/tools/manifest.json|json|version'

# --- TOOLCHAIN.md ------------------------------------------------------------

TAFFY_TOOLCHAIN_FILE_DEFAULT="$TAFFY_ROOT/TOOLCHAIN.md"

# Every data row of the "## Pins" table — and only that table, so the prose
# tables elsewhere in TOOLCHAIN.md are never mistaken for pins:
#   "<label>\t<pin cell>\t<owner cell>"
taffy_pin_rows() {
  awk -F'|' '
    /^## / { inpins = ($0 == "## Pins"); next }
    !inpins { next }
    NF >= 4 {
      label = $2; pin = $3; owner = $4
      gsub(/^[ \t]+|[ \t]+$/, "", label)
      gsub(/^[ \t]+|[ \t]+$/, "", pin)
      gsub(/^[ \t]+|[ \t]+$/, "", owner)
      if (label == "" || label == "Component") next
      if (label ~ /^-+$/) next
      printf "%s\t%s\t%s\n", label, pin, owner
    }' "${TAFFY_TOOLCHAIN_FILE:-$TAFFY_TOOLCHAIN_FILE_DEFAULT}"
}

# taffy_pin_row <label> -> "<pin cell>\t<owner cell>" for the pins-table row
# whose Component cell is exactly <label>. Prints nothing when none matches.
taffy_pin_row() {
  taffy_pin_rows | awk -F'\t' -v want="$1" '$1 == want { printf "%s\t%s\n", $2, $3; exit }'
}

taffy_pin_registered() { # <label> -> 0 when the register covers this row
  printf '%s\n' "$TAFFY_PIN_REGISTER" \
    | awk -F'|' -v want="$1" '$1 == want { found = 1 } END { exit !found }'
}

# --- the gate ---------------------------------------------------------------

# Writes one "<STATUS>\t<message>" line per register entry into $1.
taffy_pin_findings() {
  local out=$1
  : > "$out"
  printf '%s\n' "$TAFFY_PIN_REGISTER" | while IFS='|' read -r label owner reader key; do
    [ -n "$label" ] || continue
    local path row cell owner_cell value rc
    path="$TAFFY_ROOT/$owner"

    if [ ! -f "$path" ]; then
      printf 'FAIL\t%s: machine owner file is missing: %s\n' "$label" "$owner" >> "$out"
      continue
    fi

    row=$(taffy_pin_row "$label")
    if [ -z "$row" ]; then
      printf 'FAIL\t%s: no TOOLCHAIN.md row has this exact Component cell\n' "$label" >> "$out"
      continue
    fi
    cell=${row%%	*}
    owner_cell=${row#*	}

    rc=0
    value=$(taffy_version_value "$reader" "$path" "$key" 2>/dev/null) || rc=$?
    case "$rc" in
      1) printf 'FAIL\t%s: %s records no value for "%s"\n' "$label" "$owner" "$key" >> "$out"
         continue ;;
      2) printf 'FAIL\t%s: %s records "%s" more than once with different values\n' \
           "$label" "$owner" "$key" >> "$out"
         continue ;;
    esac

    # The owner cell names the file in backticks. Requiring the backticks is
    # what stops `website/package.json` from satisfying a row that owns
    # `package.json`.
    case "$owner_cell" in
      *'`'"$owner"'`'*) : ;;
      *) printf 'FAIL\t%s: TOOLCHAIN.md names owner "%s", not the file %s\n' \
           "$label" "$owner_cell" "$owner" >> "$out"
         continue ;;
    esac

    if [ "$value" = "$TAFFY_UNPINNED" ]; then
      case "$cell" in
        *"$TAFFY_UNPINNED"*) printf 'WARN\t%s: unpinned in %s\n' "$label" "$owner" >> "$out" ;;
        *) printf 'FAIL\t%s: TOOLCHAIN.md says "%s" but %s is %s\n' \
             "$label" "$cell" "$owner" "$TAFFY_UNPINNED" >> "$out" ;;
      esac
      continue
    fi

    case "$cell" in
      *"$value"*) printf 'OK\t%s = %s\n' "$label" "$value" >> "$out" ;;
      *) printf 'FAIL\t%s: TOOLCHAIN.md says "%s" but %s says "%s"\n' \
           "$label" "$cell" "$owner" "$value" >> "$out" ;;
    esac
  done
}

# check_pins — the ./tools/check fast lane. Uses pass/soft/hard/note from
# ./tools/check and $TMP for its scratch file.
check_pins() {
  step "pins: TOOLCHAIN.md agrees with every machine owner file"

  taffy_pin_findings "$TMP/pins.txt"
  local status message
  while IFS=$'\t' read -r status message; do
    case "$status" in
      OK) pass "$message" ;;
      WARN) soft "$message" ;;
      FAIL) hard "$message" ;;
    esac
  done < "$TMP/pins.txt"

  # A row that records a real value but has no register entry is a pin nothing
  # verifies. Name it rather than counting it.
  local label cell owner_cell pending=0 unchecked=0
  taffy_pin_rows > "$TMP/pin-rows.txt"
  while IFS=$'\t' read -r label cell owner_cell; do
    case "$cell" in
      *"$TAFFY_UNPINNED"*) pending=$((pending + 1)); continue ;;
    esac
    if ! taffy_pin_registered "$label"; then
      soft "TOOLCHAIN.md row \"$label\" records a pin that no register entry checks"
      unchecked=$((unchecked + 1))
    fi
  done < "$TMP/pin-rows.txt"
  [ "$unchecked" -eq 0 ] && pass "every recorded pin is machine-checked"

  note "$pending TOOLCHAIN.md row(s) still carry $TAFFY_UNPINNED; each is retired by the milestone that owns it"
}
