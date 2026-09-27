# 0032 — Register the TaffyGo profile-keyed service before profiles

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** The per-profile sandboxed core-service manager
**Estimated size:** three upstream lines in two browser startup files

## Upstream file and symbol

| | |
|---|---|
| Files | `//chrome/browser/profiles/BUILD.gn` and `chrome_browser_main_extra_parts_profiles.cc` |
| Symbols | `profiles_extra_parts_impl` and `ChromeBrowserMainExtraPartsProfiles::EnsureBrowserContextKeyedServiceFactoriesBuilt()` |

## The change

Include `//taffy/browser/core_service_manager_factory.h` and call
`taffy::CoreServiceManagerFactory::GetInstance()` in Chromium's authoritative
profile-factory registration list. This constructs only the factory and its
dependency declaration; each regular or private profile still owns its own
lazy `CoreServiceManager` instance.

The startup target declares `//taffy/browser` as the exact implementation
dependency for that include. The dependency already enters the browser product
through the same downstream component; this edge records ownership for GN and
does not introduce a second runtime.

## Why the overlay cannot host the complete graph

Chromium closes the browser-context factory dependency graph before creating a
profile. A factory first touched by the Android tab or Core API path is too
late, and Chromium deliberately aborts rather than accept a service whose
ordering was absent from that graph. The overlay cannot append to the closed
registration list through a native dependency alone.

Calling the factory from a tab, activity, or profile observer earlier by
accident would encode startup order in a consumer and would remain vulnerable
to a restored-tab path winning the race. The authoritative registration
function is the deterministic seam Chromium requires.

## Rebase risk

**Low.** The dependency, include, and factory call are each one line. A rename
or movement of Chromium's registration function conflicts during patch
application or compilation.

## Retirement

Permanent while Chromium owns a closed, central profile-factory registration
list. Retire it if upstream provides a downstream registration hook before the
dependency graph closes.

## Verification

Run `gn check` for `//chrome/browser/profiles:profiles_extra_parts_impl`, build
`//taffy/app/android:taffy_public_apk`, install it on an Android device,
and restore or create a tab. The browser process must remain alive and the
crash buffer must contain no late-factory registration DCHECK. Browser tests
must also resolve independent regular and private-profile managers.
