# 0048 — Witness physical profile deletion

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** The browser-owned restore lifecycle of decision 0122
**Estimated size:** ~493 modified upstream lines, 2 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/profiles/nuke_profile_directory_utils.h` |
| Symbol | `NukeProfileFromDiskWithResult()`, `VerifyProfileAndCacheDirectoryDeletion()` |
| Symbol | `PersistProfileDirectoryDeletionMarker()`, `ArmProfileDirectoryForDeletion()`, `CompleteProfileDirectoryDeletionMarker()` |
| Symbol | `IsProfileDirectoryMarkedForDedicatedDeletion()`, `IsDedicatedProfileDirectoryDeletionMarker()`, `GetDedicatedProfileDirectoryDeletionMarkerPath()` |
| File | `//chrome/browser/profiles/nuke_profile_directory_utils.cc` |
| Symbol | `ProfileDeletionStage`, `NukeProfileFromDiskImpl()`, `MarkProfileDirectoryForDeletion()`, `IsProfileDirectoryMarkedForDeletion()`, `NukeDeletedProfilesFromDisk()`, `NukeProfileFromDisk()` |

## The change

Chromium's profile deletion is fire-and-forget by design: `NukeProfileFromDisk`
takes a `base::OnceClosure`, retries a few times, records a histogram, and runs
the closure whether or not the directory is gone. Nothing in the public
interface can answer the one question a restore needs before it overwrites
anything — *is that directory actually absent now?*

This patch adds that answer, and the durable custody record that makes it
survive a restart, without changing what any existing caller receives.

**A third deletion stage.** `ProfileDeletionStage` gains
`MARKED_WITH_DEDICATED_CACHE` beside `SCHEDULING` and `MARKED`. It names one
in-process physical attempt with the exact per-profile cache mapping.
`NukeDeletedProfilesFromDisk` still sweeps only `MARKED`, so shutdown never
replays a dedicated attempt; it drops the volatile mark and deliberately leaves
the durable record alone.

**A result-bearing deletion.** `NukeProfileFromDiskWithResult` runs the same
retry loop but reports a boolean, and reports `true` only when the physical
witness below agrees. `NukeProfileFromDisk` keeps its closure signature and is
now a thin adapter over the same implementation.

**A physical witness.** `VerifyProfileAndCacheDirectoryDeletion` is a read-only
blocking check that both the profile path and its derived cache path are
absent, and that the surviving parent directory entries have been synchronized.
On POSIX absence is decided by `lstat` returning `ENOENT` rather than by
`PathExists`, so a dangling symbolic link is not read as absence, and the
parent directories are held open across the removal and `fsync`ed, with device
and inode identity re-checked on the held handle so a swapped-out parent cannot
be mistaken for a synchronized one. Off POSIX the synchronization step is a
no-op and the absence check falls back to `PathExists` plus `IsLink`.

**An exact cache mapping, or nothing.** `GetDedicatedCachePath` accepts a
profile only when it sits directly under `DIR_USER_DATA` and its base name is a
generated regular-profile name — Chromium's `kMultiProfileDirPrefix` followed
by a positive canonical integer. On Android it additionally requires that
Chromium's own `GetUserCacheDirectory` mapping lands on that base name inside
`DIR_CACHE`, and refuses when the derived path is the shared cache root itself
or equal to the profile path. A redirected path, a `Default` profile, or a
disabled cache-isolation mapping fails closed instead of turning the
application's shared cache directory into deletion custody.

**A durable marker that is evidence, not an instruction.**
`PersistProfileDirectoryDeletionMarker` appends a dictionary to Chromium's
existing `prefs::kProfilesDeleted` list carrying a version key
(`taffy_restore_deletion_version`, currently `1`) and the profile's base name
(`taffy_restore_profile_path`), and marks the attributes entry ephemeral and
omitted. It refuses when another dedicated marker or a legacy base-name marker
for the same path is already present, so one quarantined candidate can never be
broadened into a different physical deletion. `ArmProfileDirectoryForDeletion`
is the separate second step that moves an already-scheduled path with a
verified durable marker to the physical stage; it writes no preference, which
is what lets a caller drain and read back Local State between the two.
`CompleteProfileDirectoryDeletionMarker` removes only the exact marker once
absence has been witnessed. `IsDedicatedProfileDirectoryDeletionMarker` is
deliberately generous — it answers `true` for a malformed dedicated marker
too — while `GetDedicatedProfileDirectoryDeletionMarkerPath` decodes only the
canonical shape, so a corrupt marker is retained and fences automatic deletion
rather than being discarded or acted upon.

**Three existing behaviours change**, and a reviewer should decide on each one
separately from the additions above:

