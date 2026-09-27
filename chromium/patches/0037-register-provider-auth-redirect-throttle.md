# 0037 — Register the provider sign-in redirect throttle

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** decision
0095
section 2 (the redirect is intercepted, not listened for)
**Estimated size:** ~2 modified upstream lines, 1 file

## Upstream file and symbol

| | |
|---|---|
| File | `//chrome/browser/chrome_content_browser_client.cc` |
| Symbol | `ChromeContentBrowserClient::CreateThrottlesForNavigation(...)` |

## The gap this closes

Decision 0095 section 2 settles how an authorization code gets back into this
product, and the answer is that TaffyGo is the browser: a navigation to the
borrowed client's registered redirect address is intercepted before it
connects, the code and state are taken from it, and the navigation is
cancelled. No loopback socket is opened, nothing binds a port, and the code
never reaches a page.

The downstream half already existed. `ProviderAuthRedirectThrottle` matches
only an address a running flow presented, hands the answer to the broker's one
constant-time state-check path, and cancels a claimed navigation. Without the
upstream call, that complete path was compiled and never entered, so every
provider silently fell back to hand entry.

## The change

The patch adds the downstream header and calls
`ProviderAuthRedirectThrottle::MaybeCreateAndAdd` after Chrome's own navigation
throttles. `//taffy/browser` was already a dependency and an allowed include,
so no graph or `DEPS` edit was needed.

The checkout confirmed the pinned milestone uses
`NavigationThrottleRegistry`. The downstream precondition now avoids an
allocation unless the request is a primary navigation and the profile already
has a running flow with an interceptable redirect. The address, state and code
are still checked at request time. Starting a flow later cannot make an older,
unrelated navigation its redirect.

## Why this is not a downstream file

Navigation throttles are registered in this one embedder funnel. The matching,
claim, cancellation and tests remain under `//taffy`; the upstream edit is the
smallest call that can put them on a request.

## Rebase risk

**Low.** One include and one call beside the other embedder throttles.

**Retirement:** permanent while borrowed-client provider sign-in exists.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-30 from the
`Taffy-Patch: 0037` commit on `taffy/patched`. The queue file is
`0037-register-the-provider-sign-in-redirect-throttle.patch`.
