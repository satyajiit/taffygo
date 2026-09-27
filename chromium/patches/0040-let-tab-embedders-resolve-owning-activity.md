# 0040 — Let a tab embedder resolve its owning Activity

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** PAR-NAV-003 and PAR-WEB-011
**Estimated size:** ~90 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/java/src/org/chromium/chrome/browser/tab/InterceptNavigationDelegateClientImpl.java` |
| Symbol | `getActivity()` |
| File | `//chrome/android/junit/src/org/chromium/chrome/browser/tab/InterceptNavigationDelegateClientImplUnitTest.java` |
| Symbol | Activity-resolution regression cases |
| File | `//chrome/android/junit/BUILD.gn` |
| Symbol | tab-package Robolectric source list |

## The change

Keep the existing `TabImpl.getActivity()` result when it exists. When it does
not, ask the tab's `WindowAndroid` for the Activity it owns:

```java
Activity activity = mTab.getActivity();
if (activity != null) return activity;
WindowAndroid window = mTab.getWindowAndroid();
return window == null ? null : window.getActivity().get();
```

The focused host regression holds all three boundaries: Chrome's Activity is
still preferred, a non-Chrome embedder with an Activity-backed window is
recognized, and a detached or non-Activity-backed tab still returns null.

## Why the overlay cannot host it

`InterceptNavigationDelegateImpl` decides whether a tab is attached before it
consults the tab's `ExternalNavigationHandler`. Its only Activity fact comes
from `InterceptNavigationDelegateClientImpl`, whose current implementation
calls `TabImpl.getActivity()`. That method deliberately narrows the result to
`ChromeActivity`, so a supported embedder built directly on
`AsyncInitializationActivity` remains permanently "detached" even though its
tab has an Activity-backed `WindowAndroid`.

The downstream factory and handler already sit behind this check. Adding
another overlay delegate cannot change a decision made before any overlay
delegate is called.

## Safety boundary

This patch launches nothing and changes no URL, scheme, chooser, task, or
assistant policy. It supplies the Activity fact the interception component
already requests. The downstream handler remains responsible for deciding
whether a navigation may leave the browser; a missing window, a collected
Activity, and a truly detached tab continue to fail closed.

## Rebase risk

**Low.** One existing getter gains the same fallback already exposed by
`WindowAndroid`, plus a package-local host test. A rebase conflict means the
client's Activity contract changed and should be reviewed rather than guessed.

**Retirement:** upstream the generic fallback, or keep it as a permanent
embedder hook while `TabImpl.getActivity()` remains Chrome-specific.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-01 from commit
`a59ec6095b006` on `taffy/patched`. The queue file is
`0040-let-tab-embedders-resolve-owning-activity.patch`.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three upstream files and commit with Taffy-Patch: 0040
./tools/chromium/export-patches
./tools/check fast --only chromium
```
