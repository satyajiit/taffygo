#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/lib/audit_host.sh — the host and Chromium-workspace half of ./tools/doctor.
#
# Authority boundary: hardware guidance is decision 0013, the Chromium host rule
# is docs/development/development-setup.md section 2, and the pins are
# TOOLCHAIN.md. This module reports what the host looks like against those; it
# decides none of them.
#
# Owning milestone: M0 (WP-M0-09).
#
# Read-only by construction: no network, no install, no file written. A doctor
# that fixed things would be a bootstrap, and a bootstrap that reported would be
# a doctor.
#
# Sourced by ./tools/doctor, which supplies `record`, `version_of` and $PROBE.
# Targets bash 3.2.
#
# shellcheck shell=bash

if [ -n "${TAFFY_AUDIT_HOST_SH_LOADED:-}" ]; then return 0; fi
TAFFY_AUDIT_HOST_SH_LOADED=1

audit_host() {
  local os arch ram cpus free
  os=$(taffy_os); arch=$(taffy_arch)
  record host "operating system" info "$os/$arch"

  cpus=$(taffy_cpu_count)
  if [ "$os" = linux ] && [ "$cpus" -gt 0 ] && [ "$cpus" -lt 8 ]; then
    record host "cpu cores" warn "$cpus" "Chromium guidance is 8+ physical cores (decision 0013)."
  else
    record host "cpu cores" info "$cpus"
  fi

  ram=$(taffy_total_ram_gb)
  if [ "$os" = linux ] && [ "$ram" -gt 0 ] && [ "$ram" -lt 32 ]; then
    record host "memory" warn "${ram} GB" "Chromium link steps need 32 GB minimum, 64 GB recommended."
  else
    record host "memory" info "${ram} GB"
  fi

  free=$(taffy_free_gb "$TAFFY_ROOT")
  record host "free space (repo volume)" info "${free:-?} GB"

  if taffy_is_chromium_host; then
    if taffy_is_wsl; then
      record host "chromium build host" ok "x86-64 linux (WSL2)" \
        "Workable for development. Keep the workspace in the ext4 filesystem (never /mnt/c); milestone evidence of record comes from a native Linux builder."
    else
      record host "chromium build host" ok "supported (x86-64 linux)"
    fi
  else
    record host "chromium build host" info "not this host" \
      "Chromium builds on x86-64 Linux only; run the Chromium steps on such a machine, a remote one included."
  fi
  return 0
}

