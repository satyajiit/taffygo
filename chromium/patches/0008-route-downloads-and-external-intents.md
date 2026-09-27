# 0008 — Refuse unprivileged task-action downloads

**Status:** `[Current]` — exported against Chromium 152.0.7977.42. Commit
`faa85b0949559` on `taffy/patched` carries the upstream half and its
`Taffy-Patch: 0008` trailer, and the queue now carries the matching `.patch`
file beside this specification. The enforcement semantics
are no longer the open part of OD-056: decisions
0089
and
0090
settled them. OD-056 remains open for its independent penetration-test and
corpus evidence.
**Needed by:** WP-M1-02, CAP-AG-005, `browser.download.start`, and the M1
parity rows for downloads and external links
**Estimated size:** 12 modified upstream lines (all additions), 1 file

## Upstream file and symbol

| | |
|---|---|
| File | `//chrome/browser/download/chrome_download_manager_delegate.cc` |
| Symbol | `ChromeDownloadManagerDelegate::CheckDownloadAllowed` |

## The change

The one upstream call site covers the download path that has no downstream
refusal point: a direct renderer request. After Chromium resolves the
`WebContents`, and before its ordinary policy and permission checks, the
delegate asks:

```cpp
if (content_initiated &&
    taffy::ShouldRefuseContentInitiatedDownload(web_contents)) {
  OnCheckDownloadAllowedFailed(std::move(check_download_allowed_cb));
  return;
}
```

The downstream helper reads one content-free fact from the exact tab: whether
its action dispatcher has an unsettled assistant dispatch. If none is open,
the request proceeds through Chromium unchanged. If one is open, the helper
evaluates an assistant-attributed request with no `StartDownload` lease or
capability through the single `DownloadIntentRouter`; the router refuses it.
The patch therefore adds no second policy table and can only stop a request.

The other paths deliberately do not cross this check:

- **Navigation downloads** are bound when Chromium creates their
  `NavigationHandle`, not when response timing eventually reveals a download.
  `TaskNavigationThrottle` refuses a task-authorized Navigate response that
  becomes a transfer, and `TaskActionDownloadThrottle` refuses a navigation
  created during another assistant action if its response becomes a transfer.
  The existing patch-0038 throttle registry is already the upstream funnel.
- **A dedicated task download** is admitted first by
  `DownloadIntentRouter`, with the exact task, actor lease, spendable
  `StartDownload` capability and durable effect identity. Only then does
  `StartTaskDownload` call Chromium's `DownloadUrl`; Chromium marks that call
  `content_initiated == false`, so it does not get refused a second time by
  this patch.
- **A person's browser command**, including the download-link context menu,
  also enters with `content_initiated == false` and keeps Chromium's ordinary
  behavior. It does not inherit assistant authority.

The attribution window is bounded by the action attempt. A human interaction,
the next dispatch, or the exact action's terminal result closes it. A late
terminal callback for an older action cannot close a newer action's window.

## External applications need no upstream edit

The original specification guessed that an Android intent-helper call site
would need a second patch half. Reading and building the pinned tree disproved
that guess. Every product tab receives `TaffyExternalNavigationHandler` from
`TaffyTabDelegateFactory`; `TaffyExternalAppHandoff` sanitizes the eligible
regular-tab request and invokes Android's chooser. The product does not import
Chrome's permissive delegate. `TaffyExternalAppHandoffTest` and
`TaffyExternalNavigationTest` cover that downstream boundary, so adding an
upstream call would duplicate the decision.

## Why the overlay cannot host the remaining call site

The direct renderer path has no navigation handle and no downstream observer
that can stop it. By the time a `DownloadItem` observer runs, the transfer has
already begun. `CheckDownloadAllowed` is the first browser-owned decision that
has both the exact `WebContents` and Chromium's `content_initiated` fact, so
the twelve-line upstream call is the narrow enforcement seam.

The enforcement still leaves Chromium in charge of every ordinary check. An
allow answer means only “continue”: it bypasses no file chooser, permission
prompt, Safe Browsing decision, enterprise rule, insecure-download warning, or
TLS interstitial.

## Rebase and retirement

**Rebase risk: Low to medium.** One include and one refusal block sit inside a
long-lived delegate method. Upstream may move that method or rename its
failure callback, but the downstream contract remains one boolean query. A
missed alternate direct renderer entry point is the meaningful failure mode,
which is why browser-level coverage is required in addition to unit tests.

**Retirement:** permanent unless upstream supplies an embedder refusal
callback carrying both `WebContents` and the content-initiated classification.
Review the call site at every milestone rebase.

## Verification

The focused deterministic coverage is:

```bash
./tools/chromium/build --profile diag-sanitizer-x64 taffy_unittests
./tools/chromium/test --profile diag-sanitizer-x64 taffy_unittests -- \
  --gtest_filter='TaskNavigationThrottleTest.*:TaffyBrowserEffectSourceAttributionTest.*:TaffyM1SeamsTest.*'
```

The browser-level suite must additionally prove all four entry shapes from a
real page: an authorized Navigate response cannot turn into a download, a
direct renderer download during another action is refused, a dedicated
`StartDownload` reaches Chromium after authority is spent, and an ordinary
human download still works. No result is claimed by this specification until
those commands and the phone run are recorded in the verification report.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06, from the
`Taffy-Patch: 0008` commit on `taffy/patched`. The debt this section used to
describe — a commit that existed on the branch while the queue held no
`.patch` file for it, because unrelated live checkout edits kept the exporter
from taking a clean snapshot — was retired that day: those edits were
themselves committed, as entries 0045 to 0049, and the exporter rewrote the
queue from the branch. The exporter refuses a branch and a queue that have
diverged, and `./tools/check fast --only chromium` reads the result together
with the fork-debt figures.

The export changed nothing about the evidence: the verification section above
still owns what is owed, and none of it is claimed by this specification.

Patch files are generated only by `export-patches`; they are never edited by
hand.
