#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Shared helpers for the ./tools commands.
#
# Sourced, never executed. Targets bash 3.2 so it works on a stock macOS
# docs-only host as well as the Linux Chromium builders: no associative
# arrays, no `mapfile`, no `${var,,}`.
#
# shellcheck shell=bash

if [ -n "${TAFFY_COMMON_SH_LOADED:-}" ]; then return 0; fi
TAFFY_COMMON_SH_LOADED=1

# --- repository -------------------------------------------------------------

# Walk up from a starting directory until the repository marker files appear.
#
# The markers are the version index and the product root, both of which every
# tree this suite runs in carries. `docs/` used to be the second marker and is
# not one any more: the planning documents are this repository's own and a tree
# published without them is still a tree these commands have to work in.
taffy_repo_root() {
  local dir
  dir=$(cd "${1:-$PWD}" 2>/dev/null && pwd -P) || return 1
  while [ "$dir" != "/" ]; do
    if [ -f "$dir/TOOLCHAIN.md" ] && [ -d "$dir/taffy-core" ]; then
      printf '%s\n' "$dir"
      return 0
    fi
    dir=$(dirname "$dir")
  done
  return 1
}

# `|| true` is load-bearing. Every command here runs under `set -e`, and an
# assignment whose command substitution fails ends the process on that line —
# before the diagnostic below, which is the one thing that would say why. A
# tree with no marker exited 1 with no output at all until this was written.
TAFFY_ROOT=${TAFFY_ROOT:-$(taffy_repo_root "$(dirname "${BASH_SOURCE[0]}")" || true)}
if [ -z "$TAFFY_ROOT" ]; then
  printf 'taffy: cannot locate the repository root (no TOOLCHAIN.md beside taffy-core/ above %s)\n' "$PWD" >&2
  exit 2
fi
export TAFFY_ROOT

# --- output -----------------------------------------------------------------

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-dumb}" != "dumb" ]; then
  C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
  C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_BLUE=$'\033[34m'
else
  C_RESET=''; C_BOLD=''; C_DIM=''; C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''
fi

say()     { printf '%s\n' "$*"; }
info()    { printf '%s\n' "$*" >&2; }
step()    { printf '%s==>%s %s\n' "$C_BLUE$C_BOLD" "$C_RESET" "$*" >&2; }
ok()      { printf '%s  ok%s   %s\n' "$C_GREEN" "$C_RESET" "$*" >&2; }
warn()    { printf '%s  warn%s %s\n' "$C_YELLOW" "$C_RESET" "$*" >&2; }
bad()     { printf '%s  fail%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; }
note()    { printf '%s       %s%s\n' "$C_DIM" "$*" "$C_RESET" >&2; }

# die <message> [remediation...] — every failure names what to do next.
die() {
  local msg=$1; shift
  printf '%serror:%s %s\n' "$C_RED$C_BOLD" "$C_RESET" "$msg" >&2
  local line
  for line in "$@"; do printf '       %s\n' "$line" >&2; done
  exit 1
}

have() { command -v "$1" >/dev/null 2>&1; }

require_cmd() {
  have "$1" || die "required command not found: $1" "${2:-Install $1 and re-run.}"
}

# --- host -------------------------------------------------------------------

taffy_os() {
  case "$(uname -s)" in
    Linux)  printf 'linux\n' ;;
    Darwin) printf 'macos\n' ;;
    *)      printf 'other\n' ;;
  esac
}

taffy_arch() {
  case "$(uname -m)" in
    x86_64|amd64) printf 'x86-64\n' ;;
    arm64|aarch64) printf 'arm64\n' ;;
    *) uname -m ;;
  esac
}

# Chromium builds are x86-64 Linux only (docs/development/development-setup.md §2).
# WSL2 qualifies: it is a real Linux kernel on an ext4 filesystem. The trap it
# adds — working on a Windows drive mount — is rejected separately below.
taffy_is_chromium_host() {
  [ "$(taffy_os)" = "linux" ] && [ "$(taffy_arch)" = "x86-64" ]
}

taffy_is_wsl() {
  [ "$(taffy_os)" = "linux" ] && grep -qi microsoft /proc/version 2>/dev/null
}

# /mnt/<drive> paths cross the 9P bridge to Windows: case-insensitive and far
# too slow for a Chromium checkout. The workspace must live in the ext4 VHD.
taffy_reject_windows_mount() {
  case "$1" in
    /mnt/[a-zA-Z]|/mnt/[a-zA-Z]/*) die \
      "the workspace path $1 is on a Windows drive mount" \
      "Under WSL2 the checkout must live in the Linux filesystem (for example" \
      "\$HOME/chromium-taffy), never /mnt/c — the 9P bridge is case-insensitive" \
      "and roughly an order of magnitude slower, which breaks the build." ;;
  esac
}

taffy_require_chromium_host() {
  taffy_is_chromium_host || die \
    "the Chromium environment requires an x86-64 Linux host (found $(taffy_os)/$(taffy_arch))" \
    "Chromium's Android build does not support macOS or Windows, and a container" \
    "on those hosts is not a supported substitute." \
    "Run the Chromium steps on an x86-64 Linux machine, a remote one included; this host" \
    "can still run ./tools/bootstrap --profile android and ./tools/check fast."
}

# --- key=value files --------------------------------------------------------

# taffy_kv_get <file> <key> — reads `key=value`, ignoring `#` comments.
taffy_kv_get() {
  local file=$1 key=$2
  [ -f "$file" ] || return 1
  sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*//p" "$file" \
    | sed 's/[[:space:]]*$//' | head -n 1
}