audit_repo() {
  if have git && git -C "$TAFFY_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    local rev branch dirty
    rev=$(git -C "$TAFFY_ROOT" rev-parse --short HEAD 2>/dev/null || printf 'none')
    branch=$(git -C "$TAFFY_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || printf '?')
    dirty=$(git -C "$TAFFY_ROOT" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
    record repo "git" info "$(version_of git --version || printf 'present')"
    record repo "revision" info "$branch @ $rev ($dirty changed files)"
  else
    record repo "git" fail "missing" "Install Git; the pin and patch tooling needs it."
  fi

  local root_free
  root_free=$(taffy_free_gb "$TAFFY_ROOT")
  [ -n "$root_free" ] && [ "$root_free" -lt 5 ] 2>/dev/null && \
    record repo "free space" warn "${root_free} GB" "Low free space on the repository volume."

  record repo "workspace state" info \
    "$([ -f "$(taffy_state_file)" ] && printf '%s' "$(taffy_state_file)" || printf 'not written yet')"
  return 0
}

# Every TOOLCHAIN.md row that has a machine owner file, read through the same
# register ./tools/check fast uses, so the two can never disagree.
audit_pins() {
  local f value label owner reader key path pinned=0 unpinned=0 missing=0

  for f in depot_tools build-cache; do
    [ -f "$TAFFY_ROOT/tools/pins/$f" ] && continue
    record pins "tools/pins/$f" fail "missing" "TOOLCHAIN.md names it as a machine owner file."
  done

  printf '%s\n' "$TAFFY_PIN_REGISTER" > "$TAFFY_DOCTOR_TMP/register.txt"
  while IFS='|' read -r label owner reader key; do
    [ -n "$label" ] || continue
    path="$TAFFY_ROOT/$owner"
    if [ ! -f "$path" ]; then
      missing=$((missing + 1))
      record pins "$label" fail "owner file missing: $owner" "TOOLCHAIN.md names it; create it or correct the row."
      continue
    fi
    value=$(taffy_version_value "$reader" "$path" "$key" 2>/dev/null) || value=''
    if [ -z "$value" ]; then
      missing=$((missing + 1))
      record pins "$label" fail "no value in $owner" "The owner file records nothing for \"$key\"."
    elif [ "$value" = "$TAFFY_UNPINNED" ]; then
      unpinned=$((unpinned + 1))
    else
      pinned=$((pinned + 1))
    fi
  done < "$TAFFY_DOCTOR_TMP/register.txt"

  record pins "machine-owned rows" info "$pinned pinned, $unpinned $TAFFY_UNPINNED, $missing unreadable" \
    "./tools/check fast is the gate; this is the summary."

  local milestone commit
  milestone=$(taffy_chromium_pin milestone || printf '')
  commit=$(taffy_chromium_pin commit || printf '')
  if taffy_chromium_is_pinned; then
    record pins "chromium revision" ok "milestone ${milestone:-?} @ ${commit}"
  else
    record pins "chromium revision" warn "$TAFFY_UNPINNED" \
      "SP-01 selects the stable milestone and writes chromium/REVISION."
  fi
  record pins "chromium security patch level" info \
    "$(taffy_kv_get "$TAFFY_ROOT/chromium/SECURITY_PATCH_LEVEL" level || printf '?')"
  return 0
}

audit_chromium() {
  local ws src
  ws=$(taffy_state_get chromium_workspace "")
  if [ -z "$ws" ]; then
    record chromium "workspace" info "not created" \
      "./tools/bootstrap --profile chromium --workspace <path>"
    return 0
  fi
  record chromium "workspace" info "$ws"
  record chromium "free space (checkout volume)" info "$(taffy_free_gb "$ws") GB"

  src="$ws/src"
  if [ ! -d "$src" ]; then
    record chromium "checkout" info "not synced" "./tools/chromium/sync"
    return 0
  fi

  local head pinned
  head=$(git -C "$src" rev-parse HEAD 2>/dev/null || printf '')
  pinned=$(taffy_chromium_pin commit || printf '')
  if [ -z "$head" ]; then
    record chromium "checkout" fail "present but not a git checkout" "Re-run ./tools/chromium/sync."
  elif [ "$pinned" = "$TAFFY_UNPINNED" ] || [ -z "$pinned" ]; then
    record chromium "checkout revision" warn "${head:0:12} (repository pin unset)" ""
  elif [ "$head" = "$pinned" ]; then
    record chromium "checkout revision" ok "matches chromium/REVISION"
  elif git -C "$src" merge-base --is-ancestor "$pinned" HEAD 2>/dev/null; then
    # The documented normal state is the taffy/patched branch with the queue
    # applied, so HEAD is never the pin itself once a single patch exists. An
    # equality test reports that correct checkout as drifted and then advises a
    # re-sync, which would discard the patch commits. Ancestry is the question
    # that was meant: is this checkout the pin plus TaffyGo's own commits?
    local ahead
    ahead=$(git -C "$src" rev-list --count "$pinned"..HEAD 2>/dev/null || printf '?')
    record chromium "checkout revision" ok \
      "at chromium/REVISION plus $ahead patch commit(s)"
  else
    record chromium "checkout revision" warn \
      "${head:0:12} does not descend from pinned ${pinned:0:12}" \
      "./tools/chromium/sync brings the checkout back to the pin. Export any patch commits first (./tools/chromium/export-patches) — a re-sync from an unrelated revision discards them."
  fi

  # Product-root mount health. Hard cutover means both halves matter: //taffy
  # points at this repository and the retired //taffy path is gone.
  local mount target expected legacy_mount
  mount="$src/taffy"
  expected="$TAFFY_ROOT/taffy-core"
  if [ -L "$mount" ]; then
    target=$(readlink "$mount")
    if [ "$target" = "$expected" ]; then
      record chromium "product root mount" ok "taffy -> taffy-core"
    else
      record chromium "product root mount" fail "points at $target" \
        "Expected $expected. Re-run ./tools/chromium/sync."
    fi
  elif [ -e "$mount" ]; then
    record chromium "product root mount" fail "exists but is not a symlink" \
      "Move it aside and re-run ./tools/chromium/sync; taffy-core must never be copied."
  else
    record chromium "product root mount" info "not mounted" "./tools/chromium/sync"
  fi

  legacy_mount="$src/components/taffy"
  if [ -L "$legacy_mount" ] || [ -e "$legacy_mount" ]; then
    record chromium "retired component mount" fail "components/taffy still exists" \
      "Run ./tools/chromium/sync. The hard cutover permits only //taffy."
  else
    record chromium "retired component mount" ok "absent"
  fi

  local branch
  branch=$(git -C "$src" rev-parse --abbrev-ref HEAD 2>/dev/null || printf '?')
  record chromium "patch branch" info "$branch"

  local outdir
  for outdir in dev-x64 dev-arm64 release-arm64; do
    [ -d "$src/out/$outdir" ] && record chromium "out/$outdir" info "present"
  done

  if have depot_tools_bootstrap || have gclient; then
    record chromium "depot_tools" ok "on PATH"
  elif [ -d "$ws/depot_tools" ]; then
    record chromium "depot_tools" warn "installed but not on PATH" \
      "export PATH=\"\$PATH:$ws/depot_tools\""
  else
    record chromium "depot_tools" info "not installed" "./tools/bootstrap --profile chromium"
  fi
  return 0
}

audit_caches() {
  if have ccache; then
    local hit=''
    if [ "$PROBE" = 1 ]; then
      hit=$(ccache -s 2>/dev/null | awk -F: '/[Cc]ache hit rate/ { gsub(/^[ \t]+/,"",$2); print $2; exit }')
    fi
    record caches "ccache" ok "$(version_of ccache --version || printf 'present')${hit:+ · hit rate ${hit}}"
  else
    record caches "ccache" info "not installed" "./tools/bootstrap --profile chromium installs it."
  fi

  if have sccache; then
    local stats=''
    # `sccache --show-stats` starts the server; only read stats if it is already up.
    if [ "$PROBE" = 1 ] && pgrep -x sccache >/dev/null 2>&1; then
      stats=$(sccache --show-stats 2>/dev/null | awk -F' {2,}' '/Cache hits/ { print $2; exit }')
    fi
    record caches "sccache" ok "$(version_of sccache --version || printf 'present')${stats:+ · hits ${stats}}"
  else
    record caches "sccache" info "not installed" "Rust RUSTC_WRAPPER (decision 0013)."
  fi
  return 0
}
