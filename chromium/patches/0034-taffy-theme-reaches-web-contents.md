# 0034 — TaffyGo theme and Dark sites reach WebContents

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** SCR-407 Appearance (theme + Dark sites)
**Estimated size:** ~40 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/ui/android/night_mode/java/src/org/chromium/chrome/browser/night_mode/WebContentsThemeClient.java` |
| Symbol | `isNightModeEnabled`, `isForceDarkWebContentEnabled` |

## The gap this closes

`ChromeContentBrowserClient` writes `preferred_color_scheme` and
`force_dark_mode_enabled` from `WebContentsThemeClient`. That class currently
reads the device night bit (`ColorUtils.inNightMode`) and Chrome's disabled
"darken websites" feature flag. TaffyGo's Appearance setting therefore paints
Compose chrome dark while pages stay light.

TaffyGo cannot host this class: it is attached from `TabAndroid` and consumed
inside `chrome_content_browser_client.cc`. Writing
`webkit.webprefs.force_dark_mode_enabled` is overwritten on the next color
update.

## The change

`isNightModeEnabled` reads profile pref `taffy.ui.theme` (`SYSTEM` / `LIGHT` /
`DARK`). `DARK` is night, `LIGHT` is day, `SYSTEM` (or an absent profile)
falls back to `ColorUtils.inNightMode`.

`isForceDarkWebContentEnabled` returns true only when night is on **and**
profile pref `taffy.ui.force_dark_web` is true. It does not consult
`DARKEN_WEBSITES_CHECKBOX_IN_THEMES_SETTING`.

`UserPrefs` is already a `night_mode` Java dependency.

## Why the overlay cannot host it

The two `@CalledByNative` methods are the Android colour-scheme funnel.
A downstream observer that calls `WebContents::OnWebPreferencesChanged` still
loses to this class on the next update.

## Rebase risk

Low. The methods are short and TaffyGo-owned prefs are additive. A Chromium
rename of `WebContentsThemeClient` would need a one-file retarget.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-29, from the
`Taffy-Patch: 0034` commit on `taffy/patched`. The queue file is
`0034-let-TaffyGo-theme-and-Dark-sites-reach-WebContents.patch`, named
from the commit subject. The branch and the queue agree.
