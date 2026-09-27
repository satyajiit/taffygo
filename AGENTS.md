# TaffyGo engineering guide

This guide is for anyone changing TaffyGo, people and coding assistants alike;
`CLAUDE.md` imports it. [README.md](README.md) says what the product is, and
[CONTRIBUTING.md](CONTRIBUTING.md) covers sign-off and pull requests.

TaffyGo is a Chromium browser for Android with one built-in assistant, Taffy:
a complete browser with ad and tracker blocking, plus help with the page in
front of you and tasks you hand over, always under visible control. TaffyGo
runs no server and has no account. Taffy talks only to the AI provider a
person configures, and the browser, never the assistant's core, makes that
request.

## Know this before changing anything

- **There is one product root.** `taffy-core/` is mounted into a Chromium
  checkout at `src/taffy`, so its GN labels read `//taffy/...`.
  `./tools/chromium/sync` makes the mount, and editing `taffy-core/` needs no
  re-sync because the mount is one symlink. No upstream source is copied into
  `taffy-core/`; an edit to a Chromium-owned file is a numbered patch under
  `chromium/patches/`, described before it is written.
- **Compose owns the Android UI.** Its sources live under
  `taffy-core/ui/android/`. Gradle is the fast loop and GN compiles the same
  sources for the product. Compose owns UI and trusted platform interactions;
  portable task, policy and routing behaviour belongs to Rust. There is no
  Kotlin task runtime.
- **Rust runs in a sandboxed core service.** Small crates live under
  `taffy-core/components/*/core/rust/`. Cargo is the fast host loop and GN is
  authoritative for what ships. No TaffyGo workspace crate is linked into the
  browser process.
- **The core proposes and the browser performs.** The core service receives
  generated values only: never a profile path, SQL handle, URL loader, cookie
  jar, credential or provider key. It proposes typed effects. The browser owns
  storage, network, tabs, credentials and tool-process launch, and it performs
  each effect.
- **Five generated contracts join the parts**, under `taffy-core/contracts/`:
  BIP (renderer page semantics), Browsing (browsing-surface records), Core API
  (UI to browser), Core Service (browser to core) and Tool Runtime (broker to
  workers). Each owns one schema, one generator, golden fixtures and
  compatibility fixtures. Generated bindings are never edited by hand.
- **The model catalog is compiled in.** The embedded baseline under
  `taffy-core/components/intelligence/core/rust/model-router/catalog/` is the
  whole catalog. `MergedCatalog::build` still takes a remote overlay argument,
  and every production call passes `None`. Do not read that as "the
  catalog-fetch types are dead": `CatalogFetch`, `CatalogFetchDisposition` and
  `catalog_fetch` carry the provider listing fetch
  (`taffy-core/browser/model/profile_provider_listing_fetcher.cc` and
  `core-runtime/src/provider_listing.rs`), which is how a dynamically listed
  provider such as OpenRouter publishes its models. Deleting them by name
  would take that feature with them.
- **Design records are not in this repository.** Comments cite them as
  "decision NNNN" or "OD-nnn". The maintainer keeps them in a private working
  tree that this repository is exported from. If a comment's reasoning is not
  clear from the code, ask in Discussions rather than guessing.

## The three rules

1. **One product line.** TaffyGo is 1.0 and the updates that follow. Never
   introduce edition, tier or staged-release language. Internal build order is
   written as milestones M0 to M8 and spikes SP-nn, and neither is ever a
   release.
2. **One assistant.** Taffy is the only AI actor. Personalisation is
   configuration (a Personality setting, a skill), never another assistant.
3. **Plain language.** Everything a person reads uses the product's own words:
   task, workspace, sources, Library, Memory, Taffy's tabs, Take over, Hand
   back. `./tools/check docs` enforces the banned list in `BANNED_RULES`,
   `tools/lib/docs_lint.py`.

## Code maps (optional)

