# 0020 — No /ListAccounts when signin is disallowed

**Status:** `[Current]` — applied 2026-08-18; closes the ListAccounts
residual recorded in `docs/decisions/0019-google-service-posture.md`
**Needed by:** decision 0019's signin row — the account plane is TaffyGo's
own (Supabase, decision 0014's app layer), so the engine's Google session
machinery must stay quiet
**Estimated size:** ~6 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//components/signin/internal/identity_manager/gaia_cookie_manager_service.cc` |
| Symbol | `GaiaCookieManagerService::TriggerListAccounts` |

## The change

One early return at the top of `TriggerListAccounts()`: when the profile's
`prefs::kSigninAllowed` is false, no `/ListAccounts` request is queued and
the cookie jar simply stays stale-and-empty. Plus the
`signin_pref_names.h` include.

## Why this seam

Measured on device (2026-08-18, fresh profile, two idle minutes): with
`kSigninAllowed` defaulted false by patch 0016 and
`AvoidAutoTriggerListAccountsOnStale` enabled by patch 0015, the browser
still sent eight `gaia_auth_list_accounts` requests to
`accounts.google.com/ListAccounts`. The trigger is the cookie listener:
the default search engine's own responses set `.google.com` cookies, every
such change marks the account list stale, and `OnCookieChange` calls
`TriggerListAccounts()` unconditionally — the pref and the feature gate
other paths, not this one. `AreSigninCookiesAllowed()` gates only
`SetAccountsInCookies` at this milestone, and it reads cookie content
settings, which correctly stay open.

`TriggerListAccounts()` is the one funnel every trigger path shares
(cookie change, forced processing, stale-refresh, `ListAccounts()`
itself), so one gate there is complete. Failing closed is the honest
state: callers observe an empty, stale cookie jar, which is exactly what a
profile that cannot sign in has.

## Why the overlay cannot host it

The trigger sits inside `GaiaCookieManagerService`, upstream-owned, below
any embedder seam; there is no delegate or feature that reaches it.

## Rebase risk

**Low.** The function is small and stable; the gate is its first
statement. If upstream renames the pref the include breaks loudly. One
caveat for milestone rebases: upstream signin unit tests that drive
`TriggerListAccounts` through a `TestSigninClient` must have
`kSigninAllowed` registered — every real embedder registers it via
`PrimaryAccountManager::RegisterProfilePrefs`, but a minimal test fixture
may not. TaffyGo's own suites are unaffected.

## Verify at export

1. Fresh-profile netlog capture shows zero `accounts.google.com`
   requests while the DSE traffic still sets `.google.com` cookies.
2. `taffy_unittests` still 441 green on device.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit gaia_cookie_manager_service.cc in the checkout; commit with the
# Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
