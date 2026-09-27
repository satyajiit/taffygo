# 0022 — Point the launcher at the TaffyGo activity

**Status:** `[Current]` — exported as
`0022-point-the-launcher-at-the-TaffyGo-activity.patch` and applied on the
`taffy/patched` branch
**Needed by:** WP-M1-04 — decision
0024 makes every
screen TaffyGo's, and a person reaches the first one by tapping the icon. Until
this lands, the icon opens Chrome's tabbed activity and none of the transferred
surfaces is reachable at all
**Estimated size:** ~11 modified upstream lines, 1 file — one changed attribute
and one added `<activity>` block in
`//chrome/android/java/AndroidManifest.xml`.
**Realised: 11 modified upstream lines** — see "What the export changed"
below. It was 17 from 2026-08-23 until 2026-09-20, while this patch also
carried a closed auth-callback filter; that filter is gone and so are its six
lines.

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/java/AndroidManifest.xml` |
| Symbol | the `com.google.android.apps.chrome.Main` `activity-alias`, and the `<application>` block that must declare the target |

## Why this is a patch and not a downstream override

This was checked in the tree at the pin rather than assumed. The launcher entry
is an `activity-alias` named `com.google.android.apps.chrome.Main` whose
`android:targetActivity` is `org.chromium.chrome.browser.ChromeTabbedActivity`,
and it carries `android.intent.category.LAUNCHER` in its intent filter.

The manifest is a jinja template, so the first question is whether the block is
already parameterised — a downstream value would cost no patch at all, which is
mechanism D in decision 0024 and always preferable. It is not. The `{% if %}`
conditions nearby govern channel-specific categories inside the filter, not the
alias or its target, so there is no variable to set and no block to override.
An Android manifest merger cannot retarget an existing alias either: merging
adds and overlays elements, and the launcher category would end up declared
twice, pointing at two activities.

That is one patch, and it is the first of the six decision 0024 budgets for UI
ownership. It also happens to be the smallest possible form of the change, which
is why the record spends the budget here rather than on a broader manifest edit.

## The change

Two edits in one file:

1. Retarget the existing alias:

   ```xml
   android:targetActivity="org.chromium.taffy.shell.TaffyBrowserActivity"
   ```

2. Declare the target, beside upstream's own activity declarations. It needs
   the same `android:hardwareAccelerated` posture the surrounding comment
   requires of anything derived from `ChromeActivity` — TaffyGo's activity is
   not, but it hosts the same compositor, so the note applies for the same
   reason rather than by inheritance.

There was a third edit until 2026-09-20, and rule 7 of
[the queue's README](README.md) is why it is recorded here rather than
deleted. It added `com.taffygo.browser://auth` as a closed `VIEW`/`BROWSABLE`
filter on the already-exported launcher alias, so that Android handed the
account plane's OAuth return to the trusted profile-bound ingress and Compose
never saw a URI, code, verifier or token. Decision
0200 removed the
servers that flow signed in to, and the ingress that read the intent left with
it, so the filter named a URI nothing in the product can emit and nothing
would read if it arrived. It is removed from this patch rather than withdrawn
by a later one: a queue that adds a filter and then takes it away is two
numbers describing one thing that never shipped, and both would be paid at
every rebase.

**Provider sign-in never used this road and is unaffected.** A vendor's
redirect is claimed by a navigation throttle inside the errand page (decision
0095),
which is a navigation and not an Android intent; the manual-code and
device-authorization roads do not involve Android either.

Nothing else moves. `ChromeTabbedActivity` keeps its upstream declaration for
Chromium's internal explicit callers, but it is not a product entry point or a
second TaffyGo UI runtime.

## What this deliberately does not do

- **It does not remove `ChromeTabbedActivity`.** Removing it would be a much
  larger patch against a class that many upstream call sites still name. Those
  internal callers do not make it a second product entry point.
- **It does not retarget the other aliases.** `IntentDispatcher`,
  `AudioLauncherActivity` and `MediaLauncherActivity` route view and media
  intents rather than the launcher, and each is its own transfer with its own
  surface behind it (SCR-804, SCR-805). Retargeting them here would point live
  intent filters at an activity that cannot yet serve them.
- **It does not carry the `<activity>` for any other TaffyGo surface.** Every
  other screen is a view inside this activity's tree, which is the whole reason
  the patch budget is six and not thirty.

## What the export changed

- **The position is required, not editorial.** Android requires an
  `activity-alias` target to be declared before the alias, so the target block
  sits immediately above it.
- **The `hardwareAccelerated` posture is taken by reference.** The target calls
  upstream's `chrome_activity_common` jinja block and therefore cannot drift
  from the compositor posture of Chromium's own activities.
