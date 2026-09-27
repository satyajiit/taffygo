# 0049 — Delete a marked ephemeral profile on Android

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** The browser-owned restore lifecycle of decision 0122
**Estimated size:** ~549 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/profiles/profile_manager.h` |
| Symbol | `ProfileManager::DeleteMarkedEphemeralProfileOnAndroid()` |
| File | `//chrome/browser/profiles/profile_manager.cc` |
| Symbol | `ProfileManager::DeleteMarkedEphemeralProfileOnAndroid()`, `PostMarkedProfileDeletionOnAndroid()`, `FinishMarkedProfileDeletionOnAndroid()` |
| Symbol | `CleanUpDeletedProfilesOnAndroid()`, `ProfileManager::DeleteProfileOnAndroid()` |
| File | `//chrome/browser/profiles/profile_manager_unittest.cc` |
| Symbol | five `ProfileManagerTest` regressions for the hidden-profile deletion path |

## The change

Patch 0042 gave `ProfileManager` `DeleteProfileOnAndroid`: the confirmed
deletion of a **visible** regular profile a person chose to remove, which marks
the path, waits for destruction, and hands the directory to Chromium's
bounded disk-nuke routine. This patch adds the second, narrower entry point the
restore lifecycle needs, and hardens the startup replay 0042 introduced. It is
the caller of the interface patch 0048 adds; the two move together.

**`DeleteMarkedEphemeralProfileOnAndroid`** detaches and deletes a profile that
is *already* hidden and *already* armed. It accepts only a path that has an
attributes entry, is an allowed profile path, is not the guest path, is not the
last used profile, is both ephemeral and omitted, and is at the dedicated
deletion stage — and, when the profile is loaded, only when its `ProfileInfo`
has finished creation. It notifies observers of permanent deletion, tells the
profile's `PrefService` it is being deleted from disk, queues the physical work
behind destruction through `profiles_pending_destruction_`, removes the
attributes entry, and reports through a `bool` callback. It never regularizes a
profile, never activates one, and creates nothing.

The completion path is two hops. `PostMarkedProfileDeletionOnAndroid` posts
`NukeProfileFromDiskWithResult` to a blocking, best-effort,
skip-on-shutdown task; `FinishMarkedProfileDeletionOnAndroid` reports `true`
only when that deletion succeeded **and** the exact durable marker was
completed. A false result therefore leaves the marker in place as recovery
evidence, and — because 0048 keeps the dedicated stage out of the shutdown
sweep — that retained marker is never treated as authority to retry the
physical work by itself.

**The startup replay is fenced.** `CleanUpDeletedProfilesOnAndroid` now reads
the `kProfilesDeleted` list twice. The first pass collects every dedicated
marker's decoded path and records whether any dedicated marker is malformed.
The second pass skips dedicated markers entirely, skips a legacy marker whose
path a dedicated marker also names, and — when any dedicated marker is
malformed, so no correlation is safe — skips every automatic deletion in the
pass.

**One interrupted-deletion test is corrected.** The condition 0042 used was
`!entry || (entry->IsEphemeral() && entry->IsOmitted())`. Omission is
process-local: every stored entry reloads as non-omitted, so after a restart
that condition could only be satisfied by the absent-entry half. It is now
`!entry || entry->IsEphemeral()`, which is the durable pair — the marker plus
the persisted ephemeral bit.

**`DeleteProfileOnAndroid` gains one guard.** After
`MarkProfileDirectoryForDeletion`, it now returns `false` unless the path is
actually marked. Patch 0048 makes that call able to fail and cancel, so the
0042 path must no longer assume it succeeded.

**Five regressions** are added to the Android section of
`profile_manager_unittest.cc`: the exact-marked deletion (including that a
sibling file in the cache root survives and that `kProfilesDeleted` ends
empty); refusal of a non-generated profile base name; a missing exact cache
parent leaving the profile and its durable marker intact after a shutdown
sweep; a dedicated marker not replaying at startup; and a malformed dedicated
marker being retained at startup together with the legacy marker it fences.

Most of `profile_manager.cc`'s delta is not behaviour. Fifty-four of its
fifty-eight removed lines are a single-statement `if`, `for` or `while` header
that the tree's formatter rewrote with braces, and each of those costs three
diff lines, so roughly 160 of its 262 modified lines are reformatting of code
this patch does not otherwise touch. The unit-test file carries a smaller share
of the same churn plus some line rewrapping. That is the main reason this
entry's line count is large relative to what it does — and the reason a
reviewer should read the diff by symbol rather than by size.

