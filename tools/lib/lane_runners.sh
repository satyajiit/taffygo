#!/usr/bin/env bash
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

#
# What each lane actually runs.
#
# One runner per lane, plus the shared step/skip helpers they call. A runner
# never decides whether it may run — lane_skip_unready does that from the
# predicate in lane_readiness.sh — so the two files cannot drift into
# disagreeing about a host.
#
# Sourced by tools/lib/lanes.sh, never on its own and never executed.
# Targets bash 3.2.
#
# shellcheck shell=bash

# --- running ----------------------------------------------------------------
# Everything below needs ./tools/check's reporting helpers and $TMP.

TAFFY_LANE_LOG_LINES=120
TAFFY_LANE_LOG_SEQ=0

lane_skip() { # <lane> <reason> [remediation]
  skip "$1: $2"
  if [ -n "${3:-}" ]; then note "$3"; fi
  return 0
}

# lane_skip_unready <lane> — skip with the predicate's own reason and remedy.
# 0 when the lane is ready and the caller should continue, 1 when it skipped.
lane_skip_unready() {
  local lane=$1 blocker
  blocker=$(lane_ready "$lane") && return 0
  lane_skip "$lane" "${blocker%%	*}" "${blocker#*	}"
  return 1
}

lane_show_log() { # <log-file>
  local log=$1 total
  total=$(wc -l < "$log" | tr -d ' ')
  if [ "$total" -gt "$TAFFY_LANE_LOG_LINES" ]; then
    note "(showing the last $TAFFY_LANE_LOG_LINES of $total output lines)"
  fi
  tail -n "$TAFFY_LANE_LOG_LINES" "$log" | sed 's/^/      /' >&2
  return 0
}

# lane_run <lane> <remediation> <command> [args...]
# Runs the command from the repository root, prints it before running it, and
# on failure prints its output and fails the gate with the remediation.
lane_run() {
  local lane=$1 remedy=$2; shift 2
  local log rc=0
  TAFFY_LANE_LOG_SEQ=$((TAFFY_LANE_LOG_SEQ + 1))
  log="$TMP/lane-$lane-$TAFFY_LANE_LOG_SEQ.log"
  note "\$ $*"
  ( cd "$TAFFY_ROOT" && "$@" ) > "$log" 2>&1 || rc=$?
  if [ "$rc" -eq 0 ]; then
    pass "$lane: $*"
  else
    lane_show_log "$log"
    hard "$lane: \`$*\` exited $rc — $remedy"
  fi
  return 0
}

# lane_run_soft <lane> <remediation> <command> [args...]
# lane_run for a command that reports part of what it found as a warning rather
# than as a non-zero exit. A zero exit is still a pass, but every output line
# beginning `warning:` is counted through `soft`, so the gate's own tally names
# it. Without this the finding exists only inside a log lane_run reads on
# failure and discards on success — which is exactly the shape a warning is
# already in danger of having, and the reason the one that exists (a price
# rewritten under a base plan Play has already activated) was invisible to the
# command this repository tells authors to run. Failure is handled exactly as
# lane_run handles it: the log, then the remediation.
lane_run_soft() {
  local lane=$1 remedy=$2; shift 2
  local log rc=0 warnings=0 line
  TAFFY_LANE_LOG_SEQ=$((TAFFY_LANE_LOG_SEQ + 1))
  log="$TMP/lane-$lane-$TAFFY_LANE_LOG_SEQ.log"
  note "\$ $*"
  ( cd "$TAFFY_ROOT" && "$@" ) > "$log" 2>&1 || rc=$?
  if [ "$rc" -ne 0 ]; then
    lane_show_log "$log"
    hard "$lane: \`$*\` exited $rc — $remedy"
    return 0
  fi
  # No pipeline: `soft` increments a counter in ./tools/check's own shell, and a
  # subshell would carry every increment away with it.
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    soft "$lane: $line"
    warnings=$((warnings + 1))
  done <<EOF
$(sed -n 's/^[[:space:]]*\(warning:.*\)$/\1/p' "$log")
EOF
  [ "$warnings" -gt 0 ] || pass "$lane: $*"
  return 0
}

