# 0024 — Suppress NewApi for ContentViewRenderView at minSdk 29

**Status:** `[Current]` — committed on `taffy/patched` and exported by
`./tools/chromium/export-patches` on 2026-08-21 as
`0024-suppress-NewApi-for-ContentViewRenderView-at-minSdk-.patch`.
**Needed by:** decision
0024 — TaffyGo's own
page host renders web content through `ContentViewRenderView`, and the moment
it does, `./tools/chromium/build --profile dev-arm64` fails at
`//chrome/android:chrome_public_apk__lint`
**Estimated size:** ~8 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/expectations/lint-suppressions.xml` |
| Symbol | the `<issue id="NewApi">` block |

## The failure

Five `[NewApi]` errors, all in an upstream file TaffyGo does not edit:

```text
../../components/embedder_support/android/java/src/org/chromium/components/embedder_support/view/ContentViewRenderView.java:98: Error: Call requires API level 31 (current min is 29): android.view.Window#getRootSurfaceControl [NewApi]
../../components/embedder_support/android/java/src/org/chromium/components/embedder_support/view/ContentViewRenderView.java:100: Error: Call requires API level 35 (current min is 29): android.view.AttachedSurfaceControl#getInputTransferToken [NewApi]
../../components/embedder_support/android/java/src/org/chromium/components/embedder_support/view/ContentViewRenderView.java:113: Error: Call requires API level 35 (current min is 29): InputTransferHandler [NewApi]
../../components/embedder_support/android/java/src/org/chromium/components/embedder_support/view/ContentViewRenderView.java:119: Error: Call requires API level 35 (current min is 29): getMap [NewApi]
../../components/embedder_support/android/java/src/org/chromium/components/embedder_support/view/ContentViewRenderView.java:154: Error: Call requires API level 35 (current min is 29): remove [NewApi]
```

`minSdk` 29 is not a TaffyGo choice here — it is upstream's own
`default_min_sdk_version` at this milestone
(`build/config/android/config.gni`), so the file is already meant to be
API-29-safe by upstream's standard.

## Why TaffyGo is the first to see it

Lint's file set is `chrome_public_apk`'s Java graph, and TaffyGo put
`view_java` in it:

```bash
gn refs out/dev-arm64 //components/embedder_support/android:view_java
gn path out/dev-arm64 //chrome/android:chrome_public_apk \
    //components/embedder_support/android:view_java
```

The first command names three consumers in this graph:
`//taffy/app/android:content_host_java`,
`//content/shell/android:content_shell_apk_java` and
`//content/shell/android:content_shell_java`. The second shows the edge is
TaffyGo's:

```text
//chrome/android:chrome_public_apk --[public]--> //chrome/android:chrome_public_apk__java
  --[private]--> //chrome/android:chrome_all_java --[private]--> //chrome/android:chrome_java
  --[private]--> //taffy/app/android:island_java
  --[private]--> //taffy/app/android:content_host_java
  --[private]--> //components/embedder_support/android:view_java
```

(Recorded before patch 0009 retired; `island_java` is gone, and the edge now
runs through `//taffy/app/android/shell:shell_java`, which names
`:content_host_java` directly. The argument is unchanged.)

Neither content shell target builds a lint target, so upstream never lints
this file. The errors are a latent upstream lint gap that TaffyGo surfaced by
being the first lint-running consumer of the view.

The lint target is reached from the product target through upstream's own
template — `chrome/android/chrome_public_apk_tmpl.gni`, guarded only by the
`disable_android_lint` argument:

```bash
gn path --with-data out/dev-arm64 //taffy:taffy_public_apk \
    //chrome/android:chrome_public_apk__lint
```

prints `taffy_public_apk --[data]--> //chrome/android:android_lint
--[private]--> //chrome/android:chrome_public_apk__lint`.

## The calls are guarded, and this is the load-bearing finding

**None of the five is an unguarded call, so there is no crash waiting on an
API 29 or 30 device and nothing is being hidden.** Two guards are at work, and
neither is one lint can follow.

The first is a runtime SDK check that lives in C++. Lines 96–101 read:

```java
InputTransferToken browserInputToken = null;
Window window = mWindowAndroid.getWindow();
if (InputUtils.isTransferInputToVizSupported() && window != null) {
    AttachedSurfaceControl rootSurfaceControl =
            window.getRootSurfaceControl();
    assumeNonNull(rootSurfaceControl);
    browserInputToken = rootSurfaceControl.getInputTransferToken();
}
```

`InputUtils.isTransferInputToVizSupported()`
(`components/input/android/java/.../InputUtils.java`) is a `@NativeMethods`
call, and the native side is `InputUtils::IsTransferInputToVizSupported` in
`components/input/utils.cc`:

