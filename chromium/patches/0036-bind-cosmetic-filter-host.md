# 0036 — Bind the browser implementations of renderer filtering

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** CAP-BR-022 (Ads and trackers, decision
`0086-the-filter-engine-is-adblock-rust.md`)
**Estimated size:** ~10 modified upstream lines, 1 file

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/chrome_content_browser_client_receiver_bindings.cc` |
| Symbols | `ChromeContentBrowserClient::ExposeInterfacesToRenderer` and `RegisterAssociatedInterfaceBindersForRenderFrameHost` |

## The gap this closes

The renderer holds two deliberately narrow remotes and the browser owns the
profile state behind them. Cosmetics use an `AssociatedRemote` scoped to a
frame. Renderer-originated image, script, fetch and XHR requests use a regular
process-broker remote carrying a frame token. Downstream code can implement
both interfaces, but only the embedder can register their binders.

## The change

Two includes:

```cpp
#include "taffy/browser/cosmetic_filter_bindings.h"
#include "taffy/browser/filtering_request_filter.h"
```

The process registry gains `RequestFilter`, bound on the UI thread with the
renderer process id:

```cpp
registry->AddInterface<taffy::filtering::mojom::RequestFilter>(
    base::BindRepeating(&taffy::BindFilteringRequestFilter,
                        render_process_host->GetDeprecatedID()),
    ui_task_runner);
```

The associated registry retains the cosmetic registration in
`RegisterAssociatedInterfaceBindersForRenderFrameHost`, with the other
associated binders:

```cpp
associated_registry.AddInterface<taffy::filtering::mojom::CosmeticFilterHost>(
    base::BindRepeating(&taffy::BindCosmeticFilterHost, &render_frame_host));
```

The header pulls in the generated mojom type. `//taffy/browser` is already
in `//chrome/browser`'s `deps` from patch 0006 — this patch adds no build
edge of its own. The include is accepted by the same downstream rule
patch 0033 uses for `taffy/browser/filtering_throttles.h`.

Both implementations look up the profile with `GetForProfileIfExists` and
never construct the core-service manager. A profile with no manager, a
non-http(s) request, Ads and trackers off, a site exception, an allowlisted
document, or no active ruleset each fail open in overlay code.

## Why the overlay cannot host it

Renderer process and associated frame binders are registered in these two
embedder methods. The overlay cannot add either entry without this edit.

## Rebase risk

**Low.** Two `AddInterface` calls among their peers. A rename of either method
is a one-file retarget.

**Retirement:** permanent while Ads and trackers exists.

## How this was exported

Originally exported by `./tools/chromium/export-patches` on 2026-08-29 from
the `Taffy-Patch: 0036` commit on `taffy/patched`. The process-broker binder
was added to that existing filtering-binder seam on 2026-09-04. The expanded
queue file was reverse-checked against the pinned checkout.