If [ripwire](https://github.com/redhat-et/ripwire) is on `PATH`, map the tree
before a blind grep or a whole-file read:

```bash
ripwire . --for="<the change, in words>"     # ranked, quality-annotated signatures
ripwire . --pack-task="<task>"               # ranking, bodies, callers and tests in one call
ripwire . --callers=SYM                      # who calls X
ripwire . --impact=SYM --uses=SYM            # blast radius and every read/write/import site
ripwire . --expand=SYM                       # one body plus callee signatures
```

It does not parse Kotlin, GN, Mojo IDL, XML or SQL, so reach the Compose tree
and the build files with `rg`. Counts it marks `counts_floor` are floors, and
a zero means none found, not none exists.

## Repository layout

| Path | What it is |
|---|---|
| `TOOLCHAIN.md` | The only human-readable index of version pins. Every pin has exactly one machine owner file, and the `pins` lane reads that file as truth |
| `LICENSE`, `NOTICE`, `TRADEMARKS.md` | The source is MPL-2.0. `LICENSE` is the licence text unaltered; `NOTICE` says what it covers and where the third-party licences are; `TRADEMARKS.md` reserves the name, the marks, the Taffy character and the artwork by exact path. Every first-party source file carries the copyright line and the MPL Exhibit A notice, enforced by `tools/lib/license_headers.py` in the `files` lane |
| `tools/` | The command suite (bash 3.2 compatible) and its shared `lib/`; `tools/check.d/*.tsv` are the policy tables the checks read |
| `chromium/` | The fork integration: the `REVISION` pin, `args/` GN profiles and the numbered patch queue |
| `taffy-core/` | The whole product root: Android assembly, browser, renderer and utility seams, feature components, sandboxed services, generated contracts, Compose UI, resources, third-party code and `build/components.toml` |
| `taffy-core/contracts/` | The five generated seams |
| `taffy-core/ui/android/` | Every Compose surface plus the Android-only adapters |
| `build-logic/` | Gradle convention plugins; shared Android build configuration lives here and nowhere else |
| `website/` | taffygo.com: Next.js static export, Tailwind CSS 4, React 19 |
| `test-fixtures/` | `web/` (page corpus, including seeded-secret canaries) and `tasks/` (task scenarios) |
| `brand/` | The committed marks, reserved by `TRADEMARKS.md` |

## Commands

The tool suite is the single entry point. Every command takes `--help`, prints
what it resolved, and fails with a remediation message.

```bash
./tools/doctor                                     # read-only audit: host, pins, toolchains, runnable lanes
./tools/bootstrap --profile docs|android|chromium  # idempotent environment setup
./tools/check docs                                 # Markdown: vocabulary, status labels, links
./tools/check fast                                 # every lane this host can run
./tools/check fast --only rust,contracts           # some lanes; ./tools/check --help lists them
```

A lane skips when its toolchain is absent and never reports success for
something it did not run. `./tools/doctor` reports the same verdicts from the
same predicates.

Per component, from the repository root:

- **Rust:** `cargo fmt --all -- --check`, then
  `cargo clippy --workspace --all-targets --all-features -- -D warnings`, then
  `cargo test --workspace --locked`.
- **Contracts:** each generator runs `--check`, `--self-test` and `--verify`,
  and BIP also runs `--mojom`. The BIP flags are mutually exclusive, so that
  is four commands:
  `python3 taffy-core/contracts/bip/codegen/generate.py --check`, then the
  other three. Regenerate with `--write`, then `cargo check -p bip-types`. The
  `contracts` lane then type-checks every contract's generated TypeScript with
  `python3 taffy-core/contracts/codegen/typescript_typecheck.py`, which reports
  "not run" and exits 0 on a host with no `tsc`.
- **Android UI:** `./gradlew --no-daemon lintDebug testDebugUnitTest`, and
  `assembleDebug`. `connectedDebugAndroidTest` needs a device or emulator.
- **Website:** `pnpm --dir website lint`, then `typecheck`, then `test`, one
  script per invocation, because trailing words are passed to the script as
  arguments. Build with `./tools/website build`.
- **Chromium** (x86-64 Linux only): `./tools/chromium/sync`, then
  `./tools/chromium/build --profile dev-arm64`, then `./tools/chromium/run`.
  Use `dev-x64` for an emulator and `dev-arm64` for a phone; `run` installs on
  whatever `adb` has attached, so the profile must match the device's ABI.
  Build `taffy_unittests` and run `./tools/chromium/test --profile dev-arm64
  taffy_unittests` for the native suite. The store artifact is
  `./tools/chromium/build --profile release-arm64 taffy_public_bundle`,
  because Play takes a bundle and both dev profiles set `incremental_install`,
  which is APK-only.

### Running one test

```bash
cargo test -p policy-engine                  # one crate; the storage crate is -p taffy-storage
cargo test -p task-engine reducer            # one module or test, by name filter
./gradlew :core:model:test --tests '*TaskStateDisplayTest'   # :core:model is the only pure-JVM
                                             # module; filter on :test, not testDebugUnitTest
pnpm --dir website test tests/llms.test.ts   # one vitest file
```

### Commands that do not do what their name suggests

Do not put these in a script and read a zero exit as a pass.

- `./tools/check` with no lane prints usage and checks nothing.
- `./tools/check device` needs a Chromium build and exactly one attached
  device of the right ABI, and it refuses rather than guesses.
- `./tools/benchmark --suite <name>` exits 1 on any host that cannot drive a
  measured run, because it refuses to report a run it did not measure.
  `--list` exits 0 and skips the corpus preflight.

## What no check catches

A green `./tools/check fast` does not cover these. Each one is a change that
looks finished and breaks later or somewhere else.

**Generated code and contracts**

- **Every `mod generated;` over a generated Rust table keeps its
  `#[rustfmt::skip]`.** Three are plain (`bip-types/src/lib.rs`,
  `asset-plane/src/catalog/mod.rs`, `tool-entrypoints/src/lib.rs`) and the
  contract crates mount theirs through `#[path]` under another name. Without
  the attribute `cargo fmt --all` rewraps the output, the generator's
  `--check` fails on a tree nobody edited, and the failure names the
  generator rather than the formatter. `grep -rn 'rustfmt::skip' taffy-core`
  is the honest count.
- **Adding an enumeration to the BIP schema is not finished after `--write`.**
  `bip-types/src/version.rs` carries a hand-maintained `closed_enums!`
  registry, compared against the generated `GENERATED_ENUMERATIONS`, so
  `generate.py --check` passes while `cargo test` fails. A new schema
  definition also needs a case in the hand-written match in
  `bip-types/tests/golden_round_trip.rs`.
- **A next-minor compatibility fixture goes stale on a minor bump.** The
  verdict compares only the major version, so a fixture meant to be "a message
  from the next minor" keeps passing once the contract has moved past it.
  Fixtures whose manifest entry carries `declares_next_minor` are checked to
  sit exactly one minor ahead (`_assert_next_minor` in
  `taffy-core/contracts/codegen/compat_corpus.py`, `at_next_minor` for BIP). A
  new version fixture that omits the key is unchecked and looks exactly like
  one that carries it.
- **The cxx bridge under `taffy-core/services/core/` is not in the Cargo
  workspace, so no host command compiles it.** Its `.rs` files are listed by
  hand in that directory's `BUILD.gn` under
  `rust_static_library("service_bridge")`, not in the generated
  `crate_sources.gni` beside it. Adding a field to a contract record is
  therefore finished on the host and unfinished in the product: every Cargo
  crate compiles, and the bridge sites fail with `E0063 missing field` at the
  first `./tools/chromium/build`. A contract change is done when the Chromium
  build has seen it. `check_command_dispatch_coverage.py` in the `rust-graph`
  lane catches one shape statically: a command kind that falls through
  `RustCore::Submit` to a projection that refuses it.
- **A field added to a Core API command record does not cross the pipe until
  the shell's hand-written mapping sets it.** The shell converts each generated
  record into its mojom Java struct by hand (`CoreApiEndpointMapping.kt` and
  its neighbours under
  `taffy-core/app/android/shell/java/src/org/chromium/taffy/host/`). A field
  the mapping never assigns keeps the Java default, null for an array, and the
  generated encoder throws on the first send. A new command field needs a
  mapping that sets it and a test that serializes the result, in
  `taffy_shell_junit_tests`, which is not a fast lane.

**Build graph**

- **Adding an Android module starts in `taffy-core/build/components.toml`**:
  its component, exact sources and direct dependencies, then the regenerated
  GN and Gradle projections. Then, on every host that runs the checks,
  `python3 taffy-core/app/android/tools/kotlin_mounts.py --mount`. Until that
  gitignored link exists, `component_gn_graph.py` reports an undeclared
  `app.android` edge and the `mount` lane says `no mount at ...; run --mount`.
  Neither is a manifest defect, and adding `app.android` to any `direct_deps`
  is a layer inversion the graph check rightly refuses.
- **Adding a lane takes four edits across three files, and two fail
  silently.** A `lane_ready_*` predicate and its case in the `lane_ready`
  dispatch (`tools/lib/lane_readiness.sh`), a `lane_run_*` function
  (`tools/lib/lane_runners.sh`), and an entry in `TAFFY_FAST_LANES` in
  `tools/lib/lanes.sh`, plus `TAFFY_COMPONENT_LANES` if `./tools/doctor`
  should report it. Miss `TAFFY_FAST_LANES` and the lane never runs; miss the
  dispatch case and it skips everywhere with "no readiness predicate".
- **A `BUILD.gn` under `taffy-core/` that nothing names is never loaded**, so
  its targets do not exist and nothing says so.
  `taffy-core/build/tools/check_build_reachability.py` closes that in the
  `rust-graph` and `chromium` lanes. A build file that is not wired up yet
  goes in its `KNOWN_UNREACHABLE` register with a reason and an owner.
- **A GN source list that names a missing file fails at the very end of a
  build, as a `.stamp` with no rule to make it**, and the message never names
  the list. `taffy-core/build/tools/check_gn_source_paths.py` checks it
  statically in the `rust-graph` lane. It reads lists named `sources`,
  `inputs`, `public`, `*_sources` and `*_inputs`, resolves a relative entry
  against the file that defines the list, and cannot see a path built by
  string concatenation. Until `./tools/chromium/sync` has filled the
  gitignored `taffy-core/third_party/cpython/src/`, it skips that tree and
  prints how many listed sources it did not read.
- **Gradle's `checkFileDiscipline` honours no exemption table**, although its
  failure message names one. Only `tools/lib/file_discipline.py` reads
  `tools/check.d/file-size-exemptions.tsv`, so a large Kotlin file can pass the
  fast lane and still fail a Gradle build.
- **A signing key is an input of every APK action, and the component graph
  reads inputs as source ownership.** The checker excludes exactly the path
  the build was told to sign with, read from `gn args`. Any other non-source
  `//taffy` input added to a packaging action lands as an ownership edge and
  is not excluded. `./tools/check fast` cannot see this; only a configured GN
  graph can.
- **A device test's debug-only fixture is compiled by GN only if
  `generate_kotlin_sources.py` names it** in `_ANDROID_TEST_FIXTURES`. Gradle
  merges `src/debug/kotlin` into androidTest, so the Gradle build passes.
- **Two browser-test binaries exist.** `taffy_browsertests` links
  `//taffy/browser:browser_tests` and `//taffy/test:browser_tests`;
  `taffy_profile_browsertests`, declared in `//chrome/test`, is the only one
  that links `//taffy/test/recovery:core_service_recovery_browser_tests`. A
  green build of a target that does not contain your file looks exactly like
  a green build of one that does. `ls -l` on the expected `.o` under
  `out/<profile>/obj/taffy/` against your save time answers it; `gn path`
  explains it.

**Reading a build or a test run**

- **Trust the build log over the exit notice.** Every `out/<profile>/bin/run_*`
  script installs whatever APK is already in the output directory, so a failed
  test-APK build still yields "N tests ran, N passed" for sources that never
  compiled. `cmd > log 2>&1; echo "exit=$?" >> log` records the status in the
  log, and the compound command then exits with the `echo`'s zero. Grep the
  log for `FAILED:` before trusting any run, and compare the test names it
  prints with the tree.
- **A green build describes the tree ninja read when it started.** Do not edit
  sources under a running build; if you did, check the `.o` timestamp and run
  one more pass.
- **The fast lane compiles no C++, and the profiles disagree about missing
  includes.** `diag-sanitizer-x64` is a modules build and refuses a
  declaration that leaks in through a neighbouring header; the dev profiles
  accept it. A green `dev-arm64` build says nothing about a file's includes.
- **A sanitizer suite needs a longer launcher timeout.** Children pass their
  tests, then spend longer than the default 90 s writing a LeakSanitizer
  report, get killed, and every test in the batch is recorded SKIPPED while
  `[  FAILED  ]` stays at zero. Run it as:

  ```bash
  ./tools/chromium/test --profile diag-sanitizer-x64 taffy_unittests -- \
      --test-launcher-jobs=8 --test-launcher-timeout=600000
  ```

  Grep for `no test result for` and `Timed out waiting` and read the final
  summary before believing a count.

**Runtime behaviour**

- **`DUMP_WILL_BE_NOTREACHED` is fatal in every build this project makes**,
  because `GetDumpSeverity()` returns fatal unless `OFFICIAL_BUILD` is set.
  `PrefService::GetUserPrefValue` documents a null for a stored value of the
  wrong type and instead kills the browser; read `Preference::GetValue()`
  and detect corruption with a sentinel, as
  `ReadBackupRestoreProfileReservations` in
  `taffy-core/browser/backup_restore_profile_registry.cc` does. Every
  `WebAXObject::Serialize` call under `taffy-core/renderer/` must guard
  `IsIncludedInTree()` as well as `IsDetached()`: an object resolved from a
  DOM fact (a form's controls, a node id the browser named) can be one the
  accessibility tree leaves out, and serializing it aborts the renderer.
- **Raising the durable journal's schema version means revisiting every
  migration entry.** Each entry in
  `taffy-core/components/storage/core/schema/core_service_journal.json` goes
  from its source version to the head in a single hop, so a head that adds a
  column must add it on every path. The generator builds each historical
  schema in an in-memory SQLite database, applies the migration and refuses
  to write unless the result equals a fresh head. The migration ladder is
  inside the checksummed identity, and a test fixture for an old version
  comes from `storage_test::CreateHistoricalSchema`, never from a head
  database with a rewritten ledger row. People hold data from 1.0 onward, so
  every schema change ships with a migration.
- **An unpublished state change is invisible rather than wrong.** A surface
  reads `CoreStatus`, which reaches it only when a bridge answers through
  `response_after_change` in `taffy-core/services/core/service_bridge_status.rs`.
  A bridge that changes core state and returns a bare `response(...)`
  compiles, passes every lane, and leaves every screen on the old snapshot.
  The second form is a reader that cached before the writer ran:
  `ProfilePythonLibrary`'s constructor calls `Refresh()`, so an install nobody
  announces leaves it holding "absent" for the life of the process, and
  `StartPageGate` then keeps the start page closed.
  `ProfileAssetPlane::OnSeededAndScanned` runs the installed-set observer
  before it answers the scan for that reason.
- **A failed state publication withdraws every effect.** `withdraw_effects`
  destructures `BridgeResponse` exhaustively and clears all seven effect
  vectors, and `check_effect_withdrawal.py` in the `rust-graph` lane checks
  both failure paths. Adding an effect vector without deciding this fails
  both the host check and the GN compile.
- **The core's provider plane is empty in every new generation.** Saved
  credentials and custom providers live in the browser's register, and
  `CoreServiceManager::ReplayProviderSetup` carries them into each new utility
  process. A provider write that skips the browser's register works until the
  next start, then reads connected while every request is refused. Only a
  second generation shows it, which means a phone closed and reopened.
- **A `kReady` snapshot with no payload destroys the state before it.** The
  Android endpoint treats ready-with-nothing as a protocol violation and
  replaces its state with unavailable, and a ready core then goes quiet.
  Readiness is announced only by the snapshot that carries the state; see
  `ProfileCoreApiFacade::OnCoreAvailabilityChanged`.
- **An effect identifier must name what the effect is about, never its
  position in a plan, and its lifetime must match the work's.** The browser
  journals an effect id before dispatch and refuses one it already holds
  (`CommitIntent` in
  `taffy-core/components/storage/browser/core_storage_transactions.cc`), so a
  positional id collides silently. Consequential work a durable task can
  restore keeps one identity across utility-process generations; live and
  retryable attempts include the browser session, generation and attempt;
  asset identity hashes causation, asset id, revision, operation kind and the
  completed-attempt ordinal. `core_effect_journal` is keyed by `effect_id`
  alone, and `OnIntentCommitted` must keep its `terminal_claimed` guard.
- **Asking the core for an artifact it is already fetching costs that artifact
  an attempt**, and a backoff nothing wakes. That is why
  `RequiredPartsInstaller` waits for a hold before it asks.
- **The browser binds intent, input shape and live authority before a named
  value becomes renderer bytes.** `TaskActionInputMatchesOperationAndCanonical`,
  `PageInputForTaskAction`, `NamedInputMatchesAction` and
  `ResolveActionInput` are independent checks. A protocol support list
  describes what is implemented, never what is authorised. Only the Chromium
  build with `taffy_unittests` and `taffy_browsertests` exercises this path.

**Files and fixtures**

- **Shipping binary resources are discovered from disk.** Every binary under
  an Android `src/main/res/` comes from a first-party master or is named by a
  provenance record, and the `files` lane sweeps the disk rather than a
  register.
- **Two files in different modules may declare the same fully qualified
  type**, and only the merged APK has both, with classpath order picking one.
  `tools/lib/type_per_file.py` catches it for Kotlin and Java and
  `tools/lib/cpp_type_collisions.py` for namespace-level C++ types. Extend the
  latter's self-test fixture before teaching it a new declaration shape.
- **Fixture corpora enforce their manifests in both directions.** A file no
  manifest names is a finding, and so is a manifest entry with no file. A page
  fixture holds no absolute or protocol-relative URL; use
  `data-taffy-origin` and `data-taffy-path`. In `test-fixtures/web/` a canary
  token appears in exactly the fixtures its `carried_by` names; in
  `test-fixtures/tasks/` it may appear only in `manifest.json`. A page-corpus
  version bump forces a task-corpus bump.
- **The `secrets` lane skips its own pattern files** (`tools/check`,
  `tools/check.d/`, `tools/lib/`). It refuses tracked key material by
  extension (`*.jks`, `*.bks`, `*.keystore`, `*.p12`, `*.pfx`, `*.pkcs12`) and a
  literal signing password anywhere it scans.
- **The third-party notices check reads trackedness only.**
  `tools/lib/third_party_notices.py` refuses a `README.chromium` whose
  `License File:` names an untracked path. Whether that text is the right
  licence is a review question, and a component marked `Shipped: no` whose
  bytes ship produces a credits page that is wrong in the APK and green here.

## Toolchain and version pins

`TOOLCHAIN.md` is the only human-readable index of versions. Each version has
exactly one machine owner file (`rust-toolchain.toml`,
`gradle/libs.versions.toml`, `package.json`, `website/package.json`,
`chromium/REVISION` and so on). Bump one component per change, updating the
owner file and its `TOOLCHAIN.md` row together; the `pins` lane checks both
directions. Rows marked `TO-VERIFY` belong to components that do not exist
yet, so do not invent values for them. Inside the Chromium build, Chromium's
bundled toolchain overrides every pin.

Key pins: Rust 1.96.0, Node 24.x, pnpm 11.8.0, JDK 21, AGP 9.3.1, Kotlin
2.4.10, compile and target SDK 37, minSdk 29, Chromium 152.0.7977.42.
Nothing unpinned is installed at run time.

Release builds need a signing key. `./tools/chromium/build` reads
`TAFFY_ANDROID_KEYSTORE`, `TAFFY_ANDROID_KEY_ALIAS` and
`TAFFY_ANDROID_KEYSTORE_PASSWORD`, or an untracked `.taffy/android-signing.env`
with mode 600. A dev build that finds none signs with Chromium's debug key and
says so; a `release-arm64` `taffy_public_apk` or `taffy_public_bundle`
refuses. A fork signs with its own key. Never commit a key or its password.

## Code style

- **EditorConfig:** spaces everywhere; indent 2 by default, 4 for Rust,
  Kotlin and Java, 2 for C++ and Mojo; LF endings; final newline.
- **Rust** (workspace lints in `Cargo.toml`): clippy `all` and `pedantic` at
  warn; `unwrap_used`, `expect_used`, `panic` and `indexing_slicing` deny. A
  panic in the core service loses every task in that profile generation.
  `rustfmt.toml` sets width 100. Keep the dependency tree near zero; test-only
  machinery goes in dev-dependencies.
- **Rust tests:** an inline `#[cfg(test)] mod tests` over 300 lines moves to a
  sibling `tests.rs`, and a sibling test file splits by subject before 360
  lines, declared from a `tests.rs` that holds only `mod` lines.
  `tools/lib/code_lines.py` does not count inline test blocks and does count
  sibling files, so the move is what makes the cap apply.
  `python3 tools/lib/file_discipline.py --inline-tests` lists offenders and
  never fails. Follow the shape `status.rs`, `status/tests.rs`,
  `status/tests/model_budget.rs`.
- **Kotlin:** one public type per file, named for it (enforced). Shared build
  configuration lives only in `build-logic` convention plugins, and a
  `:feature:*` build script is about five lines. Do not apply
  `org.jetbrains.kotlin.android`: Kotlin is built into AGP 9 and applying it
  is a configuration error. Screens follow one shape: an immutable `UiState`,
  a sealed `Intent`, a view model exposing a `StateFlow`, and a pure reducer
  a host test can call directly.
- **Chromium code** under `taffy-core/` follows upstream `OWNERS`, `DEPS` and
  `//build/config` conventions. Cargo and GN name the same physical Rust
  sources.
- **Modularity is checked in the build:** a soft line cap on every
  non-generated source file, one public type per Kotlin or Java file, the
  exact component graph from `taffy-core/build/components.toml`, and the
  separation of api and internal packages.
- **Strings:** every user-visible string is externalised, checked on any host
  by `python3 taffy-core/resources/catalog/tools/check_strings.py`.

## Documentation conventions

- **Status labels.** READMEs mark claims with one of four labels: `[Current]`
  (verifiable today), `[Decided]` (the planning baseline), `[Proposed]`
  (waiting on evidence) and `[Open (OD-nnn)]` (an unresolved question in the
  maintainer's register). `./tools/check docs` refuses any other label.
- **Each subject has one authoritative document**, and numbers live in one
  place: version pins in `TOOLCHAIN.md`. Other documents reference them.
- When you change a convention a directory's README describes, update that
  README. Nearly every directory has one stating its authority boundary.

`./tools/check docs` walks every Markdown file in the tree. A fenced code block
exempts banned vocabulary and status labels but not link checks, so snippets
use real paths. A bare path in prose or in a code block is not a link, so
nothing checks that it exists. Anchors are checked against GitHub-style slugs,
so renaming a heading breaks every inbound `#anchor`. An argued vocabulary
exception goes in `tools/check.d/allow-terms.tsv` as a tab-separated row of at
least three fields; a space-separated row is skipped silently.

## Testing

- `./tools/check fast` is the check, and the person making the change runs it.
  No hosted service runs it for you.
- **Rust:** unit and integration tests per crate, `proptest` for properties
  such as replay and process-death recovery, golden files for the export
  contract, and a seeded-secret suite that drives every canary in
  `test-fixtures/web/manifest.json` through every projection and serializer.
- **Contracts:** golden fixtures prove valid shapes, and compatibility
  fixtures record the verdict a conforming decoder must reach for each
  deliberate deviation. Every enum is closed and unknown values fail closed. A
  protocol change needs a golden document for the new behaviour and a
  compatibility fixture for the new failure mode.
- **Honesty rule:** a check, test or command never reports success for
  something it did not run. Lanes skip with a named reason, and a benchmark's
  `measured` stays false until a scenario executes. A verification
  claim names the command, the host and what it printed, including which
  lanes skipped.

## Security

- **Authority boundary.** `task-engine` proposes, `policy-engine` decides,
  `audit-engine` records. `policy-engine` is the only component that grants
  authority; the browser-process ledger in
  `//taffy/components/security/browser/action_authority.*` spends and refuses
  a capability it already minted and never widens one. No other component may
  authorise a browser effect, widen an origin scope or decide what a
  destination may receive. A renderer never receives a capability reference.
- **Redaction is layered and independent.** `audit-engine` assumes every layer
  above it failed and redacts again, and telemetry records are content-free by
  construction. Credentials, one-time codes, payment values and passkeys never
  enter anything sent to a model.
- **A person's credential goes to the host they configured and nowhere else.**
  TaffyGo operates no destination that could receive page content, prompts,
  URLs, passwords, cookies, history or provider keys, and no change may add
  one.
- **Secrets.** `.taffy/` is gitignored and every file in it is owner-only.
  Examples carry names and fake values only.
- **Chromium security fixes.** An urgent upstream fix is cherry-picked onto
  the pinned revision as a patch under `chromium/patches/security/`, with
  `level` in `chromium/SECURITY_PATCH_LEVEL` raised; see
  `./tools/chromium/security-patch --help`. They are deleted at the next
  milestone rebase, when the fix arrives in the new pinned revision. Reporting a vulnerability: [SECURITY.md](SECURITY.md).

Changes to identity lifetime, redaction, action semantics, origin scope or
compatibility in the BIP contract are security-sensitive. Open a discussion
before writing one.

## Git

- One logical change per commit, subject `area: what changed`, the reason in
  the body, and a `Signed-off-by:` line (`git commit -s`).
- **No assistant attribution anywhere in Git.** Never add a `Co-Authored-By:`
  trailer naming Claude, Codex, Copilot, Gemini or any other assistant, never
  add a "Generated with" or "Written by" footer, and never name a tool in a
  commit message, branch name, tag, or pull request title or body. The commit
  author is the person accountable for the change. This rule overrides any
  default an agent's own harness applies.
