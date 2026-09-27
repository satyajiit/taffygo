# `tools/release.d/`

**Status:** `[Current]` — every module here runs today on any host;
`./tools/release self-test` is the proof, and it runs only when someone runs
it. There is no hosted lane (decision
0023) and `release` is not a
member of `TAFFY_FAST_LANES` in [`../lib/lanes.sh`](../lib/lanes.sh) either, so
a green `./tools/check fast` says nothing about this directory.
**Owning milestone:** M1, work package WP-M1-07 in the
implementation plan.
**Authority boundary:** the modules behind [`../release`](../release). They own
the artifact manifest — its schema, how it is assembled from facts, how it is
verified, and the dependency inventory it cites. They own no policy: what a
manifest must contain comes from
testing and delivery §13, the
signing rules from `PAR-SEC-004` and decision
0013, and the
inventory requirement from `PAR-SEC-010` in the
parity matrix.

## The idea

A manifest describes an artifact, and the facts about that artifact are
observed in different places. The repository knows its revision, its Chromium
pin and its patch queue. The build job knows its host and its cache
configuration. The signing job knows the certificate. The test job knows which
suites ran on which devices. Each writes a **fragment**; `./tools/release
manifest` composes them in order and verifies the result.

Nothing here ever fills in a fact it did not observe. A field that no job
supplied is absent, and an absent field fails validation with a sentence naming
the job that owes it. A defaulted field would pass validation while describing
nothing, which is the failure mode this whole directory exists to prevent.

## Modules

| File | Owns |
|---|---|
| [`manifest-schema.json`](manifest-schema.json) | The manifest contract: every field, its type, when it is required, and the remediation sentence printed when it is missing |
| [`schema_validate.py`](schema_validate.py) | The eleven JSON Schema keywords the contract uses, and the two `x-` extensions it adds |
| [`manifest.py`](manifest.py) | The command line around the schema: validate, print the field table, run the fixture suite |
| [`repo_facts.py`](repo_facts.py) | Digests, sizes, and the Chromium patch queue — the single implementation of every byte-level fact |
| [`collect.sh`](collect.sh) | The recorded pins and revisions, read through the repository's own version machinery |
| [`repository_fragment.py`](repository_fragment.py) | Shapes those values into the manifest's fragment |
| [`artifact_fragment.py`](artifact_fragment.py) | Hashes the real files being attested |
| [`assemble.py`](assemble.py) | How fragments combine, and what a later fragment replaced |
| [`sbom_collectors.py`](sbom_collectors.py) | The Rust and JavaScript collectors, the four coverage states, and the import path for the two ecosystems another track owns |
| [`gradle_inventory.py`](gradle_inventory.py) | The Gradle collector: two sources of truth, and an offline license lookup against the module cache |
| [`track_inventory.py`](track_inventory.py), [`track_inventory_native.py`](track_inventory_native.py) | The Chromium-track producers: upstream shipped-only license traversal plus byte-matched native libraries read from the complete product APK |
| [`sbom.py`](sbom.py) | The CycloneDX document and the license inventory, and their reproducibility |
| [`verify.py`](verify.py) | Runs every check in order and renders the findings |
| [`verify_checks.py`](verify_checks.py) | Claims about the checkout: revision, pins, patch queue, build configuration, signing topology and provenance |
| [`verify_artifacts.py`](verify_artifacts.py) | Claims about the upload: confined paths, every artifact digest and size, drill evidence kinds, and inventory coverage |
| [`promotion_policy.py`](promotion_policy.py) | The single promotion seam and orchestration of every mandatory candidate check |
| [`promotion_policy_selftest.py`](promotion_policy_selftest.py), [`promotion_selftest_fixtures.py`](promotion_selftest_fixtures.py) | Refusal-path coverage and its byte-backed fixture builder |
| [`promotion_artifacts.py`](promotion_artifacts.py) | The fixed candidate artifact set and path/digest links from evidence sections |
| [`promotion_benchmark.py`](promotion_benchmark.py) | Approved distinct physical-device rows and strict, typed verification of the attested benchmark report |
| [`promotion_benchmark_runs.py`](promotion_benchmark_runs.py) | Links every benchmark summary row to its attested raw-run JSON and rejects contradictions |
| [`promotion_corpus.py`](promotion_corpus.py) | The task- and page-corpus identity committed at the candidate revision |
| [`promotion_identity.py`](promotion_identity.py) | Binds a candidate to the generated capability profile and runs the repository-owned verifier over its revision-bound evidence index |
| [`promotion_approvals.py`](promotion_approvals.py) | The internal named-person and separation checks used by the promotion policy |
| [`rollback.sh`](rollback.sh) | Halt criteria, the rollback package contents, and the rehearsal |
| [`fragments/`](fragments/unsigned-development.json) | Committed fragments for states that are the same on every run — today, the unsigned development artifact |
| [`fixtures/`](fixtures/candidate-complete.json) | One schema-complete manifest; the promotion self-test supplies its byte-backed benchmark rows and mutation suite |

## Coverage states

Every ecosystem in the inventory reports one of four states, because "no
components found" and "the tool that finds components is not installed" are
different facts:

| State | Meaning |
|---|---|
| `complete` | The authoritative command ran and every component is listed |
| `partial` | A real but incomplete source was read; the blocker names the command that closes it |
| `unavailable` | Nothing could be read; the blocker names why |
| `deferred` | Another track owns it; the owner and the import file are named |

A release candidate accepts only `complete` and fails on the rest, naming each.

