# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# tools/github-release.d/selftest.sh — `./tools/github-release self-test`.
# Sourced by that command, never run alone. Targets bash 3.2.
#
# Two halves. facts_selftest.py holds the rules against recorded tool output
# and needs no SDK. The half below asks whether the command, run the way a
# person runs it, refuses what it should: it builds throwaway APKs and bundles
# with the host's own build-tools, signs them with keys generated for this run,
# and runs `prepare` over each. `draft` and `publish` then run against a
# stand-in for gh that records what it was asked and answers from a state
# file, so no network is reached and no repository is touched. Everything
# lives in one temporary directory, removed on exit.
#
# Where build-tools, a JDK or bundletool is missing, the second half skips and
# names what is missing. A skip is never counted as a pass.
#
# shellcheck shell=bash

ST_DIR=''
ST_RAN=0
ST_FAILED=0

# TAFFY_GITHUB_RELEASE_KEEP=1 keeps the directory, to read what a case printed.
st_cleanup() {
  [ -n "$ST_DIR" ] || return 0
  if [ -n "${TAFFY_GITHUB_RELEASE_KEEP:-}" ]; then info "kept $ST_DIR"; return 0; fi
  rm -rf "$ST_DIR"
}

# st_expect <name> <exit code> <words the output must hold> <command...>
st_expect() {
  local name=$1 want=$2 words=$3 code=0 log
  shift 3
  ST_RAN=$((ST_RAN + 1))
  log="$ST_DIR/run-$ST_RAN.log"
  "$@" >"$log" 2>&1 || code=$?
  if [ "$code" = "$want" ] && { [ -z "$words" ] || grep -qF -- "$words" "$log"; }; then
    ok "$name"
    return 0
  fi
  ST_FAILED=$((ST_FAILED + 1))
  bad "$name: exit $code, expected $want${words:+ and the words \"$words\"}"
  tail -n 12 "$log" | sed 's/^/         | /' >&2
}

# st_assert <name> <command...>: a pass when the command succeeds.
st_assert() {
  local name=$1
  shift
  ST_RAN=$((ST_RAN + 1))
  if "$@"; then ok "$name"; else ST_FAILED=$((ST_FAILED + 1)); bad "$name"; fi
}

# The command under test, with the throwaway key and the gh stand-in.
st_release() {
  (
    unset TAFFY_ANDROID_SIGNING_ENV
    export TAFFY_ANDROID_KEYSTORE="$ST_DIR/key-release" TAFFY_ANDROID_KEY_ALIAS=release
    export TAFFY_ANDROID_KEYSTORE_PASSWORD="$ST_PASS" TAFFY_GH="$ST_DIR/bin/gh"
    exec "$TAFFY_ROOT/tools/github-release" "$@"
  )
}

st_release_without_key() {
  (
    unset TAFFY_ANDROID_KEYSTORE TAFFY_ANDROID_KEY_ALIAS TAFFY_ANDROID_KEYSTORE_PASSWORD
    export TAFFY_ANDROID_SIGNING_ENV="$ST_DIR/no-signing.env"
    exec "$TAFFY_ROOT/tools/github-release" "$@"
  )
}

st_sums_verify() {
  local file digest
  read -r digest file < "$1/SHA256SUMS"
  [ "$(file_sha256 "$1/$file")" = "$digest" ]
}

# --- fixtures -----------------------------------------------------------------