```cpp
if (base::android::android_info::sdk_int() <
    base::android::android_info::SdkVersion::SDK_VERSION_V) {
  // InputOnViz does not work on < Android V, since the touch transfer APIs
  // were introduced in Android V.
  return false;
}
```

`SDK_VERSION_V` is API 35, so on an API 29 or 30 device the branch never runs
and neither the API 31 call at line 98 nor the API 35 call at line 100
executes. The guard is real and it is a version guard; it is simply on the
other side of a JNI boundary, where a Java source analyser cannot reach it.

The second guard is null-dataflow. Line 113 constructs `InputTransferHandler`
and line 119 calls `SurfaceInputTransferHandlerMap.getMap()` — both types
carry `@RequiresApi(Build.VERSION_CODES.VANILLA_ICE_CREAM)` — inside
`if (surfaceId != null && browserInputToken != null)`, and
`browserInputToken` is assigned nowhere but line 100. Line 154 calls
`SurfaceInputTransferHandlerMap.remove(mSurfaceId)` inside
`if (mSurfaceId != null)`, and `mSurfaceId` is assigned nowhere but line 118,
inside that same branch. Both are sound; neither is a version check.

## The change

One `<ignore>` element and its rationale comment, inside the `NewApi` block
that already exists for this purpose, following that block's own convention of
stating the number of suppressions being added. The regexp is the file's path,
so the grant is one check over one file — never a blanket disable, and never a
message-shaped regexp, which would suppress the same wording everywhere in the
build.

## Why the overlay cannot host it, and what the alternatives cost

The suppressions path is hardcoded at `chrome/android/BUILD.gn`:

```text
lint_suppressions_file = "expectations/lint-suppressions.xml"
```

There is no argument for it, so no downstream file can add a suppression to
this build. Four alternatives were considered and three were measured rather
than assumed.

**Make the guard visible to lint instead** — add
`Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM` to the
condition at line 96. This is the change one would upstream, and it was tried
in the checkout and linted. It fixes two of the five errors and leaves three:

```text
ContentViewRenderView.java:116: Error: Call requires API level 35 (current min is 29): InputTransferHandler [NewApi]
ContentViewRenderView.java:122: Error: Call requires API level 35 (current min is 29): getMap [NewApi]
ContentViewRenderView.java:157: Error: Call requires API level 35 (current min is 29): remove [NewApi]
```

`NewApi` does not follow the null-dataflow guards, so clearing those three
means restructuring upstream's callback bodies or annotating them anyway. That
is more modified lines, in a real source file that moves at every milestone,
and it still needs a suppression. Rejected on evidence, not on principle.

**`disable_android_lint = true` in the argument profiles** — zero patches, and
the reason it is refused. It does not disable one error in one file; it
disables Android lint for the whole build, including every check that guards
TaffyGo's own Kotlin and Java. A negative control run for this patch showed
what that would throw away: with the suppression in place, the identical
`getRootSurfaceControl` call injected into
`//taffy/app/android/.../TaffyPageRenderView.java` still fails the
build. Under `disable_android_lint` it would not. Trading a live quality gate
for one line of XML is not a saving.

**Add a lint argument for a downstream suppressions file** — a patch to
`chrome/android/BUILD.gn`, a far higher-churn file than the XML, for more
lines and the same outcome.

**Stop depending on `view_java`** — that is the page host, which is decision
0024's whole point. Not available.

## Rebase risk

**Low.** A suppressions file is append-mostly and rarely conflicts; if the
`NewApi` block is restructured the patch conflicts loudly and re-lands. The
one silent failure mode is upstream fixing the annotations, after which the
suppression would sit there unused — which the retirement check below is for.

**Retirement: upstream the annotation fix.** The honest repair is upstream's
to make, and it is small: either widen the guard at line 96 so lint can see
it, or mark the callback bodies `@RequiresApi`. Attempt after the shape
survives one milestone rebase. Until then, re-check at each rebase by removing
the `<ignore>` element and re-running lint; when it passes without it, this
patch is dead and the queue gets one number back.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit chrome/android/expectations/lint-suppressions.xml in the checkout;
# commit with an owner and a reason, and the trailer `Taffy-Patch: 0024`
./tools/chromium/export-patches
./tools/check fast
```

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-21, from the
`Taffy-Patch: 0024` commit on `taffy/patched`. The hand-written `git diff`
export this section used to describe, and the debt it recorded — a queue
file whose lines existed only as uncommitted modifications in the checkout —
were retired the same day: the suppression row was committed with
its trailer and the exporter rewrote the queue from it.
The branch and the queue agree, and `./tools/check fast --only chromium`
verifies that agreement together with the fork-debt figures.
