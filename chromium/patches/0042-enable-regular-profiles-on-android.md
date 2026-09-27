# 0042 — Enable regular browser profiles on Android

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** SCR-708 and CAP-BR-020
**Estimated size:** ~180 modified upstream lines, 5 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/profiles/profiles_state.cc` |
| Symbol | `profiles::IsMultipleProfilesEnabled()` |
| File | `//chrome/browser/profiles/profiles_state_unittest.cc` |
| Symbol | regular-profile capability regression |
| File | `//chrome/browser/profiles/profile_manager.h` |
| Symbol | `ProfileManager::DeleteProfileOnAndroid()` |
| File | `//chrome/browser/profiles/profile_manager.cc` |
| Symbol | Android deletion, destruction, disk cleanup and restart recovery |
| File | `//chrome/browser/profiles/profile_manager_unittest.cc` |
| Symbol | inactive-only Android deletion regression |

## The change

Make `profiles::IsMultipleProfilesEnabled()` return true on Android as it
already does on the other supported platforms. Keep one direct regression in
the existing profile-state unit suite so an Android build cannot silently
restore the old false result.

The checkout showed that the existing deletion helper is desktop-only: it
depends on browser-window and keep-alive machinery that Android does not
build. The exported patch therefore also gives `ProfileManager` one
Android-only deletion operation. It accepts only a known, nonactive,
non-guest, durable regular profile; refuses the last profile and duplicate
requests; marks the path before removing its attributes; waits for a loaded
profile to be destroyed; and then uses Chromium's bounded disk-nuke routine.
The durable deleted-profile preference is replayed at the next start, including
the interrupted state in which an omitted ephemeral attributes entry was
written but not yet removed. Corrupt entries cannot name an absolute path, a
nested path, or a live regular profile.

TaffyGo keeps creation, activation, live-window exclusion, and confirmed
deletion in its Android surface and path-free JNI facade. Chromium continues
to own profile directories, attributes, profile lifetime, crash recovery, and
durable disk deletion.

## Why the overlay cannot host it

Directory allocation and loaded-profile destruction live inside
`ProfileManager`, and the capability guard lives in Chromium. An overlay
cannot replace either fact. Reimplementing them under `//taffy` would duplicate
private ownership state and could leave local state, profile attributes,
loaded services, or on-disk data disagreeing after interruption.

## Safety boundary

The patch creates no profile and exposes no upstream Android profile picker.
TaffyGo's facade accepts only the active regular profile, excludes private and
guest profiles, limits the device to eight regular profiles, uses opaque
identifiers across JNI, serializes mutations, refuses deletion of the active or
last profile, and requires confirmation before permanent deletion. Profile
data remains local and isolated; this change adds no account, sync, import, or
cross-profile data path.

## Rebase risk

**Medium.** The capability result is small, but Android deletion now owns a
narrow branch of profile lifetime and crash recovery. A rebase conflict, a new
Android profile-destruction path, or a change to the deleted-profile preference
requires a fresh audit of the downstream facade and an Android
create-switch-delete-restart device test.

**Retirement:** upstream supported Android regular-profile lifecycle, or keep
the capability result as a permanent fork seam while upstream Android remains
single-profile.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-01 from commit
`6f70a3a7e0d78` on `taffy/patched`. The queue file is
`0042-enable-regular-profiles-on-Android.patch`. The profile-state, profile
manager, manager-test, Taffy JNI C++, and Taffy JNI Java targets compiled in
the `dev-arm64` toolchain before export.

## Verify at export

1. The profile-state regression passes in the Android unit-test build.
2. TaffyGo can list exactly one active regular profile without exposing paths.
3. Create assigns a unique Chromium-owned directory and local display name.
4. Switching relaunches into the selected profile.
5. Confirmed deletion removes only a nonactive, non-last regular profile.
6. Private, guest, duplicate-name, over-limit, and concurrent mutations fail
   closed.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the five upstream files and commit with `Taffy-Patch: 0042`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