st_find_tools() {
  local sdk
  ST_APKSIGNER=$(build_tool apksigner) || { warn "skipped: no build-tools with apksigner"; return 1; }
  ST_AAPT2=$(build_tool aapt2) || { warn "skipped: no build-tools with aapt2"; return 1; }
  ST_KEYTOOL=$(jdk_tool keytool) || { warn "skipped: no keytool"; return 1; }
  ST_JARSIGNER=$(jdk_tool jarsigner) || { warn "skipped: no jarsigner"; return 1; }
  ST_BUNDLETOOL=$(command -v bundletool || true)
  [ -n "$ST_BUNDLETOOL" ] || { warn "skipped: bundletool is not on PATH"; return 1; }
  sdk=$(taffy_android_sdk_root 2>/dev/null) || sdk=$(taffy_android_sdk_conventional 2>/dev/null) || sdk=''
  ST_ANDROID_JAR=$(find "$sdk/platforms" -mindepth 2 -maxdepth 2 -name android.jar 2>/dev/null \
    | sort | tail -n 1)
  [ -n "$ST_ANDROID_JAR" ] || { warn "skipped: no SDK platform with android.jar to link against"; return 1; }
}

st_key() { # <alias>
  "$ST_KEYTOOL" -genkeypair -keystore "$ST_DIR/key-$1" -storetype PKCS12 \
    -storepass:env ST_PASS -keypass:env ST_PASS -alias "$1" -keyalg EC -groupname secp256r1 \
    -validity 2 -dname "CN=github-release self-test $1" >/dev/null 2>&1
}

st_manifest() { # <dir> <package>
  cat > "$1/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="$2">
  <uses-sdk android:minSdkVersion="29" android:targetSdkVersion="29"/>
  <application android:hasCode="false"/>
</manifest>
EOF
}

st_link() { # <dir> <code> <name> <output> [aapt2 flags...]
  local dir=$1 code=$2 name=$3 output=$4
  shift 4
  "$ST_AAPT2" link "$@" -o "$output" -I "$ST_ANDROID_JAR" --manifest "$dir/AndroidManifest.xml" \
    --version-code "$code" --version-name "$name" >/dev/null
}

# st_apk <name> <signer> <code> <version name> [package] [abi, '' for none]
st_apk() {
  local work="$ST_DIR/build-$1.apk"
  mkdir -p "$work" || return 1
  st_manifest "$work" "${5:-com.taffygo.browser}" || return 1
  st_link "$work" "$3" "$4" "$work/unsigned.apk" || return 1
  if [ -n "${6-arm64-v8a}" ]; then
    python3 - "$work/unsigned.apk" "lib/${6-arm64-v8a}/libfixture.so" <<'PY' || return 1
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "a") as archive:
    archive.writestr(sys.argv[2], b"\x7fELF fixture")
PY
  fi
  "$ST_APKSIGNER" sign --ks "$ST_DIR/key-$2" --ks-pass env:ST_PASS --ks-key-alias "$2" \
    --out "$ST_DIR/$1.apk" "$work/unsigned.apk" >/dev/null 2>&1
}

# st_bundle <name> <signer, '' for unsigned> <code> <version name>
st_bundle() {
  local work="$ST_DIR/build-$1.aab"
  mkdir -p "$work" || return 1
  st_manifest "$work" com.taffygo.browser || return 1
  st_link "$work" "$3" "$4" "$work/proto.apk" --proto-format || return 1
  python3 - "$work/proto.apk" "$work/base.zip" <<'PY' || return 1
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as source, zipfile.ZipFile(sys.argv[2], "w") as module:
    for name in source.namelist():
        target = "manifest/AndroidManifest.xml" if name == "AndroidManifest.xml" else name
        module.writestr(target, source.read(name))
PY
  "$ST_BUNDLETOOL" build-bundle --modules="$work/base.zip" --output="$work/unsigned.aab" \
    >/dev/null 2>&1 || return 1
  if [ -z "$2" ]; then cp "$work/unsigned.aab" "$ST_DIR/$1.aab"; return; fi
  "$ST_JARSIGNER" -keystore "$ST_DIR/key-$2" -storepass:env ST_PASS -keypass:env ST_PASS \
    -signedjar "$ST_DIR/$1.aab" "$work/unsigned.aab" "$2" >/dev/null 2>&1
}

