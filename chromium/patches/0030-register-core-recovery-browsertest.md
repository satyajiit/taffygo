# 0030 — Register the Profile recovery browser tests

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** The Profile-scoped core-service recovery acceptance gates in
decision 0037
**Estimated size:** 64 added and 2 removed upstream lines in one test build file
**Actual size:** 2 added lines in one test build file — see
[What this specification got wrong](#what-this-specification-got-wrong)

## Upstream file and symbols

| | |
|---|---|
| File | `//chrome/test/BUILD.gn` |
| Symbols | `//chrome/test:browser_tests`, `//chrome/test:taffy_profile_browsertests`, and `//chrome/test:taffy_profile_browsertests_manifest` |

## The change

Add the test-only
`//taffy/test/recovery:core_service_recovery_browser_tests` source set to
Chrome's host `browser_tests`, and give its Android partition a dedicated
`taffy_profile_browsertests` binary. The Android target reuses Chrome's browser
test JNI, Java, manifest and asset setup, but it does not inherit the
monolithic upstream `android_browsertests` source or data closure. It owns only
the Taffy source set, Chrome's exact Java peer for its always-on fake
InstanceID test helper, and the exact `title1.html` and `title2.html` fixtures.

The source set's own `BUILD.gn` and `DEPS` make it an explicit embedder-owned
test component instead of weakening the Content Shell-based
`//taffy:taffy_browsertests` harness. The source remains under `//taffy`; the
upstream change carries no implementation and does not enter a product target.

The component has two deliberate source partitions:

- The common Profile cases — `core_service_crash_recovery_browsertest.cc`,
  `core_service_manager_factory_browsertest.cc`, and
  `taffy_page_intelligence_host_browsertest.cc` — compile into both
  `browser_tests` and `taffy_profile_browsertests`. They cover isolated
  utility-process crash and durable-task recovery, regular/private Profile
  manager ownership, and Profile-owned page-host attachment and teardown.
  "Portable" here means shared by Chrome's host and Android runners; these
  cases are not part of the Content Shell `taffy_browsertests` binary.
- The `is_android` partition compiles only into
  `taffy_profile_browsertests`. It owns
  `core_service_task_vertical_browsertest.cc` for selected-page task completion
  and saved workspace export/restore across an idle core restart,
  `core_service_task_control_vertical_browsertest.cc` for cancellation and
  durable pause/resume across a fresh core generation, and
  `filtering_vertical_browsertest.cc` for the Profile filtering request path,
  verified filter-pack activation, count publication, and per-site exception,
  and
  `task_benchmark_vertical_browsertest.cc` for the Task Benchmark's task-side
  transport adapter. That adapter emits a rejected `transport-only` record; it
  starts no task and is not benchmark evidence.

The crash case uses the runner's live regular `Profile`, active tab,
production `ChromeContentUtilityClient`, and shipping `TaffyCoreService`
registry. It finds the service through `ServiceProcessHost` by its exact Mojo
interface, terminates only that operating-system process, and exercises the
Profile manager's generation, authority-revocation, lazy-restart, and circuit
behavior.

On Android that service PID is not an operating-system child of the browser
process, so Chromium cannot synchronously wait for it with `waitpid`. The case
sends the termination without that unsupported wait and polls only for the old
PID to disappear, without running the browser UI loop. That preserves the
deliberate interval in which the utility is gone but its pipe-loss task is
still queued, so the first post-crash submission is genuinely pending on the
dead pipe before recovery handles it.

Recovery has two independently delivered loss notifications. A
`ServiceProcessHost` terminal event is authoritative only when both its exact
`ServiceProcessId` and the generation recorded when that process launched
still match the manager's current generation. Each Mojo disconnect handler
likewise captures the generation of the pipe it watches. Whichever notification
arrives first handles the loss and advances the generation; the other is a
stale callback and must not tear down the successor process.

## What this specification got wrong

This argument described one change and the queue now holds two. Everything
below about a dedicated `taffy_profile_browsertests` binary — the target in the
symbol table, the manifest action beside it, and the rebase and verification
sections that reason from them — is not in
`0030-register-the-core-recovery-browser-test.patch`. That patch is two added
lines: it names
`//taffy/test/recovery:core_service_recovery_browser_tests` in two existing
upstream dependency lists, and declares nothing.

The binary is [patch 0045](0045-register-taffy-profile-browsertests.md), which
also carries the two `//build/android/pylib` runner entries the suite needs.
The split was not a change of mind. The target text was written here, reviewed
here, and then lived for weeks only as an uncommitted modification in the
Chromium checkout that no `.patch` owned, so this specification described a
binary the queue could not produce and a `sync` would have deleted. Committing
it on 2026-09-06 forced the question of which number owns it, and a second
number was the honest answer: 0030 is a dependency edge, 0045 is a target
declaration plus its runner registration, and they rebase against different
upstream churn.

What it cost is the point the estimate discipline is meant to learn from. The
64-line estimate was for the work, and the work was real — but 62 of those
lines were not in the queue, so the fork-debt figure this directory reports was
short by that much for as long as the gap lasted, and nothing could see it. An
estimate is only as good as the export that eventually settles it, and an
entry whose patch never lands settles nothing.

## Why the overlay cannot host the complete graph

TaffyGo's standalone `taffy_browsertests` target is intentionally based on
Content Shell. Content Shell has neither a Chrome `Profile` nor Chrome's
production utility-service registry, so mounting these cases there would
require a testing profile and a test utility client. That would test a
substitute composition, not the shipping Profile boundary.

Chrome owns both browser-test runner compositions. A test source set can live
entirely below `//taffy/test/recovery`, but it cannot add itself to the host
runner or create an Android Chrome Profile runner. One host dependency edge and
one dedicated Android test target are the smallest truthful seams. The overlay
reachability gate records this directory as an upstream-registered test root
and rejects a missing registration or a later product dependency on it.

Putting the source set in stock `android_browsertests` was not equivalent. Its
generated runtime-dependency file owned the broad `chrome/test/data` and
`content/test/data` directory roots, so every focused Taffy case staged more
than a gigabyte of unrelated data before discovery. The dedicated target keeps
those roots with their upstream owner and gives the complete Android Profile
suite one small, canonical staging closure.

## Rebase risk

**Low to medium.** The host edit is one test-only dependency. The Android
target deliberately mirrors the small launcher/JNI/manifest subset of
`android_browsertests`; a runner or manifest refactor should conflict here and
force the composition to be reviewed rather than silently dropping the tests.

## Retirement

Permanent while Chrome owns the Profile and utility-service browser-test
runners; reviewed at each milestone rebase. Retire it if Chromium gains a
downstream test-registration hook that supports the host edge and a dedicated
Android Chrome Profile runner without a fork patch.

## Verification

Build and run the common Profile cases from the host `browser_tests` runner.
Build `taffy_profile_browsertests` and run both the common cases and the
Android-only task, workspace, pause/resume, cancellation, and filtering cases
on a device. The stock `android_browsertests` target does not contain Taffy
sources.
`taffy_browsertests` is the Content Shell suite and is not an acceptance target
for this component.

The generated Android runtime data must expand to one host and one device path
per payload, with neither the `chrome/test/data` nor `content/test/data`
directory root present. A focused device run must use fresh test data and the
runner's default setup timeout.

The crash-recovery case must observe a distinct utility-process identity after
recovery while the same browser tab and browser-owned Profile identity survive.
It must also commit a task at revision 6 before the first termination and
recover that exact revision through each fresh utility process from the
browser-owned checkpoint and journal tail. A focused filter establishes only
the selected cases; it is not a replacement full-suite baseline.
