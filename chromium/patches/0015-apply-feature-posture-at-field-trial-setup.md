# 0015 — Apply the TaffyGo feature posture at field-trial setup

**Status:** `[Current]` — applied 2026-08-18 with the runtime half of
decision 0019; see `docs/decisions/0019-google-service-posture.md`
**Needed by:** WP-M1-01 and decision 0019 — the services that still transmit
in a keyless unbranded build (network time, Feed, autofill crowdsourcing,
translate ranker, optimization-guide belt-and-braces) are feature-gated, and
features must be overridden before the feature list is frozen
**Estimated size:** ~5 modified upstream lines, 2 files (an include, a call,
a dependency edge)

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/chrome_browser_field_trials.cc` |
| Symbol | `ChromeBrowserFieldTrials::RegisterFeatureOverrides` |
| File | `//chrome/browser/BUILD.gn` |
| Symbol | the browser target's deps list |

## The change

One call at the top of `RegisterFeatureOverrides`:

```cpp
taffy::ApplyFeaturePosture(feature_list);
```

plus its include and a deps entry on
`//taffy/browser:feature_posture`. The include is rooted at
`taffy/browser/feature_posture.h`, so no retired component mount is needed.

Everything the call does lives downstream in
`taffy-core/browser/feature_posture.cc`: the list of
`base::Feature` overrides that decision 0019 assigns the posture
`disabled (runtime)`, each entry carrying a citation comment naming the
upstream symbol it silences. The list is unit-tested on the host.

The seam is deliberate: `RegisterFeatureOverrides` is the hook
`variations::PlatformFieldTrials` provides for exactly this, and it runs
after the command line and seed are read but before
`base::FeatureList::SetInstance` freezes the list. Overrides registered here
are first-wins against later registrations — which is also why the
compiled-in field-trial testing config must be disabled by the GN argument
`disable_fieldtrial_testing_config` (committed args fragment): that config
registers *earlier* and would beat this list.

## Why the overlay cannot host it

Feature overrides must be registered on the not-yet-frozen `FeatureList`
during a startup sequence upstream owns end to end. The hook is a virtual on
an upstream class constructed by upstream code; there is no downstream
registration point, and after `SetInstance` the list is immutable.

## Rebase risk

**Low.** The same shape as patch 0014: one call on a stable, purpose-built
lifecycle hook, no ordering relationship with upstream's own overrides in the
same function. If the hook is renamed or the override semantics change, the
patch conflicts loudly and re-lands in whatever replaced it.

**Retirement:** permanent, reviewed at each milestone rebase. Entries in the
downstream list retire individually as upstream removes the features they
silence or as TaffyGo replaces the services (OD-079, OD-080).

## Verify at export

1. That `RegisterFeatureOverrides` still runs before
   `base::FeatureList::SetInstance` in
   `variations_field_trial_creator.cc`.
2. That override registration is first-wins (`base/feature_list.cc`,
   `RegisterOverride`).
3. That the committed profiles carry `disable_fieldtrial_testing_config =
   true`, without which this patch is partially ineffective.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit chrome_browser_field_trials.cc and chrome/browser/BUILD.gn in the
# checkout; commit with the Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