st_case() { # <case> <apk name or ''> <bundle name>
  mkdir -p "$ST_DIR/case-$1" || return 1
  if [ -n "$2" ]; then cp -p "$ST_DIR/$2.apk" "$ST_DIR/case-$1/$ST_APK_NAME.apk" || return 1; fi
  cp -p "$ST_DIR/$3.aab" "$ST_DIR/case-$1/$ST_BUNDLE_NAME.signed.aab"
}

st_fixtures() { # <floor> <good code> <pin>
  st_key release && st_key stranger || return 1
  st_apk good release "$2" "$3" && st_apk stranger stranger "$2" "$3" &&
    st_apk floor release "$1" "$3" && st_apk ahead release "$(($2 + 1))" "$3" &&
    st_apk package release "$2" "$3" org.chromium.chrome &&
    st_apk renamed release "$2" 1.2.3.4 &&
    st_apk no-native release "$2" "$3" com.taffygo.browser '' || return 1
  st_bundle good release "$2" "$3" && st_bundle stranger stranger "$2" "$3" &&
    st_bundle unsigned '' "$2" "$3" && st_bundle floor release "$1" "$3" || return 1
  st_case good good good && st_case stranger-apk stranger good &&
    st_case stranger-aab good stranger && st_case unsigned-aab good unsigned &&
    st_case floor floor floor && st_case ahead ahead good && st_case package package good &&
    st_case renamed renamed good && st_case no-native no-native good && st_case no-apk '' good ||
    return 1
  printf 'Fixture release notes.\n' > "$ST_DIR/notes.md"
  printf '==> gn gen (profile release-arm64)\nok built\nBUILD_EXIT=0\n' > "$ST_DIR/good.log"
  printf 'FAILED: obj/taffy/x.o\n==> (profile release-arm64)\nBUILD_EXIT=0\n' > "$ST_DIR/failed.log"
  printf '==> gn gen (profile release-arm64)\nBUILD_EXIT=1\n' > "$ST_DIR/exit1.log"
}

st_prepare() { # <case> [more options...]
  local name=$1
  shift
  st_release prepare --version 1.0.0 --notes "$ST_DIR/notes.md" --apks "$ST_DIR/case-$name" \
    --out "$ST_DIR/out-$name" "$@"
}

st_key_hex() {
  "$ST_KEYTOOL" -list -v -keystore "$ST_DIR/key-release" -storepass:env ST_PASS -alias release \
    2>/dev/null | sed -n 's/^[[:space:]]*SHA256: //p' | head -n 1 | tr -d ':' | tr 'A-F' 'a-f'
}

# --- the gh stand-in ----------------------------------------------------------

