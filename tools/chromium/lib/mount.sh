#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Mounting the Taffy product root and its owned fixture authorities into a
# Chromium checkout.
#
# Split out of chromium.sh as one responsibility: what //taffy is made of on
# disk, and nothing about configuring, building or patching it.

# chromium_mount_taffy_core <chromium src>
#
# Hard cutover: //taffy is the only TaffyGo namespace in the checkout. The old
# //components/taffy mount is removed only when it is the exact generated link
# this repository used to own. An unexpected path is refused, never deleted.
chromium_mount_taffy_core() {
  local src=$1
  local core="$TAFFY_ROOT/taffy-core"
  local validator="$core/build/component_graph.py"
  local gn_validator="$core/build/component_gn_graph.py"
  local mount="$src/taffy"
  local legacy_mount="$src/components/taffy"
  local legacy_expected="$TAFFY_ROOT/chromium/overlay/components/taffy"
  local target

  [ -d "$core" ] || die "missing product root: ${core#"$TAFFY_ROOT"/}"
  [ -f "$core/BUILD.gn" ] || die "taffy-core/BUILD.gn is missing"
  [ -f "$core/build/components.toml" ] || die "taffy-core/build/components.toml is missing"
  [ -f "$validator" ] || die "taffy-core/build/component_graph.py is missing"
  [ -f "$gn_validator" ] || die "taffy-core/build/component_gn_graph.py is missing"
  [ -d "$src/components" ] || die \
    "$src does not look like a Chromium checkout (no components/ directory)" \
    "Run ./tools/chromium/sync before mounting //taffy."
  have python3 || die "python3 is required to validate taffy-core/build/components.toml"
  python3 "$validator" --check || die \
    "the //taffy component manifest is invalid" \
    "Run: python3 taffy-core/build/component_graph.py --check"
  python3 "$gn_validator" || die \
    "the //taffy GN targets differ from the component manifest" \
    "Run: python3 taffy-core/build/component_gn_graph.py"

  if [ -L "$legacy_mount" ]; then
    target=$(readlink "$legacy_mount")
    [ "$target" = "$legacy_expected" ] || die \
      "$legacy_mount points at unexpected path $target" \
      "Refusing to remove a symlink this repository does not own."
    rm -f "$legacy_mount"
    ok "removed retired //components/taffy mount"
  elif [ -e "$legacy_mount" ]; then
    die "$legacy_mount exists and is not the retired generated symlink" \
      "Hard cutover requires that //components/taffy does not exist. Move the" \
      "unexpected tree aside, then re-run ./tools/chromium/sync."
  fi

  if [ -L "$mount" ]; then
    target=$(readlink "$mount")
    [ "$target" = "$core" ] || die \
      "$mount points at unexpected path $target" \
      "Expected $core. Refusing to replace a symlink this repository does not own."
  elif [ -e "$mount" ]; then
    die "$mount exists and is not a symlink" \
      "//taffy is mounted, never copied — a copy silently forks the code." \
      "Move it aside and re-run ./tools/chromium/sync."
  else
    ln -s "$core" "$mount"
  fi
  ok "mounted //taffy -> taffy-core/"
}

# chromium_mount_owned_fixture <link> <target> [retired target]
#
# Test corpora remain top-level repository authorities, so Chromium needs an
# exact link below //taffy/test/data. Replace only the one retired target this
# repository generated before the hard cutover; any other path is user-owned
# state and is refused.
chromium_mount_owned_fixture() {
  local link=$1 target=$2 retired_target=${3:-} actual

  [ -e "$target" ] || die "fixture authority is missing: $target"
  mkdir -p "$(dirname "$link")"
  if [ -L "$link" ]; then
    actual=$(readlink "$link")
    if [ -n "$retired_target" ] && [ "$actual" = "$retired_target" ]; then
      rm -f "$link"
    elif [ "$actual" != "$target" ]; then
      die "$link points at unexpected path $actual" \
        "Expected $target. Refusing to replace a symlink this repository does not own."
    fi
  elif [ -e "$link" ]; then
    die "$link exists and is not the generated fixture symlink" \
      "Move the unexpected path aside, then re-run ./tools/chromium/sync."
  fi

  [ -L "$link" ] || ln -s "$target" "$link"
}

# chromium_mount_taffy_fixtures
#
# The mounted product root is one source tree. These four links do not retain
# old product code; they expose the independently versioned fixture authorities
# that intentionally remain at repository top level.
chromium_mount_taffy_fixtures() {
  local data="$TAFFY_ROOT/taffy-core/test/data"
  local retired="$TAFFY_ROOT/contracts/bip"

  chromium_mount_owned_fixture \
    "$data/web" \
    "$TAFFY_ROOT/test-fixtures/web"
  chromium_mount_owned_fixture \
    "$data/tasks" \
    "$TAFFY_ROOT/test-fixtures/tasks"
  chromium_mount_owned_fixture \
    "$data/contract/golden" \
    "$TAFFY_ROOT/taffy-core/contracts/bip/golden" \
    "$retired/golden"
  chromium_mount_owned_fixture \
    "$data/contract/compat" \
    "$TAFFY_ROOT/taffy-core/contracts/bip/compat" \
    "$retired/compat"
  ok "mounted //taffy test fixture authorities"
}