## Promotion is one closed check

Schema version 2 separates useful development evidence from promotion evidence.
The committed development fragment says `promotable:false`; changing the
verification profile cannot relabel it as a candidate. Candidate verification
always calls the promotion policy and always requires approvals — there is no
flag that turns those checks on.

The candidate evidence set contains the release AAB and install APK, the test
APK, mapping and symbols, SBOM and license inventory, vulnerability, test and
benchmark reports, every benchmark raw-run record, provenance, the evidence
index and the rollback rehearsal.
Each named suite must report `pass`, zero retries, zero skipped checks and an
attested report. The required names cover both repository gates, all four
device-lane suites, Android instrumentation, sanitizers, BIP fuzzers, upstream
regression, the production benchmark, accessibility, privacy/deletion,
release configuration and rollback. Two names left with decision
0200: backend
compatibility and the managed-AI smoke were about hosts this project no longer
runs, and so was the whole `backend` block of the manifest they reported into.
The benchmark link is not only a digest: promotion reads the attached v3 JSON
and requires the runner's production, measured, complete, passing and
release-eligible verdicts, its 50-run floor, clean source/profile identity, and
the exact task-corpus name, version, digest and production selection committed
at that source revision, matching page-corpus and ratified-matrix versions, and
a digest-checked raw-run artifact agreeing with each summary row.

The candidate also carries approved low-, mid-, and high-tier physical-device
observations with distinct matrix rows, a non-empty measured and approved
benchmark, and at least three distinct people across the five named
approval roles. Its capability
identity must be the generated `candidate` profile at accepted milestone M8,
with delegated task start enabled at task milestone M8 on the ratified M7
policy surface. The current M0 candidate is deliberately disabled and cannot
promote; reaching M8 without opening that complete tuple is also a refusal.
Its attested evidence index must belong to the same source revision and pass:

```bash
./tools/evidence verify <index> --artifacts-dir <dir> --root . --candidate
```

That command recomputes readiness from the indexed records and all nine
milestone exits. An empty or hand-marked `candidate_ready` index cannot satisfy
the release verifier.

## Verification list — what this directory does not do yet

Ordered, each with the reason it is still open. None is treated as work this
directory performed: where another release job supplies the evidence, its
manifest fields and attested artifact link are mandatory for a candidate.

1. **Chromium's own third-party inventory needs a built checkout.** Run
   `./tools/release inventory --profile release-arm64 --out <dir>` on the
   Chromium track. It invokes upstream's shipped-only traversal for the actual
   TaffyGo GN target and writes `chromium-inventory.json`; import that exact
   file with `--chromium-inventory`.
2. **The packaged native library list needs the complete built APK.** The same
   command reads the non-incremental path recorded by the product build graph,
   inventories every `lib/<abi>/*.so`, and byte-matches each prebuilt library
   to its source AAR and `README.chromium`. It refuses an incremental installer
   whose only native entry is Chromium's empty sentinel. Import the resulting
   `android-native-inventory.json` with `--android-native-inventory`.
3. **The Gradle inventory is the declared set, not the transitive closure.**
   Resolving the closure needs a Gradle configuration to resolve, which needs
   the Android SDK and a build. Pass the resolved report with
   `--gradle-report <file>` and the collector reports `complete`. Licenses come
   from POM files already in the Gradle module cache, following literal cached
   Maven parent coordinates because licenses are inherited. A coordinate whose
   POM or required parent has never been downloaded on that host resolves to
   nothing rather than being guessed.
4. **No dependency vulnerability scanner runs here.** This repository installs
   nothing unpinned at run time and no scanner is pinned yet, so the scan is a
   separate step that supplies its own fragment, run by whoever prepares the
   release. A development manifest records `status: not-run` and says so; a
   release candidate cannot.
5. **Nothing signs, uploads or promotes.** Signing separation is verified as a
   property of the manifest and of the workflow structure; the signing itself
   belongs to the release lane's own job and its platform credential, which
   this repository never reads.
6. **`build.caches` hit rates are supplied, not measured.** Cache statistics
   belong to the build that used the cache. The verifier checks the claim is
   coherent — a hermetic build with an enabled cache fails — but it cannot
   measure a build it did not run.
7. **Approval rows are structural claims, not authenticated signatures.** The
   verifier requires named people, times, roles and separation, but it does not
   authenticate an approver or cryptographically bind that approval to the
   candidate digest. A promotion job still needs that signed binding.
8. **Device approval and tier claims have no committed matrix authority yet.**
   Promotion rejects empty or duplicate matrix rows, but `approved`, `tier`
   and `matrix_row` are still supplied claims rather than lookups against the
   ratified device matrix. OD-017 must close and publish that authority.
9. **Several evidence artifacts are linked, not interpreted.** Vulnerability,
   rollback, provenance and general test-report bytes must exist and match
   their attested digest and size, but this verifier does not parse their
   internal verdicts. Their producing jobs remain responsible for those
   semantics until repository-owned formats and parsers are defined.

## Running it

```bash
./tools/release self-test                    # schema, fixtures, composition, inventory
./tools/release inventory --out artifacts/release --profile release-arm64
./tools/release sbom --out artifacts/release # the inventory on its own
./tools/release manifest --out artifacts/release --kind development \
  --profile dev-x64 --unsigned --sbom        # compose, then verify
./tools/release verify artifacts/release/manifest.json --profile candidate
./tools/release schema --fields              # every field and when it is required
```