# The embedded baseline is the catalog layer a device routes against on first
# run and offline, so decision 0015 requires a build to fail on a stale one.
# Nothing else can notice: the baseline is generated JSON, the source that
# produced it is three files beside it, and `cargo` compiles whichever bytes it
# finds. This lane is the only thing that reads both.
lane_run_architecture() {
  step "architecture: //taffy component and dependency boundaries (taffy-core/)"
  lane_skip_unready architecture || return 0
  lane_run architecture "Fix the component row or dependency direction reported above. taffy-core/build/components.toml is the only component register." \
    python3 taffy-core/build/component_graph.py --check
  lane_run architecture "The component validator no longer rejects a deliberately broken graph, so its passing result proves nothing." \
    python3 taffy-core/build/component_graph.py --self-test
  lane_run architecture "The active GN targets or their first-party direct dependencies differ from taffy-core/build/components.toml. Declare the exact edge or fix the BUILD.gn; do not add a second graph." \
    python3 taffy-core/build/component_gn_graph.py
  lane_run architecture "The GN boundary checker no longer catches drift, unresolved variables, generated-target ownership, or platform selection." \
    python3 taffy-core/build/component_gn_graph.py --self-test
  lane_run architecture "The shared design-token projections are stale. Regenerate with \`python3 taffy-core/resources/tokens/generate.py --write\` and commit all three outputs." \
    python3 taffy-core/resources/tokens/generate.py --check
  lane_run architecture "The design-token validator no longer rejects broken theme parity or alpha semantics, so its generated projections cannot be trusted." \
    python3 taffy-core/resources/tokens/generate.py --self-test
  lane_run architecture "The product capability projections or Chromium profile selections are stale. Regenerate with \`python3 taffy-core/build/product_capabilities.py --write\`; release-arm64 must remain on the candidate surface." \
    python3 taffy-core/build/product_capabilities.py --check
  lane_run architecture "The capability-profile validator no longer rejects candidate exposure above the accepted milestone, task/policy disagreement, or an unclassified Chromium profile." \
    python3 taffy-core/build/product_capabilities.py --self-test
  lane_run architecture "The browser's process ceiling is stricter in shape than the renderer endpoint declares in taffy-core/renderer/observation_limits.json. Raise the clamp: a shape bound below what the endpoint serves recuts every page and bounds no work (decision 0170)." \
    python3 taffy-core/build/observation_ceilings.py
  lane_run architecture "The observation-ceiling check no longer rejects a clamp that is stricter in shape than the endpoint, so the two copies of the bounds can drift again unseen (OD-142)." \
    python3 taffy-core/build/observation_ceilings.py --self-test
  return 0
}

lane_run_evidence() {
  step "evidence: revision-bound milestone and candidate evidence index"
  lane_skip_unready evidence || return 0
  lane_run evidence "The evidence index no longer rejects a missing milestone, a forged readiness verdict, a duplicate record, an unknown field or changed bytes. Fix the assembler and schema before trusting any candidate manifest." \
    ./tools/evidence self-test
  return 0
}

