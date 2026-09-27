# 0006 — Link the page intelligence broker into the browser

**Status:** `[Current]` — the exported patch is the whole of this change. The
binder-map registration this specification used to argue for is **retired**:
`PageIntelligence` is channel-associated on both ends and never travels through
the per-frame binder map. See
decision 0032,
which closes `OD-027` by construction.
**Needed by:** WP-M2-02 — TaffyGo's browser-process C++ does not compile into
`//chrome/browser` without this edge, and does not reach the APK without it
either
**Estimated size:** ~1 modified upstream line, 1 file

## Upstream files and symbols

| | | |
|---|---|---|
| File | `//chrome/browser/BUILD.gn` | **exported** |
| Symbol | `source_set("browser")`'s `deps`, gaining `//taffy/browser` | |

## What the patch does

`0006-bind-the-page-intelligence-service-for-a-frame.patch` adds **one line to
`//chrome/browser/BUILD.gn`**: a `deps` entry on `//taffy/browser`.

That edge is what makes TaffyGo's browser-process C++ compile and link into
`//chrome/browser` at all, and it is also how that code reaches the APK — an
Android APK's Java side carries `assert_no_native_deps`, so native code arrives
through libchrome rather than through the product target's deps.

The file name still says "bind … for a frame" because a patch file is written
by `export-patches` from the commit and is never hand-edited; the name is
history, and this specification is the current statement of what the number
owns.

## What was retired, and why

The specification used to argue for a second half: a registration in the
per-frame binder map, historically `PopulateChromeFrameBinders` in
`//chrome/browser/chrome_browser_interface_binders.cc`, spelled roughly

```cpp
map.Add<taffy::mojom::PageIntelligenceService>(
    base::BindRepeating(&taffy::PageIntelligenceBroker::BindForFrame));
```

Neither symbol exists, and correcting the names would not have saved the
argument. The mechanism is wrong:

* `//taffy/contracts/bip/mojom/page_intelligence.mojom` declares
  `interface PageIntelligence`. `PageIntelligenceService` is a **C++** class in
  `//taffy/common/public/page_intelligence_service.h` — not a Mojo type, so
  it cannot be named in a binder map at all.
* `PageIntelligenceBroker` has no `BindForFrame`. It reaches a frame through
  `GetOrCreateEndpoint` and its `RenderFrameCreated` override.
* The interface is **channel-associated on both ends**. The browser holds a
  `mojo::AssociatedRemote<mojom::PageIntelligence>`
  (`browser/frame_observation_endpoint.h`) and acquires it from the frame's
  `RenderFrameHost::GetRemoteAssociatedInterfaces()`
  (`browser/frame_observation_endpoint.cc`); the renderer serves it from the
  frame's own `blink::AssociatedInterfaceRegistry`
  (`renderer/taffy_render_frame_observer.cc`) and receives it as a
  `mojo::AssociatedReceiver` (`renderer/page_intelligence_endpoint.h`); and the
  test seam overrides the *associated* binder
  (`test/support/scripted_renderer_endpoint.cc`).

A channel-associated interface is requested over the frame's existing message
pipe and is ordered with navigation on it. The per-frame binder map is the
registry for *non*-associated interfaces requested through
`BrowserInterfaceBroker`. Registering there would have created a second, unused
path and told a reader the ordering guarantee comes from somewhere it does not.

**Nothing replaces it.** The renderer already advertises the interface and the
browser already asks for it; the only thing that was missing is a production
caller for the broker, and that is patch
[0023](0023-attach-page-intelligence-to-tabs.md) in a different upstream file.

## Why the overlay cannot host what remains

It cannot host anything, and that is the point: a `deps` entry in
`//chrome/browser/BUILD.gn` is by definition upstream. There is no other way to
put a `//components` target into the browser's link.

## Rebase risk

**Low.** One line in a long sorted list.

**Retirement:** permanent while TaffyGo has browser-process code.

## Regenerating the patch

```bash
./tools/chromium/sync
./tools/chromium/export-patches
./tools/check fast
```
