# 0047 — Register TaffyGo's Local State preferences

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** The browser-owned restore lifecycle of decision 0122
**Estimated size:** ~2 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/prefs/browser_prefs.cc` |
| Symbol | `RegisterLocalState(PrefRegistrySimple* registry)` |
| Symbol | the file's include block |

## The change

Add one include of `taffy/browser/application_preferences.h` and one call to
`taffy::application_preferences::RegisterLocalStatePreferences(registry)`
inside `RegisterLocalState()`, in the alphabetically ordered run of
browser-wide registrations between `SystemNetworkContextManager::RegisterPrefs`
and `tracing::RegisterPrefs`.

That call registers exactly two values in Chromium's Local State:

- `taffy.backup.installation_id`, a string, empty by default;
- `taffy.backup.restore_profile_reservations`, a dictionary.

The overlay owns both names, their defaults, their validation and every reader
and writer. Upstream gains one call and knows nothing about what the values
mean.

## Why the overlay cannot host it

Chromium's Local State registry is closed before its `PrefService` is
constructed, and `RegisterLocalState()` is the single moment at which a
browser-wide preference can be added. A `PrefService` read or write of an
unregistered path is a `CHECK` failure, not a default — so a profile-keyed
factory, a service constructor, or anything else the overlay can reach on its
own runs too late by construction.

The values themselves must be browser-wide rather than profile-scoped. Both
describe state that has to survive the destruction of a profile: one is the
installation's own identity, and the other is the browser's physical custody
record for a restore profile that may not be visible yet. A profile preference
would be deleted by the very operation it exists to make recoverable.

## Safety boundary

Neither value carries archive material, page content, a URL, a credential or an
account identifier, and neither grants authority. The installation id is one
non-secret identity for this installed application, outside every profile,
account and sync replica; the backup manifest carries it as source-installation
metadata only. The reservation register records physical custody — which
directory is reserved, and whether a profile has been created in it — and
mints no stage, commit, publish or delete authority; that authority is minted
consumptively elsewhere, and is deliberately absent from this register.

Registration is not a data path. Nothing here writes a value, and a malformed
stored value is treated by the overlay's reader as a quarantine failure rather
than as an empty register, so corruption fails closed rather than silently
resetting custody.

## Rebase risk

**Low.** Two lines in a long, mechanically ordered list. The realistic failure
is not a conflict but a silent drop: if a rebase loses the call, every read of
the two paths becomes a `CHECK` failure at first use, which is loud, and if a
rebase reorders the list the call still runs. The include's dependency edge is
the part worth watching — see the first verification item below.

**Retirement:** upstream gains an embedder hook for browser-wide preference
registration, or this remains a permanent two-line fork seam reviewed at each
milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06 from the
`taffy/patched` commit carrying `Taffy-Patch: 0047`. A `dev-x64` build was
running when this specification was written and had not yet been confirmed. No
device has run this code, so the claim "the two preferences are registered
before first use" is a reading of the call site rather than an observation.

## Verify at export

1. `browser_prefs.cc` is compiled into `//chrome/browser/prefs:impl`, whose
   declared dependencies do not name `//taffy/browser` — the existing edge
   patches 0006 and 0032 rely on is on `//chrome/browser`'s own
   `source_set("browser")`. Confirm the new include resolves for the built
   profile, and add the missing edge in this patch if it does not.
2. Registration happens exactly once, before the browser's `PrefService` is
   created.
3. A first start with no Local State produces an empty string and an empty
   dictionary rather than a failure.
4. Reading either path from the overlay after registration does not trip a
   `CHECK`.
5. No profile preference file gains either name.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the upstream registration point and commit with `Taffy-Patch: 0047`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
