#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/bootstrap_android.sh — install and verify the Android host loop.
#
# Authority boundary: docs/development/development-setup.md section 7 defines
# what the Android host loop is; the machine owner files behind TOOLCHAIN.md
# define which versions. This module installs exactly what those two say and
# invents nothing. It never touches the Chromium environment, which has its own
# host rule and its own profile.
#
# Owning milestone: M0 (WP-M0-09), serving the Compose UI of WP-M0-07.
#
# What it deliberately does not do: it does not accept an Android SDK licence on
# anyone's behalf (sdkmanager prompts for that itself), it does not install a
# JDK or a Node runtime — those are host prerequisites a package manager owns —
# and it never writes a tracked file.
#
# Sourced by ./tools/bootstrap, which supplies `run`, `confirm`, $DRY_RUN and
# the step filter. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_BOOTSTRAP_ANDROID_SH_LOADED:-}" ]; then return 0; fi
TAFFY_BOOTSTRAP_ANDROID_SH_LOADED=1

# --- host prerequisites ------------------------------------------------------

android_step_tools() {
  step "android: host prerequisites"
  local missing=0 pinned running

  if have git; then ok "git $(git --version | awk '{print $3}')"; else
    warn "git is missing"; missing=$((missing + 1)); fi

  pinned=$(taffy_android_jvm_toolchain 2>/dev/null || printf '')
  if have java; then
    running=$(taffy_java_major || printf '?')
    if [ -n "$pinned" ] && [ "$running" != "$pinned" ]; then
      warn "JDK $running on PATH, but gradle/libs.versions.toml records $pinned as the Gradle toolchain"
      note "Gradle can provision a toolchain, but the Android host loop is tested on $pinned."
    else
      ok "JDK $running"
    fi
  else
    warn "no JDK on PATH — the Android Gradle build needs JDK ${pinned:-see TOOLCHAIN.md}"
    missing=$((missing + 1))
  fi

  pinned=$(taffy_version_value toml "$TAFFY_ROOT/rust-toolchain.toml" channel 2>/dev/null || printf '')
  if have rustup; then
    ok "rustup present (rust-toolchain.toml selects ${pinned:-the pinned channel})"
  elif have cargo; then
    warn "cargo without rustup — the pinned toolchain ${pinned:-in rust-toolchain.toml} cannot be selected automatically"
  else
    warn "rustup is missing — taffy-core's Rust workspace cannot be built or tested"
    missing=$((missing + 1))
  fi

  if have node; then
    case "$(taffy_node_engine_verdict "$(node --version)")" in
      ok) ok "node $(node --version)" ;;
      not-compared) note "node $(node --version); package.json#engines could not be compared" ;;
      *) warn "node $(node --version) is outside package.json#engines ($(taffy_node_engine_range))"
         missing=$((missing + 1)) ;;
    esac
  else
    warn "node is missing"; missing=$((missing + 1))
  fi

  if [ "$missing" -gt 0 ]; then
    info ""
    warn "$missing prerequisite(s) missing (above); TOOLCHAIN.md gives the pinned version of each"
  fi
  return 0
}

# --- rust --------------------------------------------------------------------

android_step_rust() {
  step "android: portable Rust toolchain"
  if [ ! -f "$TAFFY_ROOT/rust-toolchain.toml" ]; then
    note "no rust-toolchain.toml — nothing to install"
    return 0
  fi
  if ! have rustup; then
    warn "rustup is not installed; skipping the portable Rust toolchain"
    note "Install rustup, then re-run: ./tools/bootstrap --profile android --only rust"
    return 0
  fi

  # Materialises the channel rust-toolchain.toml pins, so the first cargo
  # invocation is not a surprise download in the middle of a check run.
  run rustup show >/dev/null

  ran_ok "portable Rust toolchain selected"
  return 0
}

# --- node --------------------------------------------------------------------

android_step_node() {
  step "android: JS workspace"
  if [ ! -f "$TAFFY_ROOT/package.json" ]; then
    note "no package.json — nothing to install"
    return 0
  fi
  local pin
  pin=$(taffy_node_pnpm_pin 2>/dev/null || printf '')
  if ! have pnpm; then
    if have corepack; then
      run corepack enable pnpm
    else
      warn "neither pnpm nor corepack is available"
      note "Corepack ships with Node; install Node in the range TOOLCHAIN.md pins and re-run."
      return 0
    fi
  fi
  [ -n "$pin" ] && note "package.json#packageManager pins pnpm $pin; Corepack honours it automatically"

  # --frozen-lockfile: an install that silently rewrites pnpm-lock.yaml is a
  # reproducibility break, not a convenience.
  run pnpm --dir "$TAFFY_ROOT" install --frozen-lockfile
  ran_ok "workspace dependencies installed"
  return 0
}

# --- gradle ------------------------------------------------------------------