1. `MarkProfileDirectoryForDeletion` is now implemented on the same persist and
   arm helpers. It requires an existing `ProfileAttributesEntry` and an allowed
   profile path, sets the omitted bit independently of the ephemeral bit rather
   than only when the entry was not already ephemeral, and cancels the
   scheduling when either step fails. Upstream's desktop deletion path in
   `delete_profile_helper.cc` calls it.
2. `NukeProfileFromDiskImpl` now requires the physical witness before reporting
   success, for the plain path as well as the dedicated one. That changes when
   the retry loop retries and what `Profile.NukeFromDisk.Result` records.
3. `IsProfileDirectoryMarkedForDeletion` answers `true` for the dedicated stage
   as well as `MARKED`, so `CanCreateProfileAtPath` and
   `FindLastActiveProfile` exclude a path that is mid-deletion.

## Why the overlay cannot host it

The deletion staging map is a function-local `base::NoDestructor` inside this
translation unit, and shutdown reads it through `NukeDeletedProfilesFromDisk`.
A second custody record under `//taffy` would not be a copy of that state; it
would be a different one, and the two would disagree exactly when they matter —
after an interrupted deletion. The durable half has the same problem from the
other side: `prefs::kProfilesDeleted` is Chromium's list, replayed by Chromium
at start, and a marker in a separate overlay register could not fence that
replay.

The cache mapping is Chromium's too. `chrome::GetUserCacheDirectory` and
`chrome::kMultiProfileDirPrefix` decide where a profile's derived cache lives;
an overlay that recomputed the answer would be asserting a mapping rather than
reading one, and the failure mode of guessing wrong is deleting a shared cache
directory.

Nothing this patch adds names a `//taffy` symbol, and it introduces no
dependency on the overlay. The only coupling is the shape of one durable
marker.

## Safety boundary

The dedicated marker is custody evidence and never authority. Startup does not
replay it, shutdown does not act on it, and a malformed one fences the
automatic-deletion pass instead of being cleaned up. The only path from marker
to physical work is an explicit, in-process arm-then-delete sequence by a live
caller.

Deletion is confined by construction. It runs only for a path the profile
manager already allows, only for a generated regular-profile base name, and
only when the derived cache path resolves to that same base name — three
independent refusals, any one of which stops the operation. When the exact
parent directories cannot be held and synchronized, neither recursive removal
begins, so a missing or replaced cache parent cannot turn a failed custody
check into a half-deleted profile.

A failed attempt withdraws itself: the volatile mark is erased whatever the
outcome, so nothing can replay the operation without fresh authority, and the
durable marker is left untouched so the state remains recoverable. Success is
never inferred from the absence of an error — the callback reports `true` only
after the witness has re-read both paths as absent.

Nothing here reads, copies or exports profile content. The marker records a
directory base name and a version integer, and no more.

## Rebase risk

**Medium to high.** This is the largest single-file delta in the queue: a small
upstream utility file roughly triples in size, and three of its existing
functions change behaviour rather than merely gaining a sibling. A rebase that
touches `kProfilesDeleted`, `GetUserCacheDirectory`, the ephemeral or omitted
semantics of `ProfileAttributesEntry`, or the desktop deletion path in
`delete_profile_helper.cc` requires all three of those behaviour changes to be
re-argued, not just re-applied. Patch 0049 is the caller and moves with it.

**Retirement:** upstream gains a profile deletion that reports whether the
directory is gone and a durable record a caller can own, or this remains a
permanent fork seam reviewed at each milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06 from the
`taffy/patched` commit carrying `Taffy-Patch: 0048`. A `dev-x64` build was
running when this specification was written and had not yet been confirmed.
None of this has run on a device, and none of the POSIX behaviour it depends
on — `lstat` absence, held-parent `fsync`, device and inode re-identification —
has been observed on Android's filesystem. The regressions that exercise the
new interface live in patch 0049's file and have not been compiled; see that
specification's verification list for why.

## Verify at export

1. Both files compile, and no `//taffy` symbol appears in either.
2. A dedicated deletion of a generated regular profile reports `true` and both
   the profile and its derived cache directory are absent afterwards, while a
   sibling file in the cache root survives.
3. A profile whose base name is not `Profile <n>`, a path not directly under
   `DIR_USER_DATA`, and a cache mapping that resolves to the shared cache root
   are each refused before anything is removed.
4. A missing cache parent leaves the profile directory intact, clears the
   volatile mark, and retains the durable marker.
5. `NukeDeletedProfilesFromDisk` sweeps `MARKED` only, and leaves a dedicated
   marker in `kProfilesDeleted`.
6. A malformed dedicated marker is retained and decodes to no path.
7. The existing desktop path through `MarkProfileDirectoryForDeletion` still
   marks, still appends its legacy base-name value, and now cancels rather than
   marking when no attributes entry exists.
8. `Profile.NukeFromDisk.Result` still records one outcome per attempt with the
   new witness in the success condition.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the two upstream files and commit with `Taffy-Patch: 0048`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
