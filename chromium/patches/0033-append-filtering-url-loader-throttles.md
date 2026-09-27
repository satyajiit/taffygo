# 0033 — Filter browser-initiated profile loads

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** CAP-BR-022 (built-in ad and tracker blocking, decision
`0076-ad-and-tracker-blocking-is-a-browser-plane.md`)
**Estimated size:** ~3 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| Files | `//chrome/browser/chrome_content_browser_client.cc` and `chrome/browser/DEPS` |
| Symbol | `ChromeContentBrowserClient::CreateURLLoaderThrottles(...)` |

## The gap this closes

The filtering plane is complete on the downstream side: the parser, the
compiled ruleset, the preference-backed posture, the throttle, the per-tab
counters and the profile-scoped lifecycle service all exist under
`//taffy/components/filtering`, and the browser host owns a
`FilteringRulesetService` per profile. What no downstream file can do is put
a throttle on a browser-initiated request: these
`blink::URLLoaderThrottle`s are created in `CreateURLLoaderThrottles`, and that
funnel is upstream code. Without this edit frame navigations and the other
browser-initiated classes load unjudged. Renderer-originated image, script,
fetch and XHR resources use patch 0035's renderer loader adapter instead.

## The change

One include:

```cpp
#include "taffy/browser/filtering_throttles.h"
```

and one call at the end of `CreateURLLoaderThrottles`, after the signin
throttle and before `return result;`:

```cpp
taffy::AppendFilteringThrottles(request, browser_context, wc_getter, result);
```

`//taffy/browser` is already in `//chrome/browser`'s `deps` from patch
0006, so this patch adds no build edge of its own. `chrome/browser/DEPS`
had no matching include rule — the renderer and utility patches each added
their own scoped `+taffy/...` line — so the exported diff also adds
`+taffy/browser` there. That is one extra upstream line, in a second file,
the original estimate did not count.

## Why an unconditional call, appended last

The upstream edit carries no policy, exactly as patch 0023's does not: every
refusal lives in `taffy::AppendFilteringThrottles`
(`taffy-core/browser/filtering_throttles.cc`), which the fork owns and
unit-tests. A profile whose core-service manager has not been constructed, a
posture with blocking off, a document host under a site exception, an
allowlisted document, or a service with no compiled ruleset each cost one
early return downstream, and the upstream line stays a single call that is
correct for all of them. `GetForProfileIfExists` is deliberate — a request
must never be the event that constructs a profile's
core-service manager.

Order matters only in that the filtering throttle must not preempt Safe
Browsing: appending after the existing throttles leaves every security
throttle ahead of this one, and a request both would touch is cancelled by
whichever cancels first, which is the correct union.

`CreateURLLoaderThrottlesForKeepAlive` is deliberately not edited: it serves
keep-alive requests that already left their page, it receives no
`wc_getter`, and a blocked keep-alive would be attribution with no tab to
attribute to.

## How this was exported

Originally exported by `./tools/chromium/export-patches` on 2026-08-29 from
the `Taffy-Patch: 0033` commit on `taffy/patched`. Its scope and subject were
corrected on 2026-09-04 after the device vertical proved that this hook sees
browser-initiated loads, not renderer-originated resources. The behavior and
upstream lines are unchanged: the queue's diff body is byte-identical to the
recorded `c917abc9182f` commit. Later patches add adjacent includes, so this
early patch is validated at its queue position rather than reversed by itself
from the fully patched checkout.