android_step_gradle() {
  step "android: Gradle wrapper validation"
  local props="$TAFFY_ROOT/gradle/wrapper/gradle-wrapper.properties"
  if [ ! -f "$props" ]; then
    note "no Gradle wrapper — nothing to validate"
    return 0
  fi

  local version sum failures=0
  version=$(taffy_version_value gradle-dist "$props" 2>/dev/null || printf '')
  [ -n "$version" ] || { bad "distributionUrl does not name a Gradle version"; failures=$((failures + 1)); }

  sum=$(taffy_kv_get "$props" distributionSha256Sum 2>/dev/null || printf '')
  if [ -z "$sum" ]; then
    bad "distributionSha256Sum is not set — the wrapper would accept any distribution it downloads"
    failures=$((failures + 1))
  else
    ok "distribution checksum pinned (${#sum} hex characters)"
  fi

  if grep -q '^validateDistributionUrl=true' "$props"; then
    ok "validateDistributionUrl=true"
  else
    bad "validateDistributionUrl is not true"
    failures=$((failures + 1))
  fi

  case "$(taffy_kv_get "$props" distributionUrl || printf '')" in
    https*) ok "distribution served over https" ;;
    *) bad "distributionUrl is not https"; failures=$((failures + 1)) ;;
  esac

  [ -f "$TAFFY_ROOT/gradle/wrapper/gradle-wrapper.jar" ] || {
    bad "gradle/wrapper/gradle-wrapper.jar is missing"; failures=$((failures + 1)); }
  [ -x "$TAFFY_ROOT/gradlew" ] || { bad "./gradlew is not executable"; failures=$((failures + 1)); }

  if [ "$failures" -gt 0 ]; then
    die "the Gradle wrapper is not trustworthy ($failures problem(s))" \
      "Restore it with a Gradle release you trust: gradle wrapper --gradle-version <version> --gradle-distribution-sha256-sum <sum>" \
      "Never commit a wrapper without its checksum."
  fi

  if ! have java; then
    warn "no JDK on PATH — cannot materialise the distribution here"
    note "The first ./gradlew invocation on a machine with a JDK downloads and checksum-verifies it."
    return 0
  fi

  # The wrapper itself enforces distributionSha256Sum on download, so this is
  # the verification step, not just a warm-up.
  step "android: materialising Gradle $version (the wrapper verifies its checksum)"
  if run "$TAFFY_ROOT/gradlew" --quiet --version; then
    ran_ok "Gradle $version verified"
  else
    warn "./gradlew --version failed — see the output above"
    note "A checksum mismatch here means the distribution does not match the committed pin; do not work around it."
  fi
  return 0
}

# --- android sdk --------------------------------------------------------------

android_step_android() {
  step "android: Android SDK platform"
  local api sdk located source hint mgr package
  api=$(taffy_android_compile_sdk 2>/dev/null || printf '')
  if [ -z "$api" ]; then
    note "gradle/libs.versions.toml records no compileSdk — nothing to require"
    return 0
  fi
  note "gradle/libs.versions.toml requires SDK platform android-$api (the pinned Compose BOM refuses anything older)"

  if ! located=$(taffy_android_sdk_located); then
    if hint=$(taffy_android_sdk_conventional); then
      warn "an Android SDK exists at $hint but nothing points at it"
      note "export ANDROID_HOME=$hint   (the Android Gradle Plugin reads ANDROID_HOME, ANDROID_SDK_ROOT or local.properties)"
    else
      warn "no Android SDK found"
      note "Install the Android SDK command-line tools and platform-tools, export ANDROID_HOME, and re-run: ./tools/bootstrap --profile android --only android"
    fi
    return 0
  fi
  sdk=${located%%	*}
  source=${located#*	}
  ok "Android SDK $sdk (via $source)"

  if taffy_android_platform_dir "$sdk" "$api" >/dev/null; then
    ok "platform android-$api installed"
    return 0
  fi

  if ! mgr=$(taffy_android_sdkmanager "$sdk"); then
    warn "platform android-$api is missing and sdkmanager was not found"
    note "Install the SDK command-line tools, then: sdkmanager --install 'platforms;android-$api'"
    return 0
  fi

  # Android ships point releases (android-37.0, android-37.1). Ask sdkmanager
  # which ones exist rather than guessing a suffix.
  package=$("$mgr" --list 2>/dev/null \
    | awk -F'|' -v api="$api" '
        { name = $1; gsub(/[[:space:]]/, "", name)
          if (name ~ "^platforms;android-" api "(\\.[0-9]+)?$") print name }' \
    | sort -V 2>/dev/null | tail -n 1)
  if [ -z "$package" ]; then
    warn "sdkmanager lists no platform package for android-$api"
    note "Check network access and the SDK channel, then: sdkmanager --list | grep 'platforms;android-$api'"
    return 0
  fi

  if ! confirm "install $package with sdkmanager (it will ask you to accept its licence)?"; then
    warn "platform android-$api not installed — the kotlin lane of ./tools/check fast will skip"
    return 0
  fi

  # Not fatal. An SDK licence that has not been accepted, or an image without
  # network access to the SDK channel, is a reason for the kotlin lane to skip —
  # not a reason to fail every other environment this bootstrap just prepared.
  if ! run "$mgr" --install "$package"; then
    warn "sdkmanager could not install $package"
    note "Accept the licences interactively (sdkmanager --licenses) and re-run: ./tools/bootstrap --profile android --only android"
    return 0
  fi
  if [ "$DRY_RUN" = 1 ]; then
    return 0
  elif taffy_android_platform_dir "$sdk" "$api" >/dev/null; then
    ok "platform android-$api installed"
  else
    warn "sdkmanager reported success but the platform directory is still absent"
  fi
  return 0
}