lane_run_catalog() {
  step "catalog: the catalogs compiled into the product, and the tools that fill their rows"
  lane_skip_unready catalog || return 0
  lane_run catalog "The committed baseline no longer matches its source. Regenerate with \`python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --write\` and commit the result." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --check
  lane_run catalog "The generator's own rules no longer fire on a deliberately broken source, so the check above was passing on a generator that checks nothing." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/generate_baseline.py --self-test
  lane_run catalog "The pi catalog import no longer refuses what it must, so the next run of it would write rows nobody checked. The import itself reaches the network and this lane never runs it — this is the only thing that watches its mapping." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/import_pi_catalog.py --self-test
  lane_run catalog "The baseline generator no longer projects the kernel's catalog vocabulary — an enum label respelled on one side. Both parsers fail closed, so the symptom of ignoring this is a baseline whose rows are silently dropped on the device. The kernel's \`src/catalog/types.rs\` is the authority; fix the generator tuple the finding names." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/check_schema_agreement.py
  lane_run catalog "The schema-agreement comparison no longer fires on a known break, so the check above proves nothing." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/check_schema_agreement.py --self-test

  lane_run catalog "The four tables that decide a subscription sign-in no longer agree. Adding or enabling a vendor is a change to its catalog row, the core's \`SIGN_IN_VENDORS\`, the Android flow map and the browser's vendor table, and every one of those fails silently — the symptom is a compiled sign-in nothing can reach, a control a person can press that cannot start, or a screen drawn for one flow shape over a flow running another." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/check_vendor_agreement.py
  lane_run catalog "The vendor-agreement comparisons no longer fire on a known break, or an extractor stopped refusing a table it cannot find, so the check above would pass having compared nothing." \
    python3 taffy-core/components/intelligence/core/rust/model-router/catalog/tools/check_vendor_agreement.py --self-test

  lane_run catalog "The generated asset table no longer matches its source, so the device would fetch a set of artifacts nobody reviewed. Regenerate with \`python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py --write\` and commit the result." \
    python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py --check
  lane_run catalog "The asset catalog's own rules no longer fire on a deliberately broken source. Two matter most. The rule refusing a digest on a variant whose bytes do not exist: without it a placeholder can be committed and later read as a pin. And the rule refusing a model row whose bytes have no recorded upstream URL and no licence record in this tree: the \`files\` lane sweeps shipping binaries from disk and a delivery-plane artifact is never on this disk, so this is the only place that obligation is enforced." \
    python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py --self-test

  lane_run catalog "The generated bundled-asset projections no longer match the catalog's \`bundled\` block. That is quiet in both directions and neither shows up in a build: an artifact left out of the GN list is committed, checked, and in no APK, and a row left out of the browser's table is an artifact nothing seeds and the core then plans a download for against an origin that is empty. Regenerate with \`python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_bundled_assets.py --write\` and commit the result." \
    python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_bundled_assets.py --check
  lane_run catalog "The bundled-asset projection no longer agrees with the block it came from, or its two GN lists no longer agree in order. The second is the dangerous one: \`android_assets\` reads renaming_sources and renaming_destinations positionally, so a pair out of step packages one artifact under another's name, and nothing downstream can see it because both names exist." \
    python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_bundled_assets.py --self-test

  lane_run catalog "The artifact archive is no longer deterministic, so a rebuild of the same tree would produce a different SHA-256 and every published catalog row naming one would become a promise the build cannot keep." \
    python3 taffy-core/third_party/cpython/tools/artifact_zip.py --self-test
  lane_run catalog "The Chromium sync helper no longer verifies the pinned CPython archive before extraction, no longer refuses traversal, or cannot reuse an already verified mount offline. Any of those makes the interpreter source a machine-local accident rather than the manifest's build input." \
    python3 tools/chromium/lib/cpython_source.py --self-test
  lane_run catalog "The standard-library packager no longer refuses what it must: a tree that is not a standard library, a platform the catalog does not publish for, or the \`unsupported\` platform. Each refusal is what stops a wrong artifact being built and then pinned." \
    python3 taffy-core/third_party/cpython/tools/build_stdlib.py --self-test
  lane_run catalog "The package allowlist parser no longer refuses a malformed row. A space-separated row that parses is an allowlist entry nobody wrote, and a compiled extension that packages is an ABI nobody agreed to ship." \
    python3 taffy-core/third_party/cpython/tools/build_packages.py --self-test
  lane_run catalog "The interpreter build tool no longer holds the three rules that make its output shippable: every extension compiled in, because a sandboxed process cannot dlopen one; every loadable segment aligned to 16 KiB, because a device with that page size refuses a binary aligned for 4 KiB; and \`_ctypes\` and \`_socket\` configured out, because the sandbox has neither native calls nor network. Each is invisible until a phone refuses the APK or an import fails." \
    python3 taffy-core/third_party/cpython/tools/build_interpreter.py --self-test
  lane_run catalog "The frozen bootstrap no longer serves the standard library out of a memory-mapped archive with \`sys.path\` empty, or no longer refuses byte code built for a different interpreter. Without the first the sandboxed worker has no library; without the second it would execute code the shipped interpreter never agreed to." \
    python3 taffy-core/third_party/cpython/tools/check_bootstrap.py --self-test
  lane_run catalog "The module freezer no longer refuses a build interpreter that is not the pin, or no longer emits a table of extra rows. The first produces a binary that fails on its first frozen import with an error naming the module rather than the format; the second would substitute CPython's own frozen table and take \`importlib\` out with it." \
    python3 taffy-core/third_party/cpython/tools/freeze_modules.py --self-test
  lane_run catalog "The publication checker no longer holds its rules. It is the only thing that can tell a catalog row marked \`published\` from bytes that are actually at the URL: everything static passes on a row whose object answers 404, and the device is where that is found otherwise." \
    python3 tools/lib/asset_publish.py self-test

  lane_run catalog "The filter-list pack builder no longer refuses a snapshot whose digest moved, or no longer rebuilds byte-identically. The first lets a quietly edited rule set become a pack that claims a pinned revision; the second makes the digest recorded in the provenance file a number nobody can reproduce." \
    python3 taffy-core/third_party/easylist/tools/build_filter_pack.py --self-test
  lane_run catalog "The country-flag rasteriser no longer holds its own properties. Its output is part of the pack's bytes, so a rasteriser that has drifted moves the digest with no input having changed, and a catalog row that was true yesterday becomes false today." \
    python3 taffy-core/third_party/flag-icons/tools/svg_raster.py --self-test
  lane_run catalog "The country-flag pack builder no longer refuses what decision 0047 requires of it: a code with no artwork, a flag with no code, or a rebuild that is not byte-identical. The reconciliation is what keeps the shipped set exactly ISO 3166-1 rather than whatever upstream happened to carry." \
    python3 taffy-core/third_party/flag-icons/tools/build_flag_pack.py --self-test
  lane_run catalog "The start-scene pack builder no longer refuses what decision 0145 requires of it: a member with no painting, a painting no member names, a canvas that is not the size the source decodes against, or a rebuild that is not byte-identical. It runs no encoder, so the reconciliation is the whole of what stands between the committed artwork and a published artifact." \
    python3 taffy-core/ui/android/tools/scenes/build_scene_pack.py --self-test

  lane_run catalog "The generated entrypoint registry no longer matches its source, so the table deciding what a sandboxed worker may ever be asked to do is not the one anybody reviewed. Regenerate with \`python3 taffy-core/components/tools/entrypoints/tools/generate_entrypoints.py --write\` and commit all three artifacts." \
    python3 taffy-core/components/tools/entrypoints/tools/generate_entrypoints.py --check
  lane_run catalog "The entrypoint registry's own rules no longer fire on a deliberately broken source. The one that matters most is the rule refusing a key that names a location or a body of code: without it a row can carry a module, a path or a command line, and a frozen registry becomes a loader nobody meant to write." \
    python3 taffy-core/components/tools/entrypoints/tools/generate_entrypoints.py --self-test
  return 0
}

