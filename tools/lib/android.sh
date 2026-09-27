#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/android.sh — locate the Android SDK the way the Android Gradle
# Plugin locates it, and answer what the Android UI build requires of it.
#
# Authority boundary: the *requirements* live in gradle/libs.versions.toml (the
# machine owner file for the Android rows of TOOLCHAIN.md). This module only
# reads them and looks at the host. It never installs anything — that is
# ./tools/bootstrap — and it never decides a version.
#
# Owning milestone: M0 (WP-M0-09), serving the Android UI of WP-M0-07.
#
# What it deliberately does not do: it does not fall back to a conventional
# install path when no SDK is configured. AGP reads ANDROID_HOME,
# ANDROID_SDK_ROOT and local.properties' sdk.dir and nothing else, so a lane
# that guessed a path would run a build the developer's own Gradle invocation
# cannot reproduce. It reports the conventional path as a *hint* instead.
#
# Sourced, never executed. Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_ANDROID_SH_LOADED:-}" ]; then return 0; fi
TAFFY_ANDROID_SH_LOADED=1

# shellcheck source-path=SCRIPTDIR source=versions.sh
. "$(dirname "${BASH_SOURCE[0]}")/versions.sh"

TAFFY_VERSION_CATALOG="$TAFFY_ROOT/gradle/libs.versions.toml"

# --- what the build requires ------------------------------------------------

# The API level Compose forces on the Android product UI. The catalog explains why: the
# pinned Compose BOM refuses to compile against anything older.
taffy_android_compile_sdk() {
  taffy_version_value toml "$TAFFY_VERSION_CATALOG" compileSdk
}

taffy_android_min_sdk() {
  taffy_version_value toml "$TAFFY_VERSION_CATALOG" minSdk
}

taffy_android_jvm_toolchain() {
  taffy_version_value toml "$TAFFY_VERSION_CATALOG" jvmToolchain
}

# --- what the host offers ---------------------------------------------------

# The SDK root AGP would use, and how it was found: "<path>\t<source>".
taffy_android_sdk_located() {
  local dir
  if [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME" ]; then
    printf '%s\tANDROID_HOME\n' "$ANDROID_HOME"; return 0
  fi
  if [ -n "${ANDROID_SDK_ROOT:-}" ] && [ -d "$ANDROID_SDK_ROOT" ]; then
    printf '%s\tANDROID_SDK_ROOT\n' "$ANDROID_SDK_ROOT"; return 0
  fi
  dir=$(taffy_kv_get "$TAFFY_ROOT/local.properties" 'sdk\.dir' 2>/dev/null || printf '')
  if [ -n "$dir" ] && [ -d "$dir" ]; then
    printf '%s\tlocal.properties\n' "$dir"; return 0
  fi
  return 1
}

taffy_android_sdk_root() {
  local located
  located=$(taffy_android_sdk_located) || return 1
  printf '%s\n' "${located%%	*}"
}

# An SDK that exists at the platform's usual location but is not configured.
# Reported so the remediation can be "export this", not "install Android Studio".
taffy_android_sdk_conventional() {
  local candidate
  case "$(taffy_os)" in
    macos) candidate="$HOME/Library/Android/sdk" ;;
    linux) candidate="$HOME/Android/Sdk" ;;
    *) return 1 ;;
  esac
  [ -d "$candidate" ] || return 1
  printf '%s\n' "$candidate"
}

# The installed platform directory for an API level. Android ships point
# releases (android-37.0, android-37.1); the highest one wins, which is what
# AGP compiles against for compileSdk 37.
taffy_android_platform_dir() { # <sdk-root> <api-level>
  local sdk=$1 api=$2 dir best=''
  [ -d "$sdk/platforms" ] || return 1
  for dir in "$sdk/platforms/android-$api" "$sdk/platforms/android-$api".*; do
    [ -d "$dir" ] || continue
    best=$dir
  done
  [ -n "$best" ] || return 1
  printf '%s\n' "$best"
}

taffy_android_sdkmanager() { # <sdk-root>
  local sdk=$1 candidate
  for candidate in \
    "$sdk/cmdline-tools/latest/bin/sdkmanager" \
    "$sdk/cmdline-tools/bin/sdkmanager" \
    "$sdk/tools/bin/sdkmanager"
  do
    [ -x "$candidate" ] && { printf '%s\n' "$candidate"; return 0; }
  done
  have sdkmanager && { command -v sdkmanager; return 0; }
  return 1
}
