# 0023 — Attach page intelligence to a tab's WebContents

**Status:** `[Current]` — committed on `taffy/patched` and exported by
`./tools/chromium/export-patches` on 2026-08-21 as
`0023-attach-page-intelligence-to-a-tab-s-WebContents.patch`.
**Needed by:** WP-M2-02 — `PageIntelligenceBroker` has no production caller, so
no shipping build has ever bound a renderer endpoint
**Estimated size:** ~4 modified upstream lines, 1 file

## Upstream files and symbols

| | | |
|---|---|---|
| File | `//chrome/browser/ui/tab_helpers.cc` | |
| Symbol | `TabHelpers::AttachTabHelpers(content::WebContents*, bool)` | |

## The gap this closes

Every `PageIntelligenceBroker::CreateForWebContents` call in the tree is in a
test:

```bash
grep -rn CreateForWebContents taffy-core/
```

returns hits only under `*_browsertest.cc` and `test/support/`. The broker is a
`content::WebContentsUserData` and a `content::WebContentsObserver`: it mints a
`PageEpoch` at `DidFinishNavigation`, assembles frame identity from Chromium's
own frame tree, and creates the `FrameObservationEndpoint` that acquires the
renderer's channel-associated `PageIntelligence` remote. Nothing in a shipping
build constructs one, so none of that happens, and patch 0007's renderer
observer — even once it is constructed — serves an interface nobody asks for.

This is the browser half of the same missing pair. Neither half alone is
observable.

## The change

One call, beside the other per-tab helpers:

```cpp
taffy::TaffyPageIntelligenceHost::AttachIfEligible(web_contents);
```

plus its include. The scoped `taffy/browser` include is accepted by the
browser's existing downstream rule, so no DEPS edit is needed, and
`//taffy/browser` is already in `//chrome/browser`'s `deps` from
patch 0006 — this patch adds no build edge of its own.

## Why `AttachTabHelpers` and not only in TaffyGo's own activity

`TaffyBrowserActivity` owns one window-lifetime Compose projection, while a
tab and its `WebContents` have an independent profile-owned lifetime. Tabs may
be restored before an activity finishes composing, created by browser-owned
paths, or moved between windows. Attaching intelligence only from an activity
would therefore miss valid tabs and would incorrectly make a tab service a
child of the current window.

`TabHelpers::AttachTabHelpers` is the one funnel every real tab already passes
through, on this platform and on the platform TaffyGo is heading for:

| Caller | File |
|---|---|
| `TabAndroid::InitWebContents` | `chrome/browser/android/tab_android.cc` |
| `TabWebContentsDelegateAndroid` (`AddNewContents`) | `chrome/browser/android/tab_web_contents_delegate_android.cc` |
| `ChromeThinWebViewInitializer` | `chrome/browser/android/thin_webview/chrome_thin_webview_initializer.cc` |

TaffyGo's tab assembly constructs `TabAndroid`, so it reaches this call site
without introducing a second attachment path.

The third caller is why the upstream line names
`TaffyPageIntelligenceHost::AttachIfEligible` rather than
`PageIntelligenceBroker::CreateForWebContents`. A thin webview is not a tab a
person browses in, and the decision about which `WebContents` TaffyGo may
observe is a policy decision. It belongs in
`//taffy/browser/taffy_page_intelligence_host.cc`, which the fork
owns and unit-tests, and not in an upstream `if` that no downstream test
covers. The upstream edit is an unconditional call; every refusal is
downstream of it.

## Why the overlay cannot host it

`content` offers no downstream hook that fires when a `WebContents` becomes a
tab. `WebContentsObserver` is per-instance and needs an instance to observe;
`WebContentsUserData` needs a caller. `AttachTabHelpers` is Chromium's own
answer to this question for every other per-tab component in the browser, and
an overlay component has nothing to attach to before it runs.

## Rebase risk

**Low.** One call in a list of roughly a hundred identical calls, in a
function that grows by addition. The risk is not conflict but a second tab
path appearing that does not call `AttachTabHelpers`; that failure is visible
rather than silent, because a tab with no broker refuses every observation
with a result code rather than answering wrongly.

**Retirement:** replaced, not retired, when TaffyGo owns tab creation — the
call moves into TaffyGo's own tab assembly and the upstream line goes away.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the file in the checkout; commit with an owner and a reason, and the
# trailer `Taffy-Patch: 0023`
./tools/chromium/export-patches
./tools/check fast
```


## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-21, from the
`Taffy-Patch: 0023` commit on `taffy/patched`. The hand-written `git diff`
export this section used to describe, and the debt it recorded — a queue
file whose lines existed only as uncommitted modifications in the checkout —
were retired the same day: the tab-helpers edit was committed with
its trailer and the exporter rewrote the queue from it (the file is now
`0023-attach-page-intelligence-to-a-tab-s-WebContents.patch`, named from
the commit subject).
The branch and the queue agree, and `./tools/check fast --only chromium`
verifies that agreement together with the fork-debt figures.