lane_run_rust() {
  step "rust: the portable Cargo workspace (taffy-core/)"
  lane_skip_unready rust || return 0
  note "$(rustc --version 2>/dev/null || printf 'rustc version unknown'), pinned by rust-toolchain.toml"

  if cargo fmt --version >/dev/null 2>&1; then
    lane_run rust "Run \`cargo fmt --all\` and commit the result." \
      cargo fmt --all -- --check
  else
    lane_skip rust "the rustfmt component is not installed" "rustup component add rustfmt"
  fi

  if cargo clippy --version >/dev/null 2>&1; then
    lane_run rust "Fix the lint, or justify it with a scoped #[allow] carrying a reason." \
      cargo clippy --workspace --all-targets --all-features -- -D warnings
  else
    lane_skip rust "the clippy component is not installed" "rustup component add clippy"
  fi

  lane_run rust "Reproduce with \`cargo test --workspace --locked\` and fix the failing test." \
    cargo test --workspace --locked
  return 0
}

# The `//taffy` product build graph, as three claims a build would otherwise be the
# first thing to test.
#
# `crate_sources.gni` is the reason this lane exists. AGENTS.md records the hole
# in "What no gate checks": adding, renaming, splitting or deleting a `.rs` file
# under a `taffy-core` Rust component invalidates that generated file, and until now nothing here
# regenerated or verified it, so the miss was silent until a Chromium build on
# whichever host had one. Its Kotlin twin has had the `mount` lane since the
# island landed; this is the shape it owed.
#
# The others are the same kind of claim about the same directory:
# check_build_graph.py proves the shipping targets carry no test-only source or
# dependency and that `service_bridge` lists every module it compiles,
# check_build_reachability.py proves no BUILD.gn in the product root is an
# orphan, check_gn_source_paths.py proves every file those build files name is
# on the disk — which is a different question, and the one that was open while
# an asset target outlived the thirty-one files it listed — check_publication_totality.py proves every mutating bridge entry
# declares how its state change reaches the status publication, and
# check_effect_withdrawal.py proves publication failure cannot release an
# effect from the runtime it just discarded, and
# check_command_dispatch_coverage.py proves every command kind the contract
# carries reaches a plane that carries it rather than falling through to one
# that refuses it.
# None of them needs a checkout, a device or a Rust toolchain.
lane_run_rust-graph() {
  step "rust-graph: the //taffy product build graph (taffy-core/)"
  lane_skip_unready rust-graph || return 0
  lane_run rust-graph "The generated crate source list is stale. Regenerate with \`python3 taffy-core/services/core/tools/generate_crate_sources.py --write\` and commit the result." \
    python3 taffy-core/services/core/tools/generate_crate_sources.py --check
  lane_run rust-graph "A shipping target names a test-only source or dependency, or service_bridge's source list and its directory disagree. taffy-core/services/core/README.md owns the rules." \
    python3 taffy-core/services/core/tools/check_build_graph.py
  lane_run rust-graph "A BUILD.gn in the product root is not reachable from the root build file, so nothing it declares is ever built. Add the edge, or delete the file." \
    python3 taffy-core/build/tools/check_build_reachability.py
  lane_run rust-graph "The GN source-path checker's own fixtures fail, so its verdict about the build files proves nothing. Fix the tool before trusting the check below." \
    python3 taffy-core/build/tools/check_gn_source_paths.py --self-test
  lane_run rust-graph "A //taffy build file names a source file that is not there. Repoint the list or delete the target; a path with no file is a ninja edge with no rule to make it, and it fails at the end of a build rather than here." \
    python3 taffy-core/build/tools/check_gn_source_paths.py
  lane_run rust-graph "The publication-totality checker's own fixtures fail, so its verdict about the bridge proves nothing. Fix the tool before trusting the check below." \
    python3 taffy-core/services/core/tools/check_publication_totality.py --self-test
  lane_run rust-graph "A bridge entry mutates the runtime without a declared publication discipline, so surfaces would keep a stale projection. taffy-core/services/core/tools/check_publication_totality.py names the entry and the registers." \
    python3 taffy-core/services/core/tools/check_publication_totality.py
  lane_run rust-graph "The effect-withdrawal checker's own fixtures fail, so its verdict about publication failure proves nothing. Fix the tool before trusting the check below." \
    python3 taffy-core/services/core/tools/check_effect_withdrawal.py --self-test
  lane_run rust-graph "A failed state publication can dispatch an effect after discarding the runtime that authorized it. taffy-core/services/core/tools/check_effect_withdrawal.py names the missing vector or path." \
    python3 taffy-core/services/core/tools/check_effect_withdrawal.py
  lane_run rust-graph "The command-dispatch checker's own fixtures fail, so its verdict about the seam proves nothing. Fix the tool before trusting the check below." \
    python3 taffy-core/services/core/tools/check_command_dispatch_coverage.py --self-test
  lane_run rust-graph "A Core Service command kind reaches a projection that refuses it, so the browser is answered kInvalidCommand for a command the core implements. taffy-core/services/core/tools/check_command_dispatch_coverage.py names the kind and the plane." \
    python3 taffy-core/services/core/tools/check_command_dispatch_coverage.py

  lane_run rust-graph "self-test" \
    python3 taffy-core/services/core/tools/check_closed_enum_ceilings.py --self-test
  lane_run rust-graph "A ClosedEnum ceiling names a member that is no longer the enumeration's last, so every member added after it is refused by a projection that compiles. taffy-core/services/core/tools/check_closed_enum_ceilings.py names the site and the member." \
    python3 taffy-core/services/core/tools/check_closed_enum_ceilings.py
  return 0
}

