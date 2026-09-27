# 0038 — Enforce exact task navigation before the network

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** CAP-PI-009 and CAP-AG-005; an allowed observed-link action must
not escape its exact browser-issued destination through a redirect
**Estimated size:** ~16 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| Files | `//chrome/browser/chrome_content_browser_client.cc`; `//chrome/browser/renderer_host/chrome_navigation_ui_data.h` and `.cc` |
| Symbols | `ChromeContentBrowserClient::CreateThrottlesForNavigation(...)`; `ChromeNavigationUIData::Clone()` |

## The gap this closes

The observed-link tool re-resolves a browser-issued opaque handle immediately
before dispatch and admits one exact normalized HTTP or HTTPS address. Its
allowed redirect set is empty. An after-navigation verifier can notice that a
server escaped that address, but by then the browser has already sent the
request, including the profile's cookies. Detection after disclosure is not a
network boundary.

The first downstream draft attached authority to the `NavigationHandle`
returned by `LoadURLWithParams`. Reading the pinned content path showed that
this is too late: `NavigateWithoutEntry` calls `Navigator::Navigate`
synchronously, and that path can begin the request and register start
throttles before `LoadURLWithParams` returns its weak handle.

## The change

Chrome's own pre-start carrier gains one optional exact-destination string and
copies it in `Clone()`. The task action creates that
`ChromeNavigationUIData` before calling `LoadURLWithParams`; content clones it
into the `NavigationRequest` before navigation begins. The upstream throttle
funnel then calls `TaskNavigationThrottle::MaybeCreateAndAdd`.

All policy remains downstream. The carrier can be created only for a canonical
credential-free HTTP or HTTPS address. The throttle admits the browser-started
primary request only when it still equals that address, then refuses every
redirect before the redirect target is requested. Renderer-started, rewritten,
subframe and unmarked navigations fail closed. Ordinary manual browsing gets no
task-throttle allocation.

The field is a string rather than a downstream type so Chrome's navigation
carrier does not depend on `//taffy`. The one Taffy source that creates and
reads it has a file-scoped include allowance and an explicit renderer-host
dependency.

## Why the overlay cannot host it

The overlay owns the action, authority validation and throttle. It cannot add
a value to the `NavigationUIData` object Chrome clones into a request, nor can
it enter Chrome's one navigation-throttle registration method. A tab-level
pending marker would be weaker: an intervening navigation could inherit it.

## Rebase risk

**Low to medium.** The throttle call is the standard two-line hook. The copied
carrier field must move if Chrome replaces `ChromeNavigationUIData` or its
clone path.

**Retirement:** permanent until content exposes a typed downstream navigation
capability hook that can carry the exact destination before request start.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-30 from the
`Taffy-Patch: 0038` commit on `taffy/patched`. The queue file is
`0038-carry-and-enforce-exact-task-navigation-authority.patch`.
