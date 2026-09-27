# 0016 — Override Google-bound pref defaults

**Status:** `[Current]` — applied 2026-08-18 with the runtime half of
decision 0019; see `docs/decisions/0019-google-service-posture.md`
**Needed by:** WP-M1-01 and decision 0019 — two services the posture turns
off are pref-gated rather than feature-gated (password leak detection, the
Feed's snippet visibility), and their upstream defaults are on
**Estimated size:** ~5 modified upstream lines, 1–2 files (an include, a
call, and a dependency edge if the pref registrar's target lacks one)

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/profiles/pref_service_builder_utils.cc` |
| Symbol | `RegisterProfilePrefs` |
| File | `//chrome/browser/BUILD.gn` |
| Symbol | the browser target's deps list (only if not already carried by patch 0015) |

## The change

One call at the end of profile-pref registration:

```cpp
taffy::OverrideProfilePrefDefaults(pref_registry);
```

plus its include. The call site is `pref_service_builder_utils.cc`, not
`browser_prefs.cc`, and the distinction was learned from a startup crash:
prefs registered by keyed-service factories (`signin.allowed` among them)
are added by the two dependency managers *after* `browser_prefs.cc`'s list
finishes, so a default set at the end of `browser_prefs.cc` DCHECK-fails on
any factory-registered pref. `RegisterProfilePrefs` in
`pref_service_builder_utils.cc` is the one function that runs the static
list and then both dependency managers, on every profile path, so its end
is the first point where every profile pref exists. Everything the call
does lives downstream in
`taffy-core/browser/feature_posture.cc` (same target
as patch 0015): `PrefRegistry::SetDefaultPrefValue` for each pref decision
0019 assigns the posture `disabled (runtime)` — password leak detection off,
Feed snippets off. Changing a *default* rather than forcing a value keeps
every pref honest: the user can still turn the feature on, the pref reads as
user-set when they do, and enterprise policy behaves unchanged.

## Why the overlay cannot host it

Pref defaults must be set on the registry during registration, inside a
function upstream owns. Registering the same pref again from downstream code
is a CHECK failure, and re-writing values after registration would stamp a
user-visible "set by user" state onto profiles that never chose it.

## Rebase risk

**Low.** `RegisterProfilePrefs` in `pref_service_builder_utils.cc` is a
short, stable function; the call sits after the dependency-manager pass and
three-way merges cleanly. If an upstream pref in the downstream list is
renamed, the overlay's unit test fails on the stale name before the build
does; if one moves to a registrar that runs even later, the DCHECK at
`pref_registry.cc` names it on first launch of a debug build.

**Retirement:** shrinks entry by entry as OD-080 lands replacements or the
product replaces the surfaces; the seam itself is permanent and reviewed at
each milestone rebase.

## Verify at export

1. Whether `pref_service_builder_utils.cc` compiles into a target already
   carrying the patch 0015 deps edge — if so, this patch is include + call
   only.
2. That `SetDefaultPrefValue` accepts a post-registration default change in
   the same registration pass at the pin (it is designed for exactly this).

## Regenerating the patch

```bash
./tools/chromium/sync
# edit pref_service_builder_utils.cc (and chrome/browser/BUILD.gn if
# needed) in the checkout; commit with the Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