lane_run_contracts() {
  step "contracts: BIP, browsing, Core API, Core Service, and Tool Runtime"
  lane_skip_unready contracts || return 0
  local contract
  for contract in bip browsing core-api core-service tool-runtime; do
    lane_run contracts "Regenerate $contract with \`python3 taffy-core/contracts/$contract/codegen/generate.py --write\` and commit the result." \
      python3 "taffy-core/contracts/$contract/codegen/generate.py" --check
    lane_run contracts "$contract generator self-tests fail, so its output cannot be trusted; fix the generator first." \
      python3 "taffy-core/contracts/$contract/codegen/generate.py" --self-test
    lane_run contracts "$contract golden or compatibility fixtures disagree with the schema; fix the source of truth, never both sides at once." \
      python3 "taffy-core/contracts/$contract/codegen/generate.py" --verify
  done
  lane_run contracts "Core Service and Tool Runtime duplicate a bounded projection of the tool protocol; align their shared types, pairings, and limits." \
    python3 taffy-core/contracts/codegen/cross_contracts.py
  lane_run contracts "The cross-contract drift checker failed its mutation tests." \
    python3 taffy-core/contracts/codegen/cross_contracts.py --self-test
  # A facade method and the body record it carries are two independent lists,
  # and a field added to the body alone validates perfectly while being
  # unreachable. That has happened twice; this is the rule between them.
  lane_run contracts "A facade method and the body record it carries disagree; a body field no argument supplies can never cross the seam, so the schema's facade_bodies binding is what changes." \
    python3 taffy-core/contracts/codegen/facade_bodies.py
  lane_run contracts "The facade-to-body checker failed its own refusal tests, so a pass from it means nothing." \
    python3 taffy-core/contracts/codegen/facade_bodies.py --self-test
  # The twelve CoreStatus payload fixtures are derived, not typed, and
  # `--refreeze` does not touch them: it replaces the frozen golden bytes and
  # nothing else. They went stale once already -- the corpus sat at codec
  # version 6 while the codec wrote 7 -- and the deviation fixtures would still
  # have drawn their refusals, so only the one fixture whose job is to prove a
  # current payload decodes would have said anything.
  lane_run contracts "The CoreStatus payload fixtures are not what the schema derives, so one was hand-edited or the codec moved without them. Run \`python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --write\` and commit the result." \
    python3 taffy-core/contracts/core-api/codegen/status_payload_corpus.py --check
  lane_run contracts "The Mojo projection and the schema disagree; taffy-core/contracts/bip is the authority, so the .mojom file is what changes." \
    python3 taffy-core/contracts/bip/codegen/generate.py --mojom
  lane_run contracts "The generated TypeScript does not compile. Fix the generator, never the generated file; every contract's TypeScript is one \`--write\` away from the schema." \
    python3 taffy-core/contracts/codegen/typescript_typecheck.py
  lane_run contracts "The TypeScript typecheck failed its own refusal tests, so a pass from it means nothing." \
    python3 taffy-core/contracts/codegen/typescript_typecheck.py --self-test
  # Not a sixth contract: the durable journal's schema and its migration ladder.
  # It is checked here because it is the same kind of claim the four contracts
  # make -- a generated file that must still agree with the document it came
  # from -- and it needs nothing but the python3 this lane already requires.
  # Nothing ran it before, in any lane or in GN, so the header could disagree
  # with its source indefinitely; the migration ladder did, and the first thing
  # that noticed was a phone aborting on start.
  lane_run contracts "Regenerate the journal schema with \`python3 taffy-core/components/storage/core/schema/generate_core_service_schema.py --write\` and commit the result. If it refuses, a migration no longer lands on the head schema and the message names the difference." \
    python3 taffy-core/components/storage/core/schema/generate_core_service_schema.py --check
  return 0
}

