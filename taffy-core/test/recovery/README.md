# Core-service Profile recovery browser tests

**Status:** `[Current]` test-only Chrome Profile composition registered by
Chromium patches 0030 and 0045. This directory owns no product behavior.

This directory is intentionally separate from the portable Content Shell
harness in `//taffy/test`. `//taffy:taffy_browsertests` has Taffy's Content
Shell renderer and utility clients, but it has no Chrome `Profile` and does not
include this source set. Chrome's host `browser_tests` and dedicated Android
`taffy_profile_browsertests` instead supply the live Profile, active tab,
production
`ChromeContentUtilityClient`, and shipping `TaffyCoreService` registry needed
to exercise the product boundary.

`//taffy/test/recovery:core_service_recovery_browser_tests` is reached only
through
[patch 0030](../../../chromium/patches/0030-register-core-recovery-browsertest.md),
which adds it to Chrome's host `browser_tests`, and
[patch 0045](../../../chromium/patches/0045-register-taffy-profile-browsertests.md),
which declares the Android binary that carries it.
The source set is `testonly`; neither the product graph nor the Content Shell
test harness depends on it. Its narrow `DEPS` file permits the Chrome Profile,
tab, and browser-test APIs used here. The Android-only composition at
`//taffy/test/android/profile` adds the Java peer and shipping shell dependency
needed to exercise the PDF controller in the Profile test APK.
The Android binary is separate from stock `android_browsertests` so a focused
Profile case does not stage that suite's unrelated `chrome/test/data` and
`content/test/data` directory roots. It retains the Taffy task/web corpora used
by the fixture and benchmark support plus the exact two Chrome title fixtures.

## Runner split

"Common" below means portable between Chrome's host and Android Profile
runners. It does not mean portable to Content Shell.

| Sources | Runner | Boundary covered |
|---|---|---|
| `core_service_crash_recovery_browsertest.cc`, `core_service_manager_factory_browsertest.cc`, `taffy_page_intelligence_host_browsertest.cc` | `browser_tests` and `taffy_profile_browsertests` | Isolated utility recovery, regular/private Profile manager ownership, and Profile-owned page-host attachment and teardown |
| `core_service_task_vertical_browsertest.cc` | `taffy_profile_browsertests` only | Selected-page task completion and the saved-workspace/export/idle-restart vertical |
| `core_service_task_control_vertical_browsertest.cc` | `taffy_profile_browsertests` only | Cancellation plus durable pause/resume across a fresh core generation, through CoreStatus and the browser-private bindings |
| `filtering_vertical_browsertest.cc` | `taffy_profile_browsertests` only | Verified filter-pack transfer and activation; real browser- and renderer-originated blocks; tab/profile count publication; and the per-site exception |
| `library_memory_vertical_browsertest.cc` | `taffy_profile_browsertests` only | Explicit Library and Memory writes through the Profile facade, browser-owned durability, CoreStatus publication and search before and after an idle core restart |
| `errand_download_vertical_browsertest.cc` | `taffy_profile_browsertests` only | HTTPS navigation, person handover, fresh semantic download link, real PDF completion and exact task-owned open eligibility |
| `errand_bootstrap_vertical_browsertest.cc` | `taffy_profile_browsertests` only | Zero selected sources, real task-owned blank document, first model reply navigating directly to a known HTTPS address, verified accepted landing, semantic read and person handover; the fixture supplies tab creation and an embedder delegate forwarding navigation to Chromium instead of Android's TabModel |
| `task_benchmark_vertical_browsertest.cc` | `taffy_profile_browsertests` only | The Task Benchmark's task-side transport record; it starts no task and the scorer rejects it |

The task and workspace cases share
`core_api_status_observer.{cc,h}`, `core_api_status_reader.{cc,h}`,
`core_api_status_reader_task.cc`, `core_api_status_reader_workspace.cc`, the
delivery/provider skip reader and Library/Memory readers, and
`empty_vault_profile_platform_adapter.{cc,h}`. Workspace shape comparisons
and pre-/post-restart Markdown and CSV export checks live in
`workspace_vertical_test_support.{cc,h}`; exact durable Library/Memory
comparisons and search-publication checks live in
`library_memory_vertical_test_support.{cc,h}`. All of those helpers are inside
the `is_android` source partition in `BUILD.gn`, alongside the task, workspace,
cancellation, and benchmark cases.

The selected-page task case resolves a real product tab into browser-owned
source consent, submits the reviewed model-free task through the generated Core
API facade, observes the real renderer through a Rust-minted and browser-spent
capability, returns the published pending-action identity through the same
approval method as the Compose surface, and requires CoreStatus to publish the
browser-minted task identity and completed state. The workspace case saves the
result, compares both export formats before and after an idle core restart, and
requires the restored task and workspace identities and revisions to agree.
The separate control cases first cancel as soon as the browser-minted task
identity appears and require both CoreStatus and the browser-only terminal
binding to publish the cancelled state. The pause/resume case stops at a real
pending action, requires the Paused snapshot and durable Resume control to
agree on the revision, requires Pause to advance the durable task twice for
Pausing then Paused, and proves that returning the old action is rejected
without observing the page. It then terminates only the sandboxed utility
process while the browser tab and its issued source stay registered, restores
the same inert task and revision in the next generation, and resumes to a new
pending action that can finish the task.

