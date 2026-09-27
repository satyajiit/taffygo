# 0041 — Let embedders supply the download notifier

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** SCR-802; TaffyGo must have one download-notification owner
without copying Chromium's download manager
**Estimated size:** ~100 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/java/src/org/chromium/chrome/browser/download/DownloadManagerService.java` |
| Symbol | `getDownloadManagerService()` and the pre-instantiation notifier factory |
| File | `//chrome/android/junit/src/org/chromium/chrome/browser/download/DownloadManagerServiceUnitTest.java` |
| Symbol | embedder-factory and late-install regressions |
| File | `//chrome/android/junit/BUILD.gn` |
| Symbol | download-package Robolectric source list |

## The change

Add one process-wide factory for the `DownloadNotifier` owned by
`DownloadManagerService`. An embedder may install the factory on the UI thread
before the singleton is created. The existing `SystemDownloadNotifier` remains
the default, so upstream Chrome behavior is unchanged. A late installation is
rejected because changing the notifier after construction would falsely imply
that the already-live service had changed owners.

The factory is consumed only when `getDownloadManagerService()` creates the
singleton; it does not replace the singleton or alter any download state,
storage, retry, foreground-service, or `OfflineContentProvider` behavior. A
focused Robolectric regression proves that an installed factory supplies the
exact notifier and that installation after singleton creation is refused.

TaffyGo installs a no-op implementation during its pre-native process hook,
then publishes its own profile-scoped notifications from the same live
`OfflineContentProvider` projection used by Downloads. This makes the
downstream notification surface the sole poster while keeping Chromium's
download lifecycle authoritative.

## Why the overlay cannot host it

`DownloadManagerService.getDownloadManagerService()` constructs
`SystemDownloadNotifier` directly. `DownloadController` reaches that singleton
without consulting the TaffyGo activity or profile graph, so subscribing an
overlay observer alone produces duplicate notifications. The overlay cannot
remove or replace the notifier after construction, and copying the manager
would fork far more stateful download behavior than this single seam.

## Safety boundary

The hook selects only a presentation sink. It cannot start, pause, resume,
cancel, open, share, remove, or retry a download. It carries no profile, URI,
filename, or page data. Installation is UI-thread-only and must precede
singleton creation. Upstream's concrete notifier remains the fallback when no
embedder participates.

## Rebase risk

**Low.** One direct constructor call becomes a factory lookup, plus a focused
host regression. A conflict means Chromium changed singleton or notification
ownership and should be reviewed rather than mechanically resolved.

**Retirement:** upstream the embedder factory, or keep it as a permanent narrow
hook while `DownloadManagerService` directly owns notifier construction.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-01 from commit
`24fadd8075e17` on `taffy/patched`. The queue file is
`0041-let-embedders-supply-download-notifier.patch`.

## Verify at export

1. With no factory installed, `SystemDownloadNotifier` remains the service's
   notifier.
2. A factory installed before singleton creation supplies the exact notifier.
3. Installing a factory after singleton creation is rejected and does not
   replace the live notifier.
4. TaffyGo installs its suppression notifier before any download manager can be
   created and separately posts only regular-profile notifications.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three upstream files and commit with `Taffy-Patch: 0041`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
