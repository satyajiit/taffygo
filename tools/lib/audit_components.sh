#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/audit_components.sh — the component half of ./tools/doctor.
#
# Authority boundary: every requirement reported here is read from the machine
# owner file that owns it (gradle/libs.versions.toml, rust-toolchain.toml,
# package.json, tools/pins/*), never restated. This module compares the host
# against those files and reports which lanes of `./tools/check fast` would run.
#
# Owning milestone: M0 (WP-M0-09).
#
# What it deliberately does not do: it never installs, enables, or starts
# anything, and it reads no secret value — only whether a name is set.
#
# Sourced by ./tools/doctor, which supplies `record`, `version_of` and $PROBE.
# Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_AUDIT_COMPONENTS_SH_LOADED:-}" ]; then return 0; fi
TAFFY_AUDIT_COMPONENTS_SH_LOADED=1

# --- the Android host-loop toolchain ---------------------------------------

audit_jdk() {
  local pinned running major
  pinned=$(taffy_android_jvm_toolchain 2>/dev/null || printf '')
  if ! have java; then
    record toolchain "jdk" info "not installed" \
      "The Android Gradle build needs JDK ${pinned:-the version in TOOLCHAIN.md}."
  else
    # The JVM prints "Picked up JAVA_TOOL_OPTIONS: ..." to stderr before its
    # own banner, so an unfiltered `head -n 1` reports the environment variable
    # where the version belongs. Drop every "Picked up" line and keep the
    # version.
    running=$(java -version 2>&1 | grep -v '^Picked up ' | head -n 1)
    major=$(taffy_java_major || printf '')
    if [ -n "$pinned" ] && [ -n "$major" ] && [ "$major" != "$pinned" ]; then
      record toolchain "jdk" warn "$running" \
        "gradle/libs.versions.toml records JDK $pinned as the Gradle toolchain."
    else
      record toolchain "jdk" ok "$running${pinned:+ (toolchain $pinned)}"
    fi
  fi
  record toolchain "JAVA_HOME" info "${JAVA_HOME:-unset}"

  # A separate finding, because its consequence is not a version mismatch.
  # Chromium's build/android/gyp/compile_kt.py treats *any* stderr from the
  # compiler as a failure, and the JVM writes its "Picked up JAVA_TOOL_OPTIONS"
  # banner to stderr on every invocation. Every Kotlin and Java compile step in
  # the product root therefore fails with "Command failed because it wrote to
  # stderr" while the compiler itself reported nothing. ./tools/chromium/build
  # clears the variable for its own run; this line is for the reader who
  # invokes autoninja by hand.
  if [ -n "${JAVA_TOOL_OPTIONS:-}" ]; then
    record toolchain "JAVA_TOOL_OPTIONS" warn "${JAVA_TOOL_OPTIONS}" \
      "Set in this environment. It breaks every Chromium Java/Kotlin compile step: the JVM's own banner goes to stderr and compile_kt.py fails on any stderr. ./tools/chromium/build unsets it; a hand-run autoninja needs env -u JAVA_TOOL_OPTIONS."
  fi
  return 0
}

audit_gradle() {
  local props version dist
  props="$TAFFY_ROOT/gradle/wrapper/gradle-wrapper.properties"
  if [ ! -f "$props" ]; then
    record toolchain "gradle wrapper" info "not scaffolded" "Lands with taffy-core/ui/android."
    return 0
  fi
  version=$(taffy_version_value gradle-dist "$props" 2>/dev/null || printf '?')
  if ! grep -q '^distributionSha256Sum=' "$props"; then
    record toolchain "gradle wrapper" fail "$version, no distributionSha256Sum" \
      "The wrapper must verify the distribution it downloads."
  else
    dist="${GRADLE_USER_HOME:-$HOME/.gradle}/wrapper/dists/gradle-${version}-bin"
    if [ -d "$dist" ]; then
      record toolchain "gradle wrapper" ok "$version (distribution materialised, checksum pinned)"
    else
      record toolchain "gradle wrapper" info "$version (distribution not downloaded yet)" \
        "The first ./gradlew invocation fetches and checksum-verifies it."
    fi
  fi
  return 0
}

