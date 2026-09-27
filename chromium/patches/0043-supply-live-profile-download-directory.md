# 0043 — Supply the live profile download directory

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** CAP-AG-005 and `browser.download.start`
**Estimated size:** ~8 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/ui/tab_helpers.cc` |
| Symbol | `TabHelpers::AttachTabHelpers()` |

## The change

Bind each eligible page-intelligence host to the owning profile's
`DownloadPrefs::DownloadPath()` through a repeating callback. The host resolves
that callback when it classifies a completed download, so a preference change
made after the tab was created immediately replaces the old allowed directory.

The callback is profile-scoped and path-only. It does not expose Chromium's
download manager, preference service, or profile object to the sandboxed core.

## Why the overlay cannot host it

The overlay owns the page-intelligence host and the effect classifier, but it
does not own `Profile` or Chromium's `DownloadPrefs` factory. The upstream tab
attachment point is the one place where the WebContents and its owning profile
are both available. Capturing a process-wide or attach-time default in the
overlay would be wrong for multiple profiles and would become stale whenever a
user changes the download location.

## Safety boundary

The resolver is read-only and runs only while its WebContents is alive.
`DownloadPrefs` is profile keyed and outlives the tab. Classification still
requires the completed file to be beneath the resolved directory; an empty
resolver result authorizes nothing. The callback does not create directories,
move files, broaden filesystem access, or reveal a path to Taffy.

## Rebase risk

**Low.** The change adds one argument at the existing TaffyGo attachment hook.
A rebase that changes tab-helper lifetime, profile ownership, or download
preference storage requires the callback lifetime and freshness tests to be
rerun.

**Retirement:** upstream exposes a stable per-WebContents download-directory
resolver to embedders, or this remains a permanent narrow fork seam reviewed at
each milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-01 from the
`taffy/patched` commit carrying `Taffy-Patch: 0043`. The overlay effect source,
page-intelligence host, their unit-test object, and Chromium's shipping
`//chrome/browser/ui:ui` target compiled in `dev-x64`; the matching overlay
objects compiled in `dev-arm64` before export.

## Verify at export

1. A completed file below the current profile download directory satisfies the
   download postcondition.
2. Changing the profile preference after a tab is attached makes the old
   directory fail closed and the new directory classify successfully.
3. An empty resolver result and a sibling-prefix path authorize nothing.
4. The focused `TaffyBrowserEffectSourceTest` suite passes in the ARM Android
   unit-test runner.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the upstream attachment point and commit with `Taffy-Patch: 0043`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