- **The target is not exported; the alias is.** A launcher alias must be
  exported to be launchable, and the activity behind it does not have to be.
  The alias carries upstream's launcher filter and nothing this patch adds: no
  URI filter, broad or closed, and no web intent filter. That last sentence was
  false between 2026-08-23 and 2026-09-20, when the auth-callback filter was
  the one thing this patch added to it.

## Verification

- **Observed on the builder of record, 2026-08-19, before this patch is
  applied.** `gn gen out/dev-arm64` printed `Done. Made 63087 targets from 4670
  files`, `gn ls out/dev-arm64 "//taffy/app/android/shell:*"` listed the
  nine sub-targets, and
  `gn desc out/dev-arm64 //taffy/app/android:taffy_public_apk deps --all` reached
  `//taffy/app/android/shell` on seven edges. `autoninja -C out/dev-arm64
  taffy/app/android/shell:shell_java` then printed `The build has
  finished successfully`, producing
  `org/chromium/taffy/shell/TaffyBrowserActivity.class` together with
  its `__validate_deps.bytecode` and `__errorprone` stamps.

  So the activity is already inside the product the APK packages, and it
  compiles against `//chrome/android:chrome_java` — which is the claim decision
  0024's whole survey rests on. This patch is what makes it reachable by a
  person rather than only by the build. What none of that shows is a running
  activity: nothing here has been installed or launched.
- **Observed on the builder of record, 2026-08-20, with this patch applied.**
  `autoninja -C out/dev-x64 taffy/app/android:taffy_public_apk` printed `The
  build has finished successfully`, and the APK's own merged manifest —
  `out/dev-x64/gen/taffy/app/android/taffy_public_apk/AndroidManifest.merged.xml`
  — carries the `<activity>` block with
  `android:hardwareAccelerated="false"` expanded out of the jinja call, and the
  alias with
  `android:targetActivity="org.chromium.taffy.shell.TaffyBrowserActivity"`.
  `./tools/check fast --only chromium` moved from 18 realised patches and 187
  modified upstream lines to 19 and 198.
- **Observed on the device of record, 2026-08-20.** The patch does what it says
  and `cmd package resolve-activity -c android.intent.category.LAUNCHER
  com.taffygo.browser` reports
  `name=com.google.android.apps.chrome.Main
  targetActivity=org.chromium.taffy.shell.TaffyBrowserActivity`,
  enabled and exported. Two things the manifest change could not have shown
  came out of that first launch, and both are recorded here rather than filed
  elsewhere, because both are consequences of retargeting the alias and neither
  is a defect in the alias.

## What retargeting the alias broke, and what fixed it

Rule 7 of [the queue's README](README.md) applies to consequences as well as to
estimates. Neither of these is a change to this patch — the manifest edit is
eleven lines — but a reader who lands here after a rebase needs to know that
this one attribute moves two things that are not in the manifest at all.

That "eleven" is worth one sentence, because it was wrong for four weeks and
nothing could have said so. It was written when the patch was eleven lines,
stayed put when the auth-callback filter took it to seventeen, and is right
again now that the filter has gone. `tools/lib/fork_debt.sh` reads
`**Estimated size:**` and counts the `.patch` files; it does not read a number
spelled out in prose, so a figure in a sentence is a claim a reader has to
check by hand.

- **It moved product composition to the new Activity.** The shipping Activity
  now asks the browser-owned `TaffyProfileRuntimeProvider` for the exact regular
  or private profile, opens one sibling Window component after native profile
  initialization, and closes only that Window at Activity teardown. Rust runs
  only in the profile utility service; no task runtime or JNI-to-Rust bridge is
  created in the browser Activity.
- **It put Chrome's first-run experience in front of TaffyGo.** On a cleared
  install the launcher resolved here, `AsyncInitializationActivity.onCreate`
  handed the launch to `FirstRunActivity`, and a person saw TaffyGo's brand mark
  above the words "Welcome to Chrome", a Google Terms of Service link and a
  Google telemetry disclosure. Only second and later launches reached TaffyGo,
  which is why every screenshot after the first looked correct. **This did not
  become patch 0023.** `requiresFirstRunToBeCompleted(Intent)` is `protected` at
  the pin and both of its call sites test it before acting, so overriding it in
  `TaffyBrowserActivity` suppresses the flow completely — mechanism D of
  decision 0024, a
  downstream override, at zero patch cost. The upstream comment that overriding
  it "is almost always wrong" is written for activities whose first run *is*
  Chrome's; TaffyGo's own first-run sequence is SCR-001 onward and is reachable
  from this activity's first frame.
- The rebase risk is **Low** and stays low: the alias has carried the same name
  and the same shape across milestones, and the patch touches one attribute.
  The risk that matters is not conflict but silence — an upstream rename of the
  alias would leave the patch applying cleanly to a block nobody launches, so
  the device check above is the one that actually proves it.