audit_android_sdk() {
  local located sdk source api platform ndks
  api=$(taffy_android_compile_sdk 2>/dev/null || printf '')
  if ! located=$(taffy_android_sdk_located); then
    local hint
    if hint=$(taffy_android_sdk_conventional); then
      record toolchain "android sdk" warn "not configured (found $hint)" \
        "The Android Gradle Plugin reads ANDROID_HOME, ANDROID_SDK_ROOT or local.properties: export ANDROID_HOME=$hint"
    else
      record toolchain "android sdk" info "not installed" \
        "Android host-loop environment only; ./tools/bootstrap --profile android --only android says what to install."
    fi
    return 0
  fi
  sdk=${located%%	*}
  source=${located#*	}
  record toolchain "android sdk" ok "$sdk (via $source)"

  if [ -n "$api" ]; then
    if platform=$(taffy_android_platform_dir "$sdk" "$api"); then
      record toolchain "sdk platform" ok "$(basename "$platform") (compileSdk $api)"
    else
      record toolchain "sdk platform" warn "android-$api is not installed" \
        "./tools/bootstrap --profile android --only android installs it."
    fi
  fi

  [ -d "$sdk/platform-tools" ] || record toolchain "platform-tools" warn "missing" \
    "sdkmanager --install 'platform-tools'"
  if [ -d "$sdk/ndk" ]; then
    ndks=$(find "$sdk/ndk" -maxdepth 1 -mindepth 1 -exec basename {} \; 2>/dev/null | sort | tr '\n' ' ' | sed 's/ $//')
    record toolchain "android ndk" info "$ndks" "Gradle does not build native code; Chromium owns its own pinned NDK."
  else
    record toolchain "android ndk" info "none installed"
  fi
  return 0
}

audit_rust() {
  local pinned running
  pinned=$(taffy_version_value toml "$TAFFY_ROOT/rust-toolchain.toml" channel 2>/dev/null || printf '')
  if ! have rustc; then
    record toolchain "rust" info "not installed" \
      "taffy-core's Rust workspace needs rustup; rust-toolchain.toml then selects ${pinned:-the pinned channel}."
    return 0
  fi
  running=$(version_of rustc --version)
  case "$running" in
    *"$pinned"*) record toolchain "rust" ok "$running (rust-toolchain.toml: $pinned)" ;;
    *) record toolchain "rust" warn "$running" \
         "rust-toolchain.toml pins $pinned; rustup installs it on the next cargo invocation." ;;
  esac
  if have rustup && [ "$PROBE" = 1 ]; then
    local targets
    targets=$(rustup target list --installed 2>/dev/null | tr '\n' ' ' | sed 's/ $//')
    record toolchain "rust targets" info "${targets:-none}" \
      "The Cargo loop is host-only; Chromium supplies its own Android Rust toolchain."
  fi
  return 0
}

audit_node() {
  local range pin running verdict
  range=$(taffy_node_engine_range 2>/dev/null || printf '')
  pin=$(taffy_node_pnpm_pin 2>/dev/null || printf '')

  if have node; then
    running=$(version_of node --version)
    verdict=$(taffy_node_engine_verdict "$running")
    case "$verdict" in
      ok) record toolchain "node" ok "$running (engines: $range)" ;;
      too-old|too-new) record toolchain "node" warn "$running" \
        "package.json#engines requires $range." ;;
      *) record toolchain "node" info "$running${range:+ (engines: $range)}" ;;
    esac
  else
    record toolchain "node" info "not installed" "Needed by services/ and website/${range:+ (engines: $range)}."
  fi

  if have pnpm; then
    running=$(version_of pnpm --version)
    if [ -n "$pin" ] && [ "$running" != "$pin" ]; then
      record toolchain "pnpm" warn "$running" \
        "package.json#packageManager pins $pin; \`corepack enable pnpm\` makes that authoritative."
    else
      record toolchain "pnpm" ok "$running${pin:+ (pinned)}"
    fi
  else
    record toolchain "pnpm" info "not installed" "Run: corepack enable pnpm"
  fi
  return 0
}