lane_run_kotlin() {
  step "kotlin: the Compose product UI (taffy-core/ui/android/)"
  lane_skip_unready kotlin || return 0
  local sdk api
  sdk=$(taffy_android_sdk_root)
  api=$(taffy_android_compile_sdk)
  note "Android SDK $sdk, compileSdk $api, $(java -version 2>&1 | head -n 1)"
  lane_run kotlin "Reproduce with \`./gradlew lintDebug testDebugUnitTest\`; the report paths are in the output above." \
    ./gradlew --console=plain lintDebug testDebugUnitTest
  return 0
}

# lane_run_pnpm <lane> <package-dir> <script>...
lane_run_pnpm() {
  local lane=$1 dir=$2; shift 2
  local script
  for script in "$@"; do
    lane_run "$lane" "Reproduce with \`pnpm --dir $dir $script\` and fix what it reports." \
      pnpm --dir "$dir" "$script"
  done
  return 0
}

lane_run_website() {
  step "website: the landing page (website/)"
  lane_skip_unready website || return 0
  # shellcheck disable=SC2086  # the script list is deliberately word-split
  lane_run_pnpm website website $TAFFY_NODE_CHECK_SCRIPTS
  return 0
}

# PAR-L10N-001 says every string is externalized and pseudo-localization is
# tested. Both of those are text properties of files in this repository, so
# they are provable here rather than on the Chromium track — and until this
# lane existed they were provable and unproven, which is the same as unproven.
lane_run_strings() {
  step "strings: externalized user-visible text (PAR-L10N-001)"
  lane_skip_unready strings || return 0
  # --parity-root reaches every Compose product module. Their translations ship
  # today, so a dropped one is a defect even when a GN source projection has
  # not consumed that module yet.
  lane_run strings "A user-visible string is not in a catalogue, a catalogue and its consumers disagree, or a locale lost a translation. The catalogue is the authority." \
    python3 taffy-core/resources/catalog/tools/check_strings.py \
      --parity-root taffy-core/ui/android
  lane_run strings "The pseudo-localization transform does not round-trip, it dropped a format placeholder, or the translation-parity rule stopped reporting a dropped translation — so the catalogue check above would be passing on a check that does nothing." \
    python3 taffy-core/resources/catalog/tools/check_strings.py --self-test
  return 0
}

