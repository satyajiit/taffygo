# `tools/`

The repository-owned command suite (decision
0012). Every
command prints resolved state, fails with a remediation message naming the
exact next step, and supports `--help`.

| Command | What it does |
|---|---|
| [`doctor`](doctor) | Read-only audit: host, disk, pins, checkout, `//taffy` mount, caches, toolchains, devices, secret *names*, and which `check fast` lanes this host can run. `--json` for machines. |
| [`bootstrap`](bootstrap) | Install/verify one environment: `--profile docs\|android\|chromium`, `--only <steps>`. Idempotent; `--dry-run` claims nothing it did not do. |
| [`check`](check) | The repository gates: `docs`, `fast` (with `--only`/`--skip`), `device` (scaffolded, never run — it needs the Chromium builder and an attached device). |
| [`website`](website) | The landing page: `dev`, `build` (static export), `check`, `audit` (Lighthouse budget). |
| [`assets`](assets) | The delivery plane's artifacts: `plan` (what a row promises) and `verify` (whether every `published` row is true). It no longer publishes: TaffyGo operates no origin (decision 0200) and the parts ship inside the app, so there is nothing to upload to and no credential to hold. |
| [`play`](play) | Google Play (decision 0142): `check` (the committed listing, ladder, metadata projection, products file and generated request bodies, offline), `doctor` (pin, checksum, and one read-only call that says what the key may do; `--products-probe` asks the separate question of whether it may manage store presence), `preflight`, `sync-listing`, `sync-images`, `products`, `upload`, `promote`, `self-test`, and `-- <args>` for the whole of gplay. It runs [gplay](https://github.com/tamtom/play-console-cli) pinned by `tools/pins/gplay` and verified against the release checksum for this host, with its self-updater and its GitHub-star prompt switched off. Reads a service-account credential at run time and uploads; it never signs, because Play holds the app signing key. `products` is the one subcommand that writes to the monetization API, which has no edit to throw away and no dry run: its default mode is a read-only read-back, `--apply` creates drafts, and `--activate` walks a door that cannot be walked back. |
| [`github-release`](github-release) | A GitHub release, cut by hand (decision 0252). `prepare` checks the release-arm64 APK and signed bundle against the configured key's certificate and the version-code floor in [`github-release.d/version-floor`](github-release.d/version-floor), then writes the APK, `SHA256SUMS` and the release notes to a directory outside the repository. `draft` creates the release as a draft with `gh`; `publish` makes a draft public and refuses without `--yes`. `self-test` builds throwaway signed APKs and bundles and runs all three against a stand-in for `gh`. It is not a lane of `check fast`. |
| [`benchmark`](benchmark) | The Task Benchmark: `--suite production\|affected`, `--list`. Verifies its inputs and refuses to report a run it did not measure. |
| [`evidence`](evidence) | Assemble and verify the revision-bound index of hashed run evidence and M0–M8 exit reviews selected for one candidate. Incomplete indexes say why and cannot be promoted. |
| `export-public` | The only way a byte of this repository reaches the public one (decision 0251). `build` exports a commit less the rows of `export-public.d/exclude.tsv`, puts the public overlay in place, turns each Markdown link into a withheld path into its text, and writes a receipt beside the output. It refuses a generated file that carries such a link, since a rewrite would fail that file's generator `--check` in the public tree. `verify` checks the exported bytes: the signing keystore and its password (resolved the way `chromium/build` resolves them, never printed), key files, operator identifiers, withheld paths, links and the receipt's tree; `--full` also runs `check docs` and `check fast` inside a copy. `sync` stages a verified export in a clone of the public repository, and refuses while that clone holds a public commit not yet imported here. `key-state` says whether a signing identity resolves on this host, and `self-test` shows every refusal a tree built to fail it. It never pushes. The export withholds this command and its directory. |
| [`chromium/sync`](chromium/sync) | Checkout at exactly `chromium/REVISION`, patch queue onto `taffy/patched`, `taffy-core/` mount, hooks. |
| [`chromium/build`](chromium/build) | `gn gen` from a committed args profile + `autoninja`. `--baseline` builds upstream `chrome_public_apk` as the clean-host proof. `--jobs N` caps concurrent steps; a `release-arm64` build at one step per core outgrew 62 GB of memory and was killed. |
| [`chromium/run`](chromium/run) | Incremental install + launch on the connected device/emulator. |
| [`chromium/test`](chromium/test) | Taffy suites; `--upstream` adds the affected upstream suites. |
| [`chromium/export-patches`](chromium/export-patches) | Regenerate `chromium/patches/` from `taffy/patched` commits — the only writer of the queue. |
| [`chromium/import-patches`](chromium/import-patches) | Re-apply the queue onto the synced checkout; `--onto <commit>` is the rebase rehearsal on a throwaway branch. |
| [`chromium/orphan-targets`](chromium/orphan-targets) | Every target under `//taffy` that nothing builds, read from a generated graph. Also the `orphans` lane below. |

## The `check fast` lanes

Six repository-wide lanes, then one lane per component with host-runnable
tests, then `orphans`, which reads the real GN graph and therefore runs
only where a build has happened. `play` and `public` close the list and are
the odd ones: what `play` owns is the text and configuration behind a store
upload, and what `public` owns is the tree a publication of the source would
carry. A lane **skips** when what it needs is absent
— a toolchain for most, a generated checkout for that last one — so a
documentation-only host passes, and no lane ever reports success for something
it did not run. Run one with `./tools/check fast --only <name>`.

| Lane | Command it runs | Skips when |
|---|---|---|
| `pins` | reads every machine owner file behind [TOOLCHAIN.md](../TOOLCHAIN.md) | never; a missing owner file is a failure |
| `chromium` | committed GN args profiles, fork-debt budget, overlay hygiene | never; these are committed files |
| `files` | the soft line cap over every non-generated source file, and one public type per Kotlin or Java file | there is no `python3`; both gates are stdlib-only |
| `modules` | the Kotlin module graph, and include direction in the Chromium overlay | there is no `python3`; both gates are stdlib-only |
| `shell` | `bash -n` and `shellcheck -x` over every script here, `py_compile` over `lib/*.py` | shellcheck is absent; the `bash -n` and `py_compile` halves still run |
| `secrets` | credential-shape scan over tracked *and* stageable files; then [`lib/operator_vocabulary.py`](lib/operator_vocabulary.py) over exactly what an export of the working tree would publish, refusing every identifier of the retired services that `check.d/operator-vocabulary.tsv` lists | not a Git checkout; the operator half skips where the list is absent, which is every exported tree, or there is no `python3` |
| `architecture` | exact manifest projections plus static GN-boundary checks and both self-tests | `taffy-core/` is absent, there is no `python3`, or Python is older than 3.11 |
| `evidence` | `./tools/evidence self-test`: schema, digest, identity, milestone-totality and forged-readiness refusal paths | the tool is absent or there is no `python3` |
| `catalog` | the model-route and asset catalogs (`generate_baseline.py`, `generate_catalog.py`) and the artifact packagers that fill their rows (`taffy-core/third_party/cpython/tools/`, `taffy-core/third_party/flag-icons/tools/`), each `--check` where it has one and each `--self-test`; plus `check_vendor_agreement.py` beside the baseline generator, which reads the four tables a subscription sign-in depends on and refuses a disagreement in either direction | a generator or packager is absent, or there is no `python3` |
| `rust` | `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --all-features -- -D warnings`, `cargo test --workspace --locked` | no `Cargo.toml` or no `cargo`; the fmt and clippy steps skip individually when their component is missing |
| `rust-graph` | `python3 taffy-core/services/core/tools/generate_crate_sources.py --check`, `.../check_build_graph.py`, `taffy-core/build/tools/check_build_reachability.py`, `.../check_publication_totality.py` (self-test, then the check) | the overlay is absent or there is no `python3`; none of the four needs a checkout |
| `contracts` | `--check`, `--self-test`, and `--verify` for BIP, Core API, Core Service, and Tool Runtime; plus BIP `--mojom`, the cross-contract drift checker, `taffy-core/contracts/codegen/facade_bodies.py` with its `--self-test`, and `taffy-core/contracts/core-api/codegen/status_payload_corpus.py --check` | any of the four `taffy-core/contracts/*/` generators is absent or there is no `python3` |
| `strings` | `python3 taffy-core/resources/catalog/tools/check_strings.py` and its `--self-test` | the overlay string catalogues are absent or there is no `python3` |
| `kotlin` | `./gradlew lintDebug testDebugUnitTest` | no wrapper, no `taffy-core/ui/android/`, no JDK, no Android SDK configured, or the SDK platform for the recorded `compileSdk` is not installed |
| `website` | `pnpm --dir website lint`, `typecheck`, `test` | as above, for `website/` |
| `fixtures` | `python3 test-fixtures/web/check.py` | `test-fixtures/web/` is absent or there is no `python3` |
| `fixtures-tasks` | `python3 test-fixtures/tasks/check.py` and its `--self-test` | `test-fixtures/tasks/` is absent or there is no `python3` |
| `icons` | `python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --check` | the Android icon generator is absent or there is no `python3`; it never needs `cwebp`, which only `--generate` uses |
| `mount` | `check_shell_sources.py`, `kotlin_mounts.py --verify`, `generate_kotlin_sources.py --check` and `check_product_manifest.py`, each with its self-test where it has one | `taffy-core/app/android/` is absent or there is no `python3` |
| `play` | `./tools/play check` — the committed Play listing against Play's field limits, the package against `taffy_branding.gni`, the copy against the *document* lint's own banned-vocabulary table, the track ladder against itself, and `tools/play.d/metadata/` against `store.toml` in both directions — then `./tools/play self-test`. It checks nothing about products, because there are none: decision 0200 removed everything that charged for anything, and the price book, the margin floor and the activation ladder went with it. The call still goes through `lane_run_soft`, which now only ever reports a listing finding | `tools/play.d/store.toml` is absent, there is no `python3`, or Python is older than 3.11. It never needs gplay, a credential or a network: everything it checks is a committed file |
| `orphans` | [`chromium/orphan-targets`](chromium/orphan-targets), against the profile this host last generated | not an x86-64 Linux host, no recorded workspace, no checkout, no `gn`, or no output directory anyone has run `gn gen` in |
| `public` | `export-public` `self-test`; `export-public.d/lib/overlay_screens.py` `self-test` and `check`, which hold the README's phone screens to their record and to the capture the website's copy of each came from; then `build --worktree` into a scratch directory, then `verify` on the result — the change under review, not `HEAD`. The keystore and password probes run only where a signing identity resolves, and are named as skipped everywhere else | the tree has no exporter (every exported tree, by design), there is no `python3`, or it is not a Git checkout |

`./tools/doctor` reports the same verdicts before you run anything, from the
same predicates, so the audit and the gate cannot disagree.

## Layout

- `lib/` — the shared modules, each with its authority boundary in its header:

  | Module | Owns |
  |---|---|
  | `common.sh` | repository root, output helpers, host probes, `key=value` files, workspace state |
  | `versions.sh` | reading one recorded version out of its machine owner file (`key=value`, TOML, JSON, the Gradle wrapper URL) |
  | `pins.sh` | the register mapping every TOOLCHAIN.md row to its owner file, and the gate that proves they agree |
  | `lanes.sh` | the lane names — `TAFFY_FAST_LANES`, `TAFFY_COMPONENT_LANES` — and the lookup over them; sources the two halves below |
  | `lane_readiness.sh` | one `lane_ready_*` predicate per lane: whether this host can run it, and the reason and remediation when it cannot |
  | `lane_runners.sh` | one `lane_run_*` function per lane: what it actually runs, plus the shared step and skip helpers |
  | `android.sh` | locating the Android SDK the way the Android Gradle Plugin locates it, and what the Compose build requires |
  | `node.sh` | the JS workspace: recorded Node/pnpm versions, package readiness, the check-script list |
  | `report.sh` | the finding store and the text/JSON renderers behind `doctor` |
  | `audit_host.sh` | the host, repository, pin and Chromium-workspace audits |
  | `audit_components.sh` | the toolchain, service, device, secret-name and lane audits |
  | `bootstrap_android.sh` | the Android host-loop profile's steps |
  | `docs_lint.py` | the documentation gate (host Python 3, no network) |
  | `code_lines.py` | how a source file is measured: not blank, not a comment, not an inline test block |
  | `source_index.py` | which files the modularity rules apply to, and which of them are generated |
  | `exemption_table.py` | reading a `check.d/*.tsv` policy table, and refusing a row with no reason |
  | `file_discipline.py` | the soft line cap: the number, the warning band, and the verdict |
  | `asset_publish.py` | what a published catalog variant claims, and whether the origin actually serves it — the check nothing static can make. Anonymous reads only; the upload half left with the origin |
  | `type_per_file.py` | one public type per Kotlin or Java file, and the file named for it |
  | `screen_view_model_keys.py` | no destination asked for two view-model classes under its route key: the second evicts and clears the first, and the screen holding the first stops updating with no error. `--self-test` proves the evicting pair is found across files and that a model under its own key is not |
  | `operator_vocabulary.py` | whether a set of files names an identifier of the retired services: the list and allow table below, one scanner for the `secrets` lane (over what an export would carry) and for `export-public verify` (over the exported bytes). An allow row may excuse vocabulary, never a value such as a key, and one that excuses nothing any more fails the lane |
  | `license_headers.py` | contradictory first-party notices below `taffy-core/`: Chromium's BSD boilerplate and the inaccurate `The TaffyGo Authors` holder. Its `--self-test` proves generator templates and the authoritative `taffy-core/build/` source are scanned, nested build output is pruned, and registered assets/inbound licence texts retain their own notices |
  | `module_graph.py` | the Gradle module graph, checked with no JDK |
  | `include_direction.py` | the `//taffy` product root's own DEPS rules, checked with no Chromium checkout |
  | `vendored_assets.py` | every third-party file this repository carries against the provenance record beside it: the owner, terms, source and checksum for each recorded file, plus its byte count when that directory opts in. `--self-test` exercises complete, missing, stale and incorrect byte facts while preserving checksum-only records. Its register is hand-maintained, like `pins.sh`'s — a gate can hold a record against its bytes, but only a person notices a vendored file that was never given a record |

- `chromium/lib/` — helpers shared by the `chromium/*` commands;
- `pins/` — machine owner files for TOOLCHAIN.md rows with no better home
  (`depot_tools`, `build-cache`, `gplay`);
- `check.d/` — the policy tables `check` reads. Every row of every table ends
  in a reason, and a row without one is a hard error rather than a skipped
  line:

  | Table | Decides |
  |---|---|
  | `allow-terms.tsv` | which banned-vocabulary hits are argued exceptions |
  | `file-size-exemptions.tsv` | which files may pass the soft line cap, and how large each may be |
  | `operator-vocabulary.tsv` | the hosts, keys, registrations and machines of the retired services that no published byte may carry; each row says whether an allow row may excuse it and carries a sample it must match |
  | `operator-vocabulary-allow.tsv` | which files may still name a piece of that vocabulary, one row per file and identifier. Neither table is exported, because a list of what must not be published would publish it |

- `export-public.d/` — the exporter's own directory, withheld from every
  export: `exclude.tsv` (what stays here, each row with its kind and reason),
  `lib/` (the build, verify, sync and self-test code), `overlay/` (the public
  `README.md`, `AGENTS.md`, `CONTRIBUTING.md`, `SECURITY.md` and `.github/`,
  published at the tree root), `overlay-screens.txt` (which capture each README
  screen under `overlay/.github/assets/screens/` was made from, and its bytes)
  and `github/` (the public repository's settings).

The Android module graph is not a policy table anymore. Its sole source is
`taffy-core/build/components.toml`; the component generator writes
`taffy-core/build/generated/android-modules.tsv`, and both
`tools/lib/module_graph.py` and the Gradle build logic read that projection.
The same manifest generates `gn-component-graph.json`; the architecture lane
checks its exact static target/source/dependency model, while
`tools/chromium/build` checks the configured GN graph immediately after the
fresh `gn gen` it owns.

Machine-local state lives in `.taffy/workspace.env` at the repository root —
written by `bootstrap` and `chromium/sync`, gitignored, safe to delete.

Targets bash 3.2 (stock macOS) and Linux. Windows contributors use the
docs-only and Android host-loop environments (`./tools/bootstrap --profile
docs` or `--profile android`) under WSL2.

## Who runs the lanes

Nobody but you. There is no hosted continuous integration and no `.github/`
directory — decision
0023 removed both, and the lane
table decision 0013
mapped onto workflows is superseded with it.

The lane names survive because they were always arguments to this suite, not a
provider's vocabulary: `./tools/check docs`, `./tools/check fast`, and
`./tools/check fast --only <lane>`. A lane whose toolchain is absent skips with
a named reason and is never reported as passing, which is the property that
makes running them yourself worth anything.

## What this directory does not do yet

Ordered, with the reason each one is still open. Nothing below is silently
missing from the code: each is a command that stops with this list rather than
inventing a result.

1. **`./tools/check device` has never been run.** It is no longer a stub: it
   selects one attached device by ABI — refusing rather than guessing when
   none, several, or one whose ABI does not match the requested profile is
   attached — builds `taffy_unittests`, `taffy_browsertests`,
   `taffy_public_test_apk` and `taffy_shell_junit_tests`, and runs each
   separately so one failure still leaves the others producing evidence. What
   it needs is the Chromium builder and a device in the same place, plus the
   emulator matrix of
   testing and delivery section 9.2.
   Until somebody runs it, nothing it would prove is proved, and the
   on-device results this repository holds came from `./tools/chromium/test`
   directly.
2. **`./tools/benchmark` loads and verifies both corpora, and no host has yet
   been able to execute a scenario.** The page corpus and the
   [scenario corpus](../test-fixtures/tasks/README.md) are both checked on
   every run, and `--list` reports what each suite would select. The command
   does hold a measured-run path — it hands the selection to
   [`benchmark.d/run.py`](benchmark.d/README.md), which drives `device.py`,
   `launcher.py`, `scenario.py` and `score.py` — but it takes that path only
   when one device is attached, its ABI names a committed profile, and a
   Chromium checkout is recorded, and it refuses rather than guesses when any
   of the three is missing. The current host satisfies those readiness checks,
   but `production` still requires the unratified device matrix and `affected`
   requires `--since`. Both browser-test adapters named by `launcher.py` now
   exist and load the immutable scenario bytes. The observation half drives
   page operations, and the task half starts a bounded selected-page task and
   reads its durable audit stream. Complete scenario coverage remains
   unfinished, so the scorer rejects their missing proofs. Its synthetic
   evidence tests now run in `fixtures-tasks`; those are not device results.
   Selecting and transport are not measuring, and the evidence
   record keeps `measured` false until a scenario actually runs.
3. **The `kotlin` lane runs lint and unit tests, not `assembleDebug`.**
   Testing and delivery section 9.1 also lists a debug assembly; it belongs
   with the device lane above, because tripling the lane's duration on every
   change buys a signal that building the product already provides.
4. **The Lighthouse budget gate is not a lane.** It exists and runs as
   `./tools/website audit`, with Lighthouse pinned in `website/package.json`
   and the lockfile; it serves the export the way GitHub Pages does, under the base path, and
   skips with a named reason when the host has no Chrome. It is not a member of
   `TAFFY_FAST_LANES`, so it runs only when someone runs it — which after
   decision 0023 is what every
   check here does, and is why the skip contract matters more rather than less.
5. **`sccache` is configured but has never cached a Rust compile.**
   `sccache --show-stats` reports zero compile requests, because
   `RUSTC_WRAPPER` is never set on the builder. Rust targets do build under GN
   now, so the old reason for this gap — that none did — is retired. The
   shared-bucket half of decision
   0013 is not a gap and
   will not be closed: it specified an object-store backend for a hosted CI
   runner, decision
   0023 abolished the runner,
   and decision
   0200 rules out this
   project operating the bucket. What is left is local disk.
6. **`dev` (up/status/down) does not live here and no longer has a topology
   to bring up.** It was going to start the local Worker, database and
   fixture-server stack; decision
   0200 deleted the
   first two, so what is left is the fixture server, which is not a stack.
   `./tools/release` does exist, and its own gap is narrower and sharper: it
   never signs. See [`release.d/README.md`](release.d/README.md).
7. **`./tools/release` is not a lane.** `./tools/release self-test` and a full
   `manifest --unsigned --sbom` run are real and pass, but neither is a member
   of `TAFFY_FAST_LANES`, so a green `./tools/check fast` proves nothing about
   the release path.
8. **The `play` lane cannot see anything Play would refuse for.** It checks
   three field lengths, three vocabulary regexes, a package-name agreement and
   a generated projection — all real, all offline. Trademark, misrepresentation,
   the Data safety declaration and the generative-AI content policy are the
   ways a listing actually gets refused, and none of them is checkable here.
   `./tools/play doctor` and `preflight` are where the rest is asked, and they
   need a credential and a network respectively.
9. **`./tools/play` and `./tools/release` are not coupled.** One verifies a
   manifest and refuses to sign; the other uploads an artifact Google
   re-signs. Nothing stops an upload of a build the manifest never saw. That
   coupling belongs to OD-062.
10. **No lane runs a command this repository's own documentation tells a
    person to type.** `./tools/check docs` validates links, anchors and
    register ids; a bare path inside a fenced block is none of those, so a
    runnable command naming a file that is not there is green forever. On
    2026-09-20 a sweep of every fenced block in every tracked Markdown file
    produced sixteen path-like tokens that do not resolve, and **one was a
    real defect**: `taffy-core/renderer/README.md` offered two commands under
    the sentence "Two commands prove it on any host, including one that cannot
    compile anything", and both named `tools/generate_observation_limits.py`,
    which has never existed — the generator is at
    `taffy-core/renderer/tools/generate_observation_limits.py`. From the
    repository root each one failed with `No such file or directory`. Fixed by
    naming the real path and then running both verbatim, which is the only way
    that claim can be made.

    The other fifteen are the reason this is a paragraph and not a lane. Four
    classes have to be understood before a checker is worth writing, and each
    is legitimate: **Chromium-checkout paths** in `chromium/patches/*.md`, which
    describe files in `src/` and correctly do not exist here;
    **name templates** such as `chromium/patches/NNNN-short-name.patch`;
    the **gitignored Kotlin mount** under `taffy-core/app/android/kotlin/`,
    absent until `kotlin_mounts.py --mount` runs; and **directory trees in
    `text` blocks** written relative to the README that holds them, like
    `taffy-core/components/tools/entrypoints/README.md`, where a
    repository-root reading is simply the wrong frame. A checker that cannot
    tell those four from a broken command would report fifteen findings and one
    defect, which is worse than the silence it replaces.
