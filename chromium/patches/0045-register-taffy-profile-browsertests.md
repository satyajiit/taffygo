# 0045 — Give the Android Profile verticals their own browser-test binary

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** The Profile-scoped acceptance verticals of decision 0037, run
under the one-process-per-test rule patch 0021 established
**Estimated size:** ~72 modified upstream lines, 5 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/test/BUILD.gn` |
| Symbol | `test("taffy_profile_browsertests")` |
| Symbol | `jinja_template("taffy_profile_browsertests_manifest")` |
| Symbol | the Android `test("android_browsertests")` dependency list |
| File | `//build/android/pylib/gtest/gtest_test_instance.py` |
| Symbol | `BROWSER_TEST_SUITES` |
| File | `//build/android/pylib/local/device/local_device_gtest_run_test.py` |
| Symbol | `LocalDeviceGtestRunTest.testGetTestsFromFiltersBrowserWildcardNeedsEnumeration` |
| File | `//chrome/test/android/browsertests_apk/AndroidManifest.xml.jinja2` |
| Symbol | `extra_application_definitions` |
| File | `//third_party/jni_zero/jni_zero.gni` |
| Symbol | the generated action in `generate_jni_impl` |

## The change

Give `//taffy/test/recovery:core_service_recovery_browser_tests` a dedicated
Android Chrome browser-test binary instead of a seat inside stock
`android_browsertests`, and teach the Android test runner that the new suite is
a browser-test suite.

1. Remove `//taffy/test/recovery:core_service_recovery_browser_tests` from the
   `android_browsertests` dependency list. Patch 0030's branch commit put it
   there; from this patch on, 0030 owns only the host `browser_tests` edge and
   this file owns the Android composition.
2. Add `test("taffy_profile_browsertests")`. It reuses Chrome's browser-test
   launcher, JNI onload source, Java peer and asset targets —
   `:browser_tests_runner`, `:test_support`, `:test_support_ui_android`,
   `:android_browsertests_java`, `:android_browsertests_assets`,
   `android/browsertests_apk/android_browsertests_jni_onload.cc` — together
   with `//chrome:chrome_android_core`, `//chrome/browser/profiles`, the
   crashpad handler trampoline and the recovery source set. Its `data` list is
   exactly `chrome/test/data/title1.html` and `title2.html`; it names neither
   the `chrome/test/data` nor the `content/test/data` directory root, so its
   generated runtime-dependency file stages one file per payload rather than
   the upstream corpora.
3. Add `jinja_template("taffy_profile_browsertests_manifest")`, which derives
   the upstream browser-test manifest through
   `//taffy/test/recovery/AndroidManifest.xml.jinja2` with
   `library_name=taffy_profile_browsertests__library`. The upstream template
   gains an empty application-definitions block. Only Taffy's overlay fills
   it, with the product's two non-exported file providers and their existing
   file roots; the overlay also adds the product's exact PDF-viewer query.
4. Add `'taffy_profile_browsertests'` to `BROWSER_TEST_SUITES`, beside the
   `taffy_browsertests` entry patch 0021 added. That is what makes
   `GtestTestInstance.__init__` set the `ShardSizeLimit` and `ShardNanoTimeout`
   instrumentation extras for the suite, which is the only way
   `content::BrowserTestBase` gets the fresh process per test it requires.
5. Extend the runner regression tuple so the wildcard-enumeration case covers
   the new suite name as well as `android_browsertests` and
   `taffy_browsertests`.
6. Compose `//taffy/test/android/profile:task_pdf_handoff_java` into this APK.
   It invokes the shipping download controller against the real Profile,
   waits for its provider, and exercises native task ownership before Android
   receives a PDF chooser. Its Kotlin value-class method is reached through
   one exact public suspend signature, with a keep rule in this test target.
   No provider, ownership answer or file URI is substituted.
7. Forward `testonly` to the action inside `generate_jni_impl`,
   as that template already does for its group and Java target. Without this
   line the test-only JNI declaration generates a non-test action and fails
   Taffy's configured component graph. Production defaults and the internal
   action's visibility stay unchanged.

## Why the overlay cannot host it

A GN target is declared where its `BUILD.gn` file lives. A source set under
`//taffy/test/recovery` can be depended upon, but it cannot declare a binary in
`//chrome/test`, and the browser-test composition it needs — the launcher, the
Java peer, the assets, the JNI onload source and the rendered Android
manifest — is assembled by targets private to that directory. The runner's
suite list is Chromium build tooling under `//build/android`, which the overlay
does not mount at all.

Building the same composition under `//taffy` would mean copying that
launcher, manifest and Java setup downstream and maintaining a second copy of
it at every milestone rebase. That is the expensive shape rule 4 of this
directory's README exists to refuse.

## Safety boundary

Everything this patch adds is test-only. The new target is a `test()`, it
enters no product target, and no shipping binary gains a dependency. Removing
the recovery source set from `android_browsertests` narrows what the stock
suite compiles; it adds nothing to it.

Two facts a reader should carry into a device run. The rendered manifest keeps
the upstream `manifest_package=org.chromium.android_browsertests_apk`, so the
new binary and `android_browsertests` install under one Android package name
and the second install replaces the first — they cannot be resident together.
And the suite is registered as a browser-test suite precisely so that no two
cases share a process; a suite that is built but left out of
`BROWSER_TEST_SUITES` fails on its second case with
`Check failed: !g_instance_already_created`, which reads as a test defect
rather than as a missing registration.

## Rebase risk

**Low to medium.** The pylib edits are one list entry and one test tuple. The
GN target deliberately mirrors a small subset of `android_browsertests`, so an
upstream refactor of the browser-test launcher, the JNI onload source, the
Java peer or the manifest template should conflict here and force the
composition to be reviewed rather than silently dropping the suite.

**Retirement:** upstream gains a way for a downstream test source set to
declare an Android Chrome Profile browser-test binary without editing
`//chrome/test/BUILD.gn`, or this remains a permanent test-only fork seam
reviewed at each milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06 from the
`taffy/patched` commit carrying `Taffy-Patch: 0045`. The evidence behind it is
narrow and worth stating plainly: the work was written against the real
upstream files in the pinned checkout, and a `dev-x64` build was running when
this specification was written and had not yet been confirmed. The suite has
**not** been built or run on a device, and no result from it is claimed
anywhere. Until `./tools/chromium/build --profile dev-arm64
taffy_profile_browsertests` and a device run exist, this patch is a
registration, not evidence that anything it registers passes.

## Verify at export

1. `taffy_profile_browsertests` configures and builds; the recovery source set
   compiles into it.
2. Stock `android_browsertests` still configures and no longer names any
   `//taffy` source.
3. The new binary's generated runtime-dependency file contains one host and one
   device path per payload and neither the `chrome/test/data` nor the
   `content/test/data` directory root.
4. A multi-case run of the new suite does not trip
   `!g_instance_already_created`, which is what proves the
   `BROWSER_TEST_SUITES` entry took effect.
5. `local_device_gtest_run_test.py` passes with the extended tuple.
6. Installing the new binary on a device replaces `android_browsertests`,
   because both render the same manifest package.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the five upstream files and commit with `Taffy-Patch: 0045`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
