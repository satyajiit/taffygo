# 0052 — Let a downstream manifest drop the media-capture service types

**Status:** `[Current]` — exported; the merged manifest of a built artifact is
the evidence
**Needed by:** decision
0252,
point 6
**Estimated size:** 4 added upstream lines, 1 moved, 1 file

## Upstream file and symbol

`//chrome/android/java/AndroidManifest.xml`: the
`FOREGROUND_SERVICE_CAMERA`, `FOREGROUND_SERVICE_MICROPHONE` and
`FOREGROUND_SERVICE_MEDIA_PROJECTION` permissions, and the
`android:foregroundServiceType` attribute of
`org.chromium.chrome.browser.media.MediaCaptureNotificationService`.

## The change

Wrap the three permissions in
`{% block media_capture_foreground_service_permissions %}` and the service's
type attribute in `{% block media_capture_foreground_service_type %}`. The
`{% set enable_screen_capture %}` line that sat between the permissions moves
above the first block, so the variable stays a template-level name the
service's block can still read. Every template that does not override the two
blocks renders what it rendered before.

TaffyGo's overlay overrides both with nothing.

## Why

Play asks a developer to declare, and demonstrate on video, every
foreground-service type the manifest carries. The only code in the pinned
Chromium that starts `MediaCaptureNotificationService` in the foreground with
any of those three types is `startOrUpdateForegroundService` in
`MediaCaptureNotificationServiceImpl`, and it runs only while
`media::kAndroidEnableBackgroundMediaCapturing` is on. That feature is
disabled by default and enabled only in `IS_DESKTOP_ANDROID` builds, which
TaffyGo is not. On a phone the service posts at most a notification, and
capture stops when the browser leaves the screen. A dump of the service on the
phone during a camera-and-microphone call on 2026-09-27 read
`startForegroundCount:0`.

So the three types were declared and never used, and there is nothing to
demonstrate. The feature posture in `taffy-core/browser/feature_posture.cc`
now disables the feature by name, so an upstream change of its default is
refused by the posture rather than turned into a `startForeground` call for a
type the manifest no longer declares.

The overlay cannot remove them any other way, for the reason patch 0051
records: `tools:node="remove"` is ignored for an element the same rendered
document declares.

## Rebase risk and retirement

**Low.** Four block lines around declarations upstream has not moved since the
Android 14 service-type requirement. A rebase that drops the blocks turns the
overlay's overrides into a jinja error at `gn gen`, not a silent return of the
permissions.

Retire it when upstream enables background capture on phones, which would
make the types real, or when upstream adds its own hook.

## Export and verification

The edit is committed on `taffy/patched` with `Taffy-Patch: 0052` and exported
by `./tools/chromium/export-patches`.

```bash
grep -cE 'FOREGROUND_SERVICE_(CAMERA|MICROPHONE|MEDIA_PROJECTION)' \
  out/release-arm64/gen/taffy/app/android/taffy_public_base_bundle_module/AndroidManifest.merged.xml
```

Zero. The source half, that the overlay still overrides both blocks with
nothing, is asserted by
`taffy-core/app/android/tools/check_product_manifest.py` in
`./tools/check fast --only mount`.