The Library/Memory case first creates and explicitly approves a model-free
source-table task because Library refuses uncited or unsaved input. It saves
that real workspace, explicitly keeps one published fact, explicitly adds one
person-authored Memory sentence, searches both stores through the generated
Core API, tears the isolated core down only after it is quiescent, and requires
the same records plus fresh searches in the next generation. It does not seed
the SQLite store or create a test-only task or UI runtime.

The errand case serves a local HTTPS document page and a valid PDF. Its
provider fixture intercepts only the external model response; navigation,
page observation, handover, policy, download, storage and task completion
remain the shipping implementations. The person-side fixture enters a
synthetic private value and reveals a new download link. The next model
request must contain a fresh semantic handle and no private value. The case
compares the completed file's bytes and asks the same browser-owned eligibility
check as the task's Open PDF action. It also accepts and replays the recorded
flow across isolated-core restarts, then verifies that disabling it preserves
both completed tasks in a further core generation.

The optional `--taffy-test-pdf-handoff` device flag invokes the shipping download
controller with that real profile, completed replay file and native task
ownership. Its provider obtains the content URI and the controller opens the
Android chooser. The flag places the temporary files beneath the platform
Downloads directory and holds the fixture for 20 seconds after chooser
acceptance so the installed viewer can be selected and inspected. The test
result establishes chooser acceptance; a separate device capture must establish
viewer rendering. No provider, file URI or authorization result is substituted.
The fixture does not establish that a live identity service accepts a real
person's credentials.

The filtering case intercepts only the configured asset origin's response. It
drives the shipping profile asset plane through a cold transfer, digest check,
pack install, installed-set notification, list-member read, compile, and
matcher swap. The temporary asset interceptor is removed before the page opens.
A cross-origin subframe must then stop in Chrome's browser request path before
the embedded server sees it; the tab, lifetime, and week counts must publish the
block; and adding the document host to the product's per-site exception must
let the next subframe reach the server. The case repeats block and exception
with a renderer-originated `fetch()`, through the renderer loader adapter and
browser-owned request decision. This is deterministic evidence for delivery,
activation, both request paths, publication, and exception mechanics, not
evidence that the hosted publication served the same bytes.

The factory-lifetime case creates a primary private Profile, proves that
regular and private Profiles own distinct managers, destroys the private
Profile before fixture teardown, and requires the regular manager to remain
registered. The page-host lifetime cases attach to a real Profile-owned
`WebContents`, check idempotent attachment, and exercise both supported
host/broker teardown orders. Content Shell cannot substitute for any of those
Profile-owned boundaries.

## One loss, one generation advance

Utility loss can reach the browser twice: once as a Mojo disconnect and once as
a `ServiceProcessHost` terminal notification. Their order is not stable,
especially on Android.

The manager therefore records the exact `ServiceProcessId` together with the
service generation active when that process launched. A normal-termination or
crash notification may handle the loss only when both values still match the
current process/generation pair. Each Mojo disconnect handler separately
captures the generation of the pipe it watches and may handle the loss only
while that generation is current. Whichever valid notification arrives first
settles pending work, revokes generation-bound authority, and advances the
generation. The late callback from the old process or old Mojo pipe is stale;
it is ignored and cannot tear down the successor.

The crash case commits a real task through the production manager before
terminating the utility process. Every replacement process must restore that
task at revision 6 from the browser-owned checkpoint and journal tail; matching
only a Profile identifier is not durability evidence. It also requires fresh
service-process identities, fail-closed pending work, revoked old authority,
the restart delays, the Profile circuit, and explicit retry.

This is utility-process recovery within one live browser session. The retained
tab and its session-local consent binding cover fault isolation and
revalidation for that session; they do not claim consent restoration after a
browser-process restart.

## Evidence boundary

A focused `browser_tests` or `taffy_profile_browsertests` filter is evidence
only for the exact cases it selected. It does not establish a full-suite
result. The historical full baseline retained in
`../TEST-INDEX.md` belongs to
`taffy_browsertests`, the separate Content Shell binary, and is not a recovery
component result.

**A tool worker does not belong here, and the media-worker suite is where the
line falls.** A worker has no Profile in it: it is handed a job, the descriptors
the browser opened, and a pipe. It needs a browser process carrying the
product's utility registry, not a Chrome Profile. `//taffy/test` installs that
registry through `TaffyContentUtilityClient`, so the media-worker suite remains
in `taffy_browsertests`.
