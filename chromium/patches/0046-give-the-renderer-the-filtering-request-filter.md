# 0046 — Give the renderer the filtering request filter

**Status:** `[Current]` — exported against Chromium 152.0.7977.42
**Needed by:** CAP-BR-022 and decision 0086
**Estimated size:** ~47 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/chrome_content_browser_client_receiver_bindings.cc` |
| Symbol | `ChromeContentBrowserClient::ExposeInterfacesToRenderer()` |
| File | `//chrome/renderer/url_loader_throttle_provider_impl.h` |
| Symbol | `URLLoaderThrottleProviderImpl` constructor, `CloneFilteringRequestFilterPendingRemote()`, `pending_filtering_request_filter_`, `filtering_request_filter_` |
| File | `//chrome/renderer/url_loader_throttle_provider_impl.cc` |
| Symbol | `URLLoaderThrottleProviderImpl::Create()`, the constructor, `Clone()`, `CreateThrottles()`, `CloneFilteringRequestFilterPendingRemote()` |

## The change

Patch 0033 filters browser-initiated profile loads. This patch supplies the
other half: the resource loads a renderer starts, which never reach that
throttle.

In the browser, register `taffy::filtering::mojom::RequestFilter` in the
process-scoped binder registry that `ExposeInterfacesToRenderer` populates,
bound to `taffy::BindFilteringRequestFilter` with the requesting
`RenderProcessHost`'s id already applied and dispatched on the UI task runner
the same function already builds for its other bindings.

In the renderer, carry one pending remote of that interface from the
thread-safe browser interface broker through
`URLLoaderThrottleProviderImpl::Create()`, the constructor, and `Clone()`, in
the same positional shape as the existing safe-browsing remote.
`CloneFilteringRequestFilterPendingRemote()` binds the stored pending remote
once and then mints a fresh pipe per caller through the interface's own
`Clone` method, so a throttle provider cloned onto a loader sequence never
shares a bound `Remote`. `CreateThrottles()` appends one
`taffy::filtering::FilteringURLLoaderThrottle` for every non-frame resource
that carries a `LocalFrameToken`, for every provider type, immediately after
the safe-browsing throttle and before the no-state-prefetch one.

No build-graph edit is needed. `//chrome/renderer` already depends on
`//taffy/components/filtering/renderer:renderer`, which carries the mojom
target in `public_deps`, and `//chrome/browser`'s `source_set("browser")`
already depends on `//taffy/browser`; patches 0035 and 0036 added both edges.

This is not patch 0036 with a second interface. 0036 binds
`CosmeticFilterHost` in the **associated** registry, per `RenderFrameHost`,
in the same upstream file; this interface is non-associated and
process-scoped, so it is registered in a different function on a different
registry with a different lifetime. The two call sites do sit in one file, and
a rebase that moves either will land on both.

## Why the overlay cannot host it

`URLLoaderThrottleProviderImpl` is the only place Chrome's renderer answers
Blink's request for embedder throttles. Blink calls the provider, the provider
is constructed and cloned by `ChromeContentRendererClient`, and there is no
downstream hook that can add a throttle to that vector or add a member to the
provider that survives `Clone()`. A `//taffy` render-frame observer, which is
what patch 0035 owns, sees frames rather than resource loads and cannot defer a
loader.

The browser half is the same shape. The process-scoped binder registry for
renderer-facing interfaces is built by `ChromeContentBrowserClient`, and a
component cannot add itself to a registry it is not handed.

## Safety boundary

The renderer asks one question — a frame token, a URL and a request
destination — and learns one boolean. The ruleset, the filtering posture, the
per-site exceptions, the owning profile, the document attribution and the
blocked-count publication all stay in the browser implementation; none of them
crosses the pipe, and the interface has no method that could return them.

The frame token the renderer supplies is a claim, not authority: the browser
implementation resolves it back to a live frame and its profile, and a token
that does not resolve cannot widen anything, because the only reply is a
boolean.

Document requests and non-HTTP schemes are never asked about, and frame
resources are skipped in the renderer because the browser already filters
those loads through patch 0033 — asking twice would double-count a block. When
the pipe is unavailable or disconnects, the throttle resumes any deferred load
and stops asking: filtering fails open, matching the browser path with no
ruleset loaded. That is a deliberate choice and it means a broken binder
weakens blocking rather than breaking browsing; it does not weaken any
security boundary, because nothing is authorized by this interface.

## Rebase risk

**Low.** All three edits sit beside an existing member of the same shape — the
safe-browsing remote in the renderer, and the call-stack-profile binding in the
browser. A conflict is most likely if upstream changes the throttle provider's
constructor signature or the conditions under which it is cloned, and that
conflict is loud rather than silent.

**Retirement:** upstream exposes an embedder hook that can contribute a
renderer `URLLoaderThrottle` and a process-scoped interface without editing
these files, or this remains a permanent narrow fork seam reviewed at each
milestone rebase.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-09-06 from the
`taffy/patched` commit carrying `Taffy-Patch: 0046`. A `dev-x64` build was
running when this specification was written and had not yet been confirmed;
nothing here has been run on a device, and no blocked count has ever been
observed on a phone. The renderer-originated block is the half of CAP-BR-022
that only a device run can prove, and this patch does not prove it.

## Verify at export

1. The three upstream files compile, and `gn check` accepts the new `//taffy`
   includes in `//chrome/renderer` and `//chrome/browser`.
2. A subresource request from a live frame is deferred exactly once, and a
   blocked answer cancels it with `net::ERR_BLOCKED_BY_CLIENT`.
3. A document request and a non-HTTP scheme produce no call at all.
4. A cloned throttle provider on a loader sequence gets its own pipe; no bound
   `Remote` crosses a sequence.
5. Closing the browser side of the pipe resumes a deferred load rather than
   stranding it, and no further checks are attempted on that throttle.
6. The per-frame `CosmeticFilterHost` binding patch 0036 owns still works;
   the two registrations are independent.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three upstream files and commit with `Taffy-Patch: 0046`
./tools/chromium/export-patches
./tools/check fast --only chromium
```