# taffy_kv_set <file> <key> <value> — rewrites in place, preserving comments
# *and the file's permissions*. The second half is not decoration: this writes
# through a temporary file and `mv`s it over the target, so without the seeding
# step below every write silently replaced the file's mode with whatever the
# umask gave the temporary — which is how the machine-local workspace state
# ended up group- and world-readable inside a directory that also holds the
# release signing environment. `cp -p` creates the temporary carrying the
# target's mode, and a `>` redirection into an existing file truncates it
# without changing that mode. `chmod --reference` would be shorter and is
# GNU-only, like `stat`'s flags.
taffy_kv_set() {
  local file=$1 key=$2 value=$3 tmp
  tmp="${file}.tmp.$$"
  [ -f "$file" ] && cp -p "$file" "$tmp"
  if [ -f "$file" ] && grep -q "^[[:space:]]*${key}[[:space:]]*=" "$file"; then
    sed "s|^[[:space:]]*${key}[[:space:]]*=.*|${key}=${value}|" "$file" > "$tmp"
  else
    { [ -f "$file" ] && cat "$file"; printf '%s=%s\n' "$key" "$value"; } > "$tmp"
  fi
  mv "$tmp" "$file"
}

# --- workspace state --------------------------------------------------------
# Machine-local, gitignored. Written only by bootstrap and chromium/sync.

taffy_state_file() { printf '%s/.taffy/workspace.env\n' "$TAFFY_ROOT"; }

taffy_state_get() {
  local key=$1 default=${2:-} value
  value=$(taffy_kv_get "$(taffy_state_file)" "$key" 2>/dev/null) || value=''
  [ -n "$value" ] && printf '%s\n' "$value" || printf '%s\n' "$default"
}

taffy_state_set() {
  mkdir -p "$TAFFY_ROOT/.taffy"
  local f; f=$(taffy_state_file)
  if [ ! -f "$f" ]; then
    printf '%s\n' \
      '# Machine-local TaffyGo workspace state. Not committed; safe to delete.' \
      '# Rewritten by ./tools/bootstrap and ./tools/chromium/sync.' > "$f"
  fi
  # Owner-only, like everything else under .taffy/. This file holds paths and
  # profile names today and no credential, but it is the one file in a
  # credential directory that was being created at the umask default -- and
  # `./tools/doctor` is right to warn about any of them, so the fix belongs
  # here rather than as an exemption in the check. Set before the write, which
  # now preserves it, and applied every time so a file created under the old
  # behaviour is repaired rather than grandfathered.
  chmod 600 "$f" 2>/dev/null || true
  taffy_kv_set "$f" "$1" "$2"
}

# --- pins -------------------------------------------------------------------

# The value every unpinned row carries until its milestone owner sets it.
TAFFY_UNPINNED='TO-VERIFY'

taffy_pin() { taffy_kv_get "$TAFFY_ROOT/tools/pins/$1" "$2"; }

taffy_chromium_pin() { taffy_kv_get "$TAFFY_ROOT/chromium/REVISION" "$1"; }

taffy_chromium_is_pinned() {
  local c; c=$(taffy_chromium_pin commit)
  [ -n "$c" ] && [ "$c" != "$TAFFY_UNPINNED" ]
}

# --- misc -------------------------------------------------------------------

taffy_json_escape() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/	/\\t/g'
}

# Free space in GB on the volume holding a path (portable-ish df parsing).
taffy_free_gb() {
  local path=$1
  [ -d "$path" ] || path=$(dirname "$path")
  df -Pk "$path" 2>/dev/null | awk 'NR==2 { printf "%d", $4/1024/1024 }'
}

taffy_total_ram_gb() {
  case "$(taffy_os)" in
    linux) awk '/MemTotal/ { printf "%d", $2/1024/1024 }' /proc/meminfo 2>/dev/null ;;
    macos) sysctl -n hw.memsize 2>/dev/null | awk '{ printf "%d", $1/1024/1024/1024 }' ;;
    *) printf '0' ;;
  esac
}

# The JDK major version on PATH, normalised across the 1.8 and 9+ schemes.
# The only JDK requirement in this repository is the Gradle toolchain recorded
# in gradle/libs.versions.toml; Chromium brings its own.
taffy_java_major() {
  local v
  have java || return 1
  v=$(java -version 2>&1 | sed -n 's/.*version "\([0-9._]*\).*/\1/p' | head -n 1)
  [ -n "$v" ] || return 1
  case "$v" in
    1.*) printf '%s\n' "$v" | cut -d. -f2 ;;
    *)   printf '%s\n' "$v" | cut -d. -f1 ;;
  esac
}

taffy_cpu_count() {
  case "$(taffy_os)" in
    linux) getconf _NPROCESSORS_ONLN 2>/dev/null || printf '0' ;;
    macos) sysctl -n hw.physicalcpu 2>/dev/null || printf '0' ;;
    *) printf '0' ;;
  esac
}