lane_run_fixtures() {
  step "fixtures: the web fixture corpus (test-fixtures/web/)"
  lane_skip_unready fixtures || return 0
  lane_run fixtures "The corpus manifest and the files on disk disagree; a fixture change is a version bump, not an edit." \
    python3 test-fixtures/web/check.py
  return 0
}

# The scenario half of the Task Benchmark, and a lane of its own rather than a
# second command inside `fixtures`. The two corpora fail for different reasons —
# a page moved, or a scenario's expectation did — and a lane that reported both
# under one name would make a reader guess which. This lane also runs the
# corpus's self-test, because a validator nobody has watched fail is a validator
# that can quietly stop validating.
lane_run_fixtures-tasks() {
  step "fixtures-tasks: the Task Benchmark scenario corpus (test-fixtures/tasks/)"
  lane_skip_unready fixtures-tasks || return 0
  lane_run fixtures-tasks "The manifest and the scenarios on disk disagree, or a page-fixture, action, result-code or event reference no longer resolves; a scenario change is a version bump, not an edit." \
    python3 test-fixtures/tasks/check.py
  lane_run fixtures-tasks "The corpus rules no longer fire on a deliberately broken corpus, so the check above was passing on a validator that validates nothing." \
    python3 test-fixtures/tasks/check.py --self-test
  lane_run fixtures-tasks "The scorer accepted malformed or reordered adapter evidence; reproduce with python3 tools/benchmark.d/score_selftest.py." \
    python3 tools/benchmark.d/score_selftest.py
  lane_run fixtures-tasks "The local timing reader accepted incomplete measurement evidence; reproduce with ./tools/benchmark --local-loop --self-test." \
    python3 tools/benchmark.d/local_loop.py --self-test
  return 0
}

# The Android UI commits its launcher icons and in-app brand marks rather than
# deriving them in the Gradle build, so nothing else would notice if a mark
# were edited by hand, filed under the wrong density, or left behind by a brand
# change. This lane is what notices: it re-reads the brand masters' digests,
# the committed files' digests, and the pixel size each file records about
# itself.
# The generated GN source list for the Android product Kotlin, and the mount
# contract it is generated from. `crate_sources.gni`, its Rust twin, shipped
# without a lane and AGENTS.md records the consequence: a renamed file stays
# silently stale until a Chromium build fails on a builder, hours later. This
# lane is why that does not happen twice.
lane_run_mount() {
  step "mount: Android UI sources consumed by the product (taffy-core/app/android/)"
  lane_skip_unready mount || return 0
  lane_run mount "The shell-source census checker does not reject its broken fixtures, so its verdict about shell_java proves nothing." \
    python3 taffy-core/app/android/tools/check_shell_sources.py --self-test
  lane_run mount "The hand-maintained shell_java source list and app/android/shell/java disagree. Add or remove the exact path in app/android/shell/BUILD.gn; every source must be named in both directions." \
    python3 taffy-core/app/android/tools/check_shell_sources.py
  lane_run mount "The mount contract and the tree disagree. A finding of the form \`<module>: no mount at ...; run --mount\` means the gitignored mount directory on this host is stale, not the contract: run \`python3 taffy-core/app/android/tools/kotlin_mounts.py --mount\` (idempotent; it writes only under app/android/kotlin/). Until the link exists component_gn_graph.py attributes that module's sources to app.android and the architecture lane reports an undeclared edge on every target that lists them. For any other finding run \`python3 taffy-core/app/android/tools/kotlin_mounts.py --report\` to see which module gained or lost a blocker, then move it between MOUNTED_MODULES and BLOCKED_MODULES in that file." \
    python3 taffy-core/app/android/tools/kotlin_mounts.py --verify
  lane_run mount "The generated Kotlin source list is stale. Regenerate with \`python3 taffy-core/app/android/tools/generate_kotlin_sources.py --write\` and commit the result." \
    python3 taffy-core/app/android/tools/generate_kotlin_sources.py --check
  lane_run mount "The product-manifest checker does not reject broken foreground-service or backup fixtures, so its verdict proves nothing." \
    python3 taffy-core/app/android/tools/check_product_manifest.py --self-test
  lane_run mount "The Gradle and shipping Android manifests disagree about task continuation or backup policy. Keep the TaffyGo Jinja overlay, product target, Gradle manifest, and both exclusion resources exact." \
    python3 taffy-core/app/android/tools/check_product_manifest.py
  return 0
}

