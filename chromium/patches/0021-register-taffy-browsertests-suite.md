# 0021 — Give Android browser tests one process per exact test

**Status:** `[Current]` — suite registration applied 2026-08-19; scoped-filter
isolation added 2026-09-04 after the first multi-test vertical run exposed the
runner shortcut
**Needed by:** WP-M1-02 and WP-M2-08 — every Android browser test must run in
a fresh process, including tests selected through a scoped wildcard
**Estimated size:** ~46 modified upstream lines, 3 files

## Upstream files and symbols

| File | Symbol |
|---|---|
| `//build/android/pylib/gtest/gtest_test_instance.py` | `BROWSER_TEST_SUITES` |
| `//build/android/pylib/local/device/local_device_gtest_run.py` | `_ExtractTestsFromFilters`, `LocalDeviceGtestRun._GetTests` |
| `//build/android/pylib/local/device/local_device_gtest_run_test.py` | `LocalDeviceGtestRunTest` |

## The change

1. Register `taffy_browsertests` in `BROWSER_TEST_SUITES`. This makes the
   instrumentation runner enforce a one-test shard limit for the downstream
   suite.
2. Do not let `--extract-test-list-from-filter` represent a browser-suite
   wildcard such as `Suite.*` as one synthetic test. Return to normal device
   enumeration so the wildcard resolves to exact names before instrumentation
   sharding.
3. Keep the shortcut for exact browser-test filters and for scoped wildcards
   in non-browser suites.

The unit regression covers `android_browsertests`, `taffy_browsertests`, and
the dedicated `taffy_profile_browsertests` runner, plus the two preserved fast
paths.

## Why the patch exists

`content::BrowserTestBase` refuses to construct a second browser test in one
process:

```text
Check failed: !g_instance_already_created. Each browser test should be run in
a new process. If you are adding a new browser test suite that runs on
Android, please add it to //build/android/pylib/gtest/gtest_test_instance.py.
```

On Android a gtest APK runs a whole shard in one process by default. The
one-test-per-shard behaviour a browser test needs is switched on by two
instrumentation extras — `NativeTestInstrumentationTestRunner.ShardSizeLimit`
and `.ShardNanoTimeout` — which `GtestTestInstance.__init__` sets only for a
suite named in `BROWSER_TEST_SUITES`.

Registration alone handles a full-suite run and a colon-separated list of
exact tests. It did not handle the generated wrapper's local-development path:

- `run_taffy_browsertests` passes `--fast-local-dev`;
- that enables `--extract-test-list-from-filter`;
- `_ExtractTestsFromFilters()` treats one scoped trailing wildcard as a
  complete one-item test list, avoiding a roughly three-second device query;
- Android therefore receives `Suite.*` as one shard, and gtest expands its
  matches only after the native process has started.

The instrumentation shard limit sees one synthetic item, not the exact tests
behind it. The first matching browser test is valid; constructing the second
in that same process trips the CHECK. `--test-launcher-batch-limit=1` cannot
repair this because the launcher likewise sees only the one wildcard item.

The observed `CoreServiceTaskVerticalBrowserTest.*` run demonstrated that
sequence exactly: `BuildSource` passed, `CompletedWorkspace` began in the same
test-process PID and crashed at the CHECK, and `Cancel` was never started.
That is why the report contained two results with only one pass even though
the filter matched three tests.

For browser suites, a wildcard now takes the existing enumeration path. The
resolved exact names then reach the existing one-test instrumentation shards,
which gives each match its required process. An exact filter still avoids the
query, and non-browser suites retain the scoped-wildcard optimization.

## Why the overlay cannot host it

The suite list and the only authoritative resolved test list both live inside
Chromium's Python runner:

- **No GN hook.** Android `test()` targets use Chromium's generated runner,
  which supplies `--suite <target_name>` but offers no target-level way to
  change this filter-extraction policy.
- **No instrumentation-extra flag.** The public runner does not expose the two
  browser shard extras. `--test-launcher-batch-limit` controls a different
  in-process layer.
- **No downstream exact-name source.** A repository wrapper has the wildcard
  but not the APK's resolved test list. Reimplementing list-tests, filtering,
  sharding, retries and result reporting beside Chromium's runner would create
  a second test protocol and would still leave the generated
  `out/<profile>/bin/run_taffy_browsertests` command unsafe.

The durable seam is therefore the runner branch that already knows both the
suite kind and the enumerated device tests.

**Upstreamable:** partly. The downstream suite-name entry has no meaning in
upstream Chromium and remains fork debt. The generic rule that browser-suite
wildcards must be enumerated before Android sharding is independently useful
upstream. They remain one numbered patch here because together they implement
one invariant: every selected browser test gets one process.

## Rebase risk

**Low to medium.** The suite entry is a literal list addition. The behavioral
change is a small branch around an existing optimization, with unit coverage
at that branch. A conflict is likely only if upstream changes filter
extraction or derives browser-test isolation without the name list.

**Retirement:** when upstream both recognizes downstream browser suites by a
non-name-based mechanism and guarantees that a wildcard is expanded before
one-test Android sharding.

## Verification

Host regression:

```bash
cd /path/to/chromium/src
PYTHONPATH="$PWD/build/android" \
  vpython3 build/android/pylib/local/device/local_device_gtest_run_test.py
```

Queue and repository gates:

```bash
./tools/check fast --only chromium
./tools/check docs
```

Device acceptance after importing the refreshed patch and rebuilding the
runner/APK:

```bash
./tools/chromium/test --profile dev-arm64 taffy_profile_browsertests \
  -- --gtest_filter=CoreServiceTaskControlVerticalBrowserTest.*
```

Acceptance is all matching tests reported, no
`g_instance_already_created` CHECK, and a distinct Android test-process PID
for each exact test.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three files above in the checkout;
# commit with the Taffy-Patch: 0021 trailer
./tools/chromium/export-patches
./tools/check fast
```
