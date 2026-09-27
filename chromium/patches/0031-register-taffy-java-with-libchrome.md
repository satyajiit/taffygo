# 0031 — Register the TaffyGo Java closure with libchrome

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** The Android product assembly, its eight JNI browser bridges,
the upstream `ContentViewRenderView` used by the page host, and the upstream
`CreditUtils` used by the About repository
**Estimated size:** one upstream line in one Android build file

## Upstream file and symbol

| | |
|---|---|
| File | `//chrome/android/BUILD.gn` |
| Symbol | `//chrome/android:chrome_java` dependencies |

## The change

Add the overlay-owned `//taffy/browser/android:jni_bridge_java` group to
Chromium's `chrome_java` closure. That group contains the eight TaffyGo bridge
targets, the upstream `//components/embedder_support/android:view_java`
target, and the upstream `//components/webui/about/android:aboutui_java`
target. `libchrome_impl` uses the public Chrome APK as its final-JNI Java root,
and that APK consumes `chrome_java`; the one edge lets the generator observe
the same bridge classes, `ContentViewRenderView`, and `CreditUtils` whose
native halves enter libchrome through `//taffy/browser`.

The change adds no Java source, copies no class, and does not weaken JNI's
missing-class check. The authoritative bridge sources remain under
`//taffy/browser/android/java`; the render view and credits bridge remain under
their upstream components; all reach the APK through
`//taffy/app/android:taffy_java`.

## Why the overlay cannot host the complete graph

The overlay owns the product APK and all four bridge Java targets, but Chromium
owns the public APK Java root used to generate libchrome's registration table.
An APK dependency alone is not enough: final JNI registration is generated
from the native library's declared Java roots, before product packaging. The
overlay cannot append itself to Chromium's `chrome_java` target.

Making the TaffyGo APK a second `libchrome_impl.java_targets` entry is also not
a seam: the product APK consumes libchrome's generated registration source jar
and would introduce a dependency cycle. One dependency on the exact
source-authoritative JNI group is the smallest acyclic edge. Disabling the
missing-JNI check or keeping uncalled registrations would hide an incomplete
dex instead of composing the product.

## Rebase risk

**Low.** The edit is one label in `chrome_java`'s existing dependency list. A
Java-root or APK-composition change conflicts at generation rather than
silently dropping a required class.

## Retirement

Permanent while Chromium owns libchrome's final JNI Java root; reviewed at each
milestone rebase. Retire it if the shared-library template gains a downstream
product-Java registration hook.

## Verification

Generate the Android graph, confirm the eight Taffy bridge source paths,
`ContentViewRenderView.java`, and `CreditUtils.java` appear in both the product
APK closure and libchrome's generated Java-source list, then build
`//chrome/android:libchrome__jni_registration`. The generator must accept all
five native-linked classes without a missing-class allowance.