st_fake_gh() {
  mkdir -p "$ST_DIR/bin" "$ST_DIR/gh"
  cat > "$ST_DIR/bin/gh" <<'GH'
#!/usr/bin/env bash
# A stand-in for gh: it records every call, answers from a state file
# (absent, tag-exists, draft or published) and reaches no network.
set -eu
dir=$ST_GH_DIR
printf '%s\n' "$*" >> "$dir/calls"
state=$(cat "$dir/state")
digest() {
  if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
case "$1 ${2:-}" in
  "api repos/"*)
    case "$2" in
      */branches/main) printf 'main\n' ;;
      */git/ref/tags/*)
        [ "$state" = tag-exists ] || { printf 'gh: Not Found (HTTP 404)\n' >&2; exit 1; }
        printf 'refs/tags/fixture\n' ;;
      *) exit 1 ;;
    esac ;;
  "release view")
    case "$state" in draft|published) ;; *) printf 'release not found\n' >&2; exit 1 ;; esac
    case "$*" in
      *isDraft*) if [ "$state" = draft ]; then echo true; else echo false; fi ;;
      *assets*) cat "$dir/assets" ;;
    esac ;;
  "release create")
    : > "$dir/assets"
    previous=''
    for word in "$@"; do
      if [ -f "$word" ] && [ "$previous" != --notes-file ]; then
        printf '%s\t%s\tsha256:%s\n' "$(basename "$word")" "$(wc -c < "$word" | tr -d ' ')" \
          "$(digest "$word")" >> "$dir/assets"
      fi
      previous=$word
    done
    echo draft > "$dir/state" ;;
  "release edit") echo published > "$dir/state" ;;
  *) printf 'gh stand-in: no answer for: %s\n' "$*" >&2; exit 1 ;;
esac
GH
  chmod +x "$ST_DIR/bin/gh"
  ST_GH_DIR="$ST_DIR/gh"
  export ST_GH_DIR
}

st_gh_state() { printf '%s\n' "$1" > "$ST_DIR/gh/state"; : > "$ST_DIR/gh/calls"; }
st_gh_silent() { [ ! -s "$ST_DIR/gh/calls" ]; }
st_gh_called() { grep -qxF -- "$1" "$ST_DIR/gh/calls"; }
st_gh_never() { ! grep -qF -- "$1" "$ST_DIR/gh/calls"; }

# --- the cases ------------------------------------------------------------------

st_prepare_cases() { # <floor> <good code>
  st_expect "a release signed by the configured key is prepared" 0 "wrote" \
    st_prepare good --build-log "$ST_DIR/good.log"
  st_assert "SHA256SUMS holds the copied APK's digest" st_sums_verify "$ST_DIR/out-good"
  st_assert "the notes carry the key's certificate fingerprint" \
    grep -qF "$(st_key_hex)" "$ST_DIR/out-good/release-notes.md"
  st_expect "an APK signed by another key is refused" 1 "the APK is signed by" \
    st_prepare stranger-apk
  st_assert "a refused run writes nothing" test ! -e "$ST_DIR/out-stranger-apk"
  st_expect "a bundle signed by another key is refused" 1 "the bundle is signed by" \
    st_prepare stranger-aab
  st_expect "an unsigned bundle is refused" 1 "the bundle carries no signing certificate" \
    st_prepare unsigned-aab
  st_expect "the version code Play already took is refused" 1 "is not above $1" st_prepare floor
  st_expect "--min-version-code raises the bar" 1 "is not above --min-version-code" \
    st_prepare good --min-version-code "$(($2 + 10))"
  st_expect "an APK and a bundle from two builds are refused" 1 "not from one build" \
    st_prepare ahead
  st_expect "another package is refused" 1 "not com.taffygo.browser" st_prepare package
  st_expect "a version name other than the pinned Chromium is refused" 1 \
    "not the pinned Chromium" st_prepare renamed
  st_expect "an APK with no arm64-v8a code is refused" 1 "no ABI at all" st_prepare no-native
  st_expect "a missing APK is refused" 1 "no APK at" st_prepare no-apk
  st_expect "missing notes are refused" 1 "no release notes" \
    st_prepare good --notes "$ST_DIR/absent.md"
  st_expect "a build log with a FAILED: line is refused" 1 "FAILED: line" \
    st_prepare good --build-log "$ST_DIR/failed.log"
  st_expect "a build log ending BUILD_EXIT=1 is refused" 1 "does not end with BUILD_EXIT=0" \
    st_prepare good --build-log "$ST_DIR/exit1.log"
  st_expect "no signing identity is refused" 1 "certificate is unknown" \
    st_release_without_key prepare --version 1.0.0 --notes "$ST_DIR/notes.md" \
    --apks "$ST_DIR/case-good" --out "$ST_DIR/out-no-key"
  st_expect "an output directory inside the repository is refused" 1 "inside this repository" \
    st_prepare good --out "$TAFFY_ROOT/out-github-release-self-test"
}

st_remote_cases() { # <pin>
  local out="$ST_DIR/out-good" repo=fixture-owner/fixture-repo
  st_fake_gh
  st_gh_state absent
  st_expect "publish without --yes is refused" 1 "needs --yes" \
    st_release publish --version 1.0.0 --repo "$repo" --out "$out"
  st_assert "  and asks gh nothing" st_gh_silent
  st_expect "draft without --repo is refused" 1 "--repo is required" \
    st_release draft --version 1.0.0 --out "$out"
  st_gh_state published
  st_expect "draft refuses a release that exists" 1 "already exists" \
    st_release draft --version 1.0.0 --repo "$repo" --out "$out"
  st_assert "  and creates nothing" st_gh_never "release create"
  st_gh_state tag-exists
  st_expect "draft refuses a tag that exists" 1 "the tag v1.0.0 already exists" \
    st_release draft --version 1.0.0 --repo "$repo" --out "$out"
  cp -Rp "$out" "$ST_DIR/tampered"
  printf 'x' >> "$ST_DIR/tampered/TaffyGo-1.0.0-arm64.apk"
  st_gh_state absent
  st_expect "draft refuses an APK changed since prepare" 1 "no longer matches" \
    st_release draft --version 1.0.0 --repo "$repo" --out "$ST_DIR/tampered"
  st_assert "  and asks gh nothing" st_gh_silent
  st_expect "draft creates a draft" 0 "draft v1.0.0 on $repo" \
    st_release draft --version 1.0.0 --repo "$repo" --out "$out"
  st_assert "  with --draft, the title, the notes and both files" st_gh_called \
    "release create v1.0.0 --repo $repo --target main --draft --title TaffyGo 1.0 (Chromium $1) --notes-file $out/release-notes.md $out/TaffyGo-1.0.0-arm64.apk $out/SHA256SUMS"
  st_assert "  and publishes nothing" st_gh_never "--draft=false"
  st_expect "publish --yes makes the draft public" 0 "is public on $repo" \
    st_release publish --version 1.0.0 --repo "$repo" --yes --out "$out"
  st_assert "  through gh release edit --draft=false" st_gh_called \
    "release edit v1.0.0 --repo $repo --draft=false"
  st_expect "publish refuses a release that is already public" 1 "already public" \
    st_release publish --version 1.0.0 --repo "$repo" --yes --out "$out"
  st_gh_state absent
  st_expect "publish refuses when there is no release" 1 "there is no release" \
    st_release publish --version 1.0.0 --repo "$repo" --yes --out "$out"
}

command_self_test() {
  require_python
  local failed=0 floor pin good
  step "github-release: the rules, over recorded tool output"
  python3 "$RELEASE_D/facts_selftest.py" || failed=1

  step "github-release: end to end, with throwaway keys, artifacts and a gh stand-in"
  if ! st_find_tools; then
    [ "$failed" = 0 ] || return 1
    warn "the end-to-end half did not run; only the recorded-output rules passed"
    return 0
  fi
  floor=$(taffy_kv_get "$RELEASE_D/version-floor" play_accepted_version_code)
  pin=$(taffy_chromium_pin tag)
  good=$((floor + 1))
  ST_APK_NAME=$(branding_name taffy_apk_name)
  ST_BUNDLE_NAME=$(branding_name taffy_bundle_name)
  ST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/github-release-self-test.XXXXXX")
  trap st_cleanup EXIT
  # A password made for this run and gone with it; nothing here is a real key.
  ST_PASS=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n')
  export ST_PASS
  info "building fixtures in $ST_DIR"
  st_fixtures "$floor" "$good" "$pin" || die "the fixtures could not be built" \
    "Every tool above was found, so this is a self-test defect or a broken SDK install."
  st_prepare_cases "$floor" "$good"
  st_remote_cases "$pin"

  if [ "$ST_FAILED" -gt 0 ] || [ "$failed" != 0 ]; then
    bad "github-release self-test: $ST_FAILED of $ST_RAN end-to-end checks failed"
    return 1
  fi
  ok "github-release self-test: the recorded-output rules and $ST_RAN end-to-end checks pass"
}