lane_run_icons() {
  step "icons: Android launcher icons and brand marks (taffy-core/ui/android/)"
  lane_skip_unready icons || return 0
  lane_run icons "A committed icon, its checksum and the brand master no longer agree. Re-derive with \`python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --generate\` on a host carrying the cwebp version TOOLCHAIN.md pins, and commit the result." \
    python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --check
  return 0
}

# The command chooses the profile itself, from the last one this host generated,
# so the lane names no profile: a builder that last built for a phone should be
# asked about the graph it actually has.
# Two calls rather than one, because they are two different claims: that the
# committed listing is publishable, and that the checkers which say so still
# reject their broken fixtures. A checker that has quietly stopped failing is
# the failure mode a self-test exists for.
# The first goes through lane_run_soft because the listing checker deliberately
# exits 0 on findings a person must decide about rather than fix. Nothing here
# sells anything any more (decision 0200), so the example that used to be given
# — a price rewritten under a base plan Play has already activated — no longer
# exists; the soft path stays because the shape it exists for does.
lane_run_play() {
  step "play: store listing, products, track ladder and publishing checkers"
  lane_skip_unready play || return 0
  lane_run_soft play "The committed Play listing or track ladder is not publishable. Play refuses an over-long field, docs/voice-and-naming.md binds the copy, and the generated metadata under tools/play.d/metadata/ must match store.toml; the findings above name which." \
    ./tools/play check
  lane_run play "A ./tools/play checker no longer rejects its broken fixtures, so a green lane above proves nothing." \
    ./tools/play self-test
  return 0
}

# Three claims, so three calls: the exporter still refuses every fixture built
# to fail it; this working tree exports; and the export verifies. The build
# takes --worktree so the change under review is what gets checked rather than
# HEAD, and a --worktree receipt can never be synced. The keystore and password
# probes need the signing identity, which most hosts do not hold; there the
# lane names them as skipped instead of passing over them.
lane_run_public() {
  step "public: what an export of this working tree would publish (tools/export-public)"
  lane_skip_unready public || return 0
  lane_run public "The exporter no longer refuses one of its own adversarial fixtures, so its verdict below would prove nothing. Reproduce with ./tools/export-public self-test." \
    ./tools/export-public self-test
  lane_run public "The README screens check no longer refuses one of its fixtures, so its verdict below would prove nothing." \
    python3 tools/export-public.d/lib/overlay_screens.py self-test
  lane_run public "A README screen is not the one tools/export-public.d/overlay-screens.txt records, or comes from an older capture than the website's copy of the same screen. The finding above names the file and the command that remakes it." \
    python3 tools/export-public.d/lib/overlay_screens.py check
  local out="$TMP/public-export" state=0
  lane_run public "The working tree cannot be exported as it stands; the refusal above names the table row or the overlay file." \
    ./tools/export-public build --worktree --out "$out"
  [ -d "$out" ] || return 0
  "$TAFFY_ROOT/tools/export-public" key-state >/dev/null 2>&1 || state=$?
  case "$state" in
    0) lane_run public "The export would publish something it must not. Each finding above names the file and the lettered check in ./tools/export-public --help." \
         ./tools/export-public verify "$out" ;;
    3) lane_skip public "the keystore and password probes (no signing identity on this host)" \
         "They run wherever .taffy/android-signing.env or the TAFFY_ANDROID_* variables resolve."
       lane_run public "The export would publish something it must not. Each finding above names the file and the lettered check in ./tools/export-public --help." \
         ./tools/export-public verify "$out" --no-key-probe ;;
    *) hard "public: a signing identity is configured and does not resolve — run ./tools/export-public key-state" ;;
  esac
  return 0
}

lane_run_orphans() {
  step "orphans: targets under //taffy that nothing builds"
  lane_skip_unready orphans || return 0
  lane_run orphans "A target under //taffy is compiled into no binary, so its tests can neither fail nor pass. Wire it into an entry point, or record it in one of the registers in tools/chromium/orphan-targets with a written reason." \
    ./tools/chromium/orphan-targets
  return 0
}
