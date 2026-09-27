# 0035 — Attach Ads and trackers to renderer frames and resource loaders

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** CAP-BR-022 (Ads and trackers, decision
`0086-the-filter-engine-is-adblock-rust.md`)
**Estimated size:** ~52 modified upstream lines, 5 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/renderer/BUILD.gn` |
| Symbol | `static_library("renderer")`'s `deps`, gaining `//taffy/components/filtering/renderer:renderer` |
| File | `//chrome/renderer/DEPS` |
| Symbol | the include-rule list, gaining `+taffy/components/filtering/renderer` |
| File | `//chrome/renderer/chrome_content_renderer_client.cc` |
| Symbol | `ChromeContentRendererClient::RenderFrameCreated(content::RenderFrame*)` |
| Files | `//chrome/renderer/url_loader_throttle_provider_impl.{cc,h}` |
| Symbol | `URLLoaderThrottleProviderImpl` construction, cloning and `CreateThrottles(...)` |

## The gap this closes

The filtering renderer module has two adapters. Cosmetics bind an associated
`CosmeticFilterHost`, insert a user-origin hide stylesheet, and pump class and
id tokens. Network filtering defers a renderer-originated resource while the
browser resolves the frame's live profile, ruleset and posture. Downstream code
can implement both adapters, but only Chrome's renderer embedder can construct
the frame observer and append a loader throttle.

## The frame change

One include:

```cpp
#include "taffy/components/filtering/renderer/cosmetic_filter_render_frame_observer.h"
```

and one construction in `RenderFrameCreated`, immediately after
`new taffy::TaffyRenderFrameObserver(render_frame);`:

```cpp
new taffy::filtering::CosmeticFilterRenderFrameObserver(render_frame);
```

`DEPS` gains `+taffy/components/filtering/renderer`, and deliberately not the
wider `+taffy` rule: the renderer process has no business reaching the
browser half. `BUILD.gn` gains `//taffy/components/filtering/renderer:renderer`.

The observer deletes itself in `OnDestruct()`, which is
`content::RenderFrameObserver`'s lifetime contract — the same idiom as
patch 0007. It is created for every frame. It does not hang off
`TaffyRenderFrameObserver` and does not reuse `DomMutationSignalSource`.

## The resource-loader change

`URLLoaderThrottleProviderImpl` obtains the process-scoped `RequestFilter`
remote from the browser interface broker and preserves the pipe across provider
clones. For a non-frame resource with a local frame token it appends
`FilteringURLLoaderThrottle` after Safe Browsing. The adapter sends only the
frame token, request URL and fetch destination, then applies the browser's one
boolean answer. Rules, preference state and counts never enter the renderer.

Frame resources stay on patch 0033's browser-initiated path, avoiding two
decisions or two counts for one request. Loads with no attributable local frame
also stay unfiltered rather than inventing a document owner.

No feature guard. Every refusal lives downstream: a non-http(s) request, a site
exception, an allowlisted document, Ads and trackers switched off, or no
active ruleset each answers allow from the browser implementation.

## Why the overlay cannot host it

`RenderFrameCreated` is the embedder's one notification that a frame exists,
and `URLLoaderThrottleProviderImpl` is Chrome's renderer resource funnel. An
overlay module has nothing to attach to before those calls.

## Rebase risk

**Low to medium.** The observer remains one neighbouring construction. The
loader provider carries one remote through the same construction/clone pattern
as Safe Browsing and one appended adapter after the security throttle.

**Retirement:** permanent while Ads and trackers exists.

## How this was exported

Originally exported by `./tools/chromium/export-patches` on 2026-08-29 from
the `Taffy-Patch: 0035` commit on `taffy/patched`. The renderer-resource hook
was added to that existing seam on 2026-09-04 after its focused device
vertical exposed the uncovered request class. The expanded queue file was
reverse-checked against the pinned checkout.