audit_python() {
  if have python3; then
    record toolchain "python3" ok "$(version_of python3 --version)" \
      "Host tooling only: the docs lane, the BIP generator and the fixture check. Decision 0007 governs Python in the product."
  else
    record toolchain "python3" fail "not installed" \
      "./tools/check docs, the BIP generator and the fixture corpus check all need it."
  fi
  return 0
}

audit_android_toolchain() {
  audit_jdk
  audit_gradle
  audit_android_sdk
  audit_rust
  audit_node
  audit_python
  return 0
}

# --- local services ---------------------------------------------------------

# --- devices ----------------------------------------------------------------

audit_devices() {
  if ! have adb; then
    record devices "adb" info "not installed" "Part of the Android SDK platform-tools."
    return 0
  fi
  record devices "adb" info "$(version_of adb --version || printf 'present')"
  if [ "$PROBE" = 1 ]; then
    if pgrep -f 'adb .*server' >/dev/null 2>&1 || pgrep -x adb >/dev/null 2>&1; then
      local list
      list=$(adb devices | awk 'NR>1 && NF { print $1 }' | tr '\n' ' ' | sed 's/ $//')
      record devices "attached" info "${list:-none}"
    else
      record devices "attached" info "adb server not running (doctor does not start it)"
    fi
  fi
  if have emulator && [ "$PROBE" = 1 ]; then
    local avds
    avds=$(emulator -list-avds 2>/dev/null | tr '\n' ' ' | sed 's/ $//')
    record devices "avds" info "${avds:-none}"
  fi
  return 0
}

# --- secrets (names only) ---------------------------------------------------

audit_secrets() {
  # There are no environment-variable names left to report. Every one this
  # ever named -- the two Cloudflare tokens and the Supabase access token --
  # belonged to a host TaffyGo ran, and decision 0200 removed all of them.
  #
  # What is left is the untracked workspace directory, and it matters more
  # than the names did: `.taffy/android-signing.env` is where the release
  # signing identity is resolved from (decision 0203) and mode 600 is that
  # decision's own requirement, and `.taffy/play-service-account.json` is a
  # Play credential. This used to check exactly one file, `env.sh`, which was
  # the account-plane one -- so it was about to become a check of a file
  # nothing writes while the two credential files beside it went unchecked.
  #
  # Contents are never read; only whether another account on this host could.
  # `find -perm` rather than `stat`, whose flags differ between GNU and BSD.
  local file name
  for file in "$TAFFY_ROOT"/.taffy/*; do
    [ -f "$file" ] || continue
    name=".taffy/${file##*/}"
    if [ -n "$(find "$file" -prune \( -perm -040 -o -perm -004 \) -print 2>/dev/null)" ]; then
      record secrets "$name" warn "readable by other accounts on this host" \
        "It can hold credentials. Run: chmod 600 $name"
    else
      record secrets "$name" ok "owner-only (contents never read by doctor)"
    fi
  done
  return 0
}

# --- what ./tools/check fast would do here ----------------------------------

audit_lanes() {
  local lane blocker reason
  for lane in $TAFFY_COMPONENT_LANES; do
    if blocker=$(lane_ready "$lane"); then
      record lanes "$lane" ok "would run"
    else
      reason=${blocker%%	*}
      record lanes "$lane" info "would skip: $reason" "${blocker#*	}"
    fi
  done
  record lanes "command" info "./tools/check fast" \
    "A skipped lane is not a failure; it is work this host cannot verify."
  return 0
}