## Why the overlay cannot host it

Profile lifetime is `ProfileManager`'s. `profiles_info_`,
`profiles_pending_destruction_`, the attributes storage and the
observer notification for permanent deletion are private, and the ordering
between "stop using this profile" and "remove its directory" is enforced by
that class. An overlay that tried to reproduce it would hold a second,
disagreeing view of which profiles exist — and the disagreement would appear
precisely when a deletion is interrupted, which is the case this exists for.

The startup replay has the same shape. `CleanUpDeletedProfilesOnAndroid` runs
inside the `ProfileManager` constructor, before any overlay component exists,
so a marker the overlay wants fenced can only be fenced there.

## Safety boundary

This is a narrow physical continuation, not a deletion decision. Every semantic
question — whether this profile should be removed at all — is settled before
the call, by a caller that has already scheduled the path, persisted the
durable marker, drained and read back Local State, and armed the exact stage.
Seven independent preconditions are re-checked at the entry point, and any one
of them refuses.

It cannot reach a profile a person is using. The active profile is excluded by
the last-used check, the guest path by name, and a visible profile by the
required ephemeral and omitted bits. It creates no profile, exposes no profile
picker, and adds no account, sync, import or cross-profile data path.

Failure is conservative in both directions. A refusal before dispatch leaves
everything as it was; a failed physical attempt keeps the durable marker so the
state stays recoverable, while the volatile mark that would authorize a retry
is dropped. A malformed durable marker suspends automatic deletion for the
whole startup pass rather than being resolved by guessing.

## Rebase risk

**Medium to high.** The new entry point is small and sits beside 0042's, but it
depends on private ordering — pending destruction, attributes removal and the
observer notification — and on patch 0048's interface. The startup reducer now
carries two-pass fencing logic inside a constructor, and the formatting churn in
this diff will make a real upstream conflict harder to read than its size
suggests. A rebase that changes profile destruction ordering, the ephemeral or
omitted semantics, or `kProfilesDeleted` requires this patch and 0048 to be
re-reviewed together, and an Android create-restore-delete-restart device run
to be repeated.

**Retirement:** upstream supports an Android hidden-profile deletion that
reports its physical outcome, or this remains a permanent fork seam reviewed at
each milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06 from the
`taffy/patched` commit carrying `Taffy-Patch: 0049`. A `dev-x64` build was
running when this specification was written and had not yet been confirmed, and
nothing here has run on a device.

The unit suite this patch extends has **not** been compiled, and there is a
known reason to compile it before the patch is claimed complete: two of the
five new cases call `CleanUpDeletedProfilesOnAndroid` directly, and in the
exported tree that function has internal linkage — it is defined in the
anonymous namespace of `profile_manager.cc` and declared in no header. As
written, those two cases cannot resolve it from a separate translation unit.
That is stated here rather than left for the next reader to discover, and it is
the first item below.

## Verify at export

1. Build the Chromium unit-test target that contains
   `profile_manager_unittest.cc`. The two startup-replay cases reach
   `CleanUpDeletedProfilesOnAndroid`, which is currently file-local; either
   give it external linkage with a declaration, or drive those cases through a
   public entry point.
2. The exact-marked deletion removes the profile and its derived cache
   directory, leaves a sibling file in the cache root, empties
   `kProfilesDeleted`, and leaves the active profile valid and still last used.
3. A profile whose base name is not a generated regular-profile name is refused
   at the marker step and nothing is written.
4. A missing exact cache parent yields `false`, leaves the directory present,
   clears the volatile stage, and leaves exactly one dedicated marker.
5. A shutdown sweep with an armed dedicated marker deletes nothing.
6. A dedicated marker does not replay at startup; a malformed one is retained
   and fences the legacy marker beside it.
7. `DeleteProfileOnAndroid` returns `false` when marking did not take, and the
   0042 create-switch-delete-restart behaviour is unchanged otherwise.
8. An Android device run of the restore lifecycle across a browser restart —
   the only evidence that the durable half of this works, and the one thing no
   host suite can supply.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three upstream files and commit with `Taffy-Patch: 0049`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
