#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Resolving the Android signing identity for a Chromium build.
#
# Split out of chromium.sh because it is one responsibility with one rule and
# no dependency on the checkout helpers around it: where the key is, and the
# refusal to keep it here.
#
# The signing identity is not in this repository, and that is a rule rather
# than an omission (decision 0203). It is resolved here and appended to a
# configured build's args.gn by ./tools/chromium/build, so the key reaches the
# packaging step exactly as it always did while living where a public export
# cannot carry it. Two sources, in this order:
#
#   1. TAFFY_ANDROID_KEYSTORE, TAFFY_ANDROID_KEY_ALIAS and
#      TAFFY_ANDROID_KEYSTORE_PASSWORD, all three or none;
#   2. an untracked env file, $TAFFY_ROOT/.taffy/android-signing.env by
#      default and $TAFFY_ANDROID_SIGNING_ENV when set, holding the same three
#      names as KEY=VALUE lines.
#
# Resolving nothing is a working configuration: the build signs with
# Chromium's own debug key, which is right for a build a person installs by
# hand and wrong for one a store distributes — so the caller refuses that
# combination rather than this helper.
TAFFY_SIGNING_KEYSTORE=''
TAFFY_SIGNING_ALIAS=''
TAFFY_SIGNING_PASSWORD=''
TAFFY_SIGNING_SOURCE=''

taffy_signing_env_file() {
  if [ -n "${TAFFY_ANDROID_SIGNING_ENV:-}" ]; then
    printf '%s\n' "$TAFFY_ANDROID_SIGNING_ENV"
  else
    printf '%s/.taffy/android-signing.env\n' "$TAFFY_ROOT"
  fi
}

# Reads one KEY=VALUE line out of a file without sourcing it. A file holding a
# password is data; `source` would run whatever else it held, and a build
# command is the wrong place to discover that.
taffy_signing_env_value() {
  local file=$1 key=$2 line value
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      "$key"=*) value=${line#*=} ;;
      *) continue ;;
    esac
    value=${value%\"}; value=${value#\"}
    value=${value%\'}; value=${value#\'}
    printf '%s\n' "$value"
    return 0
  done < "$file"
  return 1
}

# 0 with TAFFY_SIGNING_* set, 1 when no identity is configured anywhere. Dies
# on a half-written one, because that is a mistake and not a choice.
taffy_signing_resolve() {
  TAFFY_SIGNING_KEYSTORE=''
  TAFFY_SIGNING_ALIAS=''
  TAFFY_SIGNING_PASSWORD=''
  TAFFY_SIGNING_SOURCE=''
  local file
  if [ -n "${TAFFY_ANDROID_KEYSTORE:-}${TAFFY_ANDROID_KEY_ALIAS:-}${TAFFY_ANDROID_KEYSTORE_PASSWORD:-}" ]; then
    if [ -z "${TAFFY_ANDROID_KEYSTORE:-}" ] || [ -z "${TAFFY_ANDROID_KEY_ALIAS:-}" ] ||
       [ -z "${TAFFY_ANDROID_KEYSTORE_PASSWORD:-}" ]; then
      die "the signing identity is half-set in the environment" \
        "All three or none: TAFFY_ANDROID_KEYSTORE, TAFFY_ANDROID_KEY_ALIAS, TAFFY_ANDROID_KEYSTORE_PASSWORD"
    fi
    TAFFY_SIGNING_KEYSTORE=$TAFFY_ANDROID_KEYSTORE
    TAFFY_SIGNING_ALIAS=$TAFFY_ANDROID_KEY_ALIAS
    TAFFY_SIGNING_PASSWORD=$TAFFY_ANDROID_KEYSTORE_PASSWORD
    TAFFY_SIGNING_SOURCE='the environment'
  else
    file=$(taffy_signing_env_file)
    [ -f "$file" ] || return 1
    # A world- or group-readable password file is the failure this whole
    # arrangement exists to avoid, so it is refused rather than warned about.
    #
    # shellcheck disable=SC2012  # `stat` spells permission bits differently on
    # GNU and BSD, and this suite is portable; the path is one this file
    # constructed, so the filename hazard `find` exists to handle cannot arise.
    case "$(ls -l "$file" | cut -c5-10)" in
      ------) : ;;
      *) die "$file is readable by more than its owner" "chmod 600 ${file}" ;;
    esac
    TAFFY_SIGNING_KEYSTORE=$(taffy_signing_env_value "$file" TAFFY_ANDROID_KEYSTORE || true)
    TAFFY_SIGNING_ALIAS=$(taffy_signing_env_value "$file" TAFFY_ANDROID_KEY_ALIAS || true)
    TAFFY_SIGNING_PASSWORD=$(taffy_signing_env_value "$file" TAFFY_ANDROID_KEYSTORE_PASSWORD || true)
    if [ -z "$TAFFY_SIGNING_KEYSTORE" ] || [ -z "$TAFFY_SIGNING_ALIAS" ] ||
       [ -z "$TAFFY_SIGNING_PASSWORD" ]; then
      die "$file names no complete signing identity" \
        "It needs all three: TAFFY_ANDROID_KEYSTORE, TAFFY_ANDROID_KEY_ALIAS, TAFFY_ANDROID_KEYSTORE_PASSWORD"
    fi
    TAFFY_SIGNING_SOURCE=${file#"$TAFFY_ROOT"/}
  fi
  case "$TAFFY_SIGNING_KEYSTORE" in
    /*) : ;;
    *) die "the signing keystore path must be absolute: $TAFFY_SIGNING_KEYSTORE" ;;
  esac
  [ -f "$TAFFY_SIGNING_KEYSTORE" ] || \
    die "no keystore at $TAFFY_SIGNING_KEYSTORE" "Named by $TAFFY_SIGNING_SOURCE"
  # A key inside the worktree is one `git add -A` from being published. It is
  # allowed only where Git is already told to ignore it.
  case "$TAFFY_SIGNING_KEYSTORE" in
    "$TAFFY_ROOT"/*)
      ( cd "$TAFFY_ROOT" && git check-ignore -q "$TAFFY_SIGNING_KEYSTORE" ) || die \
        "the keystore ${TAFFY_SIGNING_KEYSTORE#"$TAFFY_ROOT"/} is inside the repository and is not ignored" \
        "Move it out of the worktree, or under an ignored path such as .taffy/"
      ;;
  esac
  return 0
}
